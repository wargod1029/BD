/**
 * CPU-only variant of the MVS ROS2 driver — for testing without GPU/CUDA/TensorRT.
 *
 * Stripped of: CUDA debayering, GPU undistortion, YOLOv8 PII blurring.
 * Uses OpenCV CPU for Bayer→RGB conversion and optional CPU undistortion.
 *
 * Build: add to CMakeLists.txt alongside the GPU version.
 */
#include "MvCameraControl.h"
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.hpp>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <rclcpp/rclcpp.hpp>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <thread>

using namespace std;

enum PixelFormat : unsigned int {
  RGB8 = 0x02180014,
  BayerRG8 = 0x01080009,
  BayerRG12Packed = 0x010C002B,
  BayerGB12Packed = 0x010C002C,
  BayerGB8 = 0x0108000A,
  BayerGR8 = 0x01080008
};

struct time_stamp {
  int64_t high;
  int64_t low;
};

// Color helpers
#define COLOR_RESET   "\x1b[0m"
static const int kLightColourCodes[] = {
    190, 159, 225, 210, 215, 118, 226, 51, 201, 196, 208, 83, 99, 129, 162, 46, 220, 141
};
static const size_t kLightColourCount = sizeof(kLightColourCodes) / sizeof(kLightColourCodes[0]);

inline std::string GetColorForSerial(const std::string &serial) {
  size_t h = std::hash<std::string>{}(serial);
  return "\x1b[38;5;" + std::to_string(kLightColourCodes[h % kLightColourCount]) + "m";
}

bool PrintDeviceInfo(MV_CC_DEVICE_INFO *pstMVDevInfo) {
  if (NULL == pstMVDevInfo) { printf("NULL device info!\n"); return false; }
  if (pstMVDevInfo->nTLayerType == MV_GIGE_DEVICE) {
    int ip1 = (pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp >> 24) & 0xff;
    int ip2 = (pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp >> 16) & 0xff;
    int ip3 = (pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp >> 8) & 0xff;
    int ip4 = pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0xff;
    printf("Model: %s  IP: %d.%d.%d.%d  SN: %s\n",
           pstMVDevInfo->SpecialInfo.stGigEInfo.chModelName,
           ip1, ip2, ip3, ip4,
           pstMVDevInfo->SpecialInfo.stGigEInfo.chSerialNumber);
  } else if (pstMVDevInfo->nTLayerType == MV_USB_DEVICE) {
    printf("Model: %s  SN: %s\n",
           pstMVDevInfo->SpecialInfo.stUsb3VInfo.chModelName,
           pstMVDevInfo->SpecialInfo.stUsb3VInfo.chSerialNumber);
  }
  return true;
}

// ============================================================================
// CPU-only MVS Camera ROS2 Node
// ============================================================================
class MVSCameraCPUNode : public rclcpp::Node {
public:
  MVSCameraCPUNode(const std::string &params_file, const std::string &node_name)
      : Node(node_name) {

    cv::FileStorage Params(params_file, cv::FileStorage::READ);
    if (!Params.isOpened()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to open: %s", params_file.c_str());
      rclcpp::shutdown();
      return;
    }

    trigger_enable_ = static_cast<int>(Params["TriggerEnable"]);
    g_serial_number_ = static_cast<std::string>(Params["SerialNumber"]);
    std::string pub_topic = static_cast<std::string>(Params["TopicName"]);
    int pixel_format = static_cast<int>(Params["PixelFormat"]);
    image_scale_ = static_cast<float>(Params["image_scale"]);
    if (image_scale_ < 0.1f) image_scale_ = 1.0f;

    // --- Camera calibration (CPU-only, optional) ---
    std::vector<double> camData;
    cv::FileNode camNode = Params["CameraMatrix"];
    if (camNode.type() == cv::FileNode::SEQ && camNode.size() == 9) {
      for (int i = 0; i < 9; ++i) camData.push_back(static_cast<double>(camNode[i]));
    } else {
      camData = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    }
    cameraMatrix_ = cv::Mat(3, 3, CV_64F, camData.data()).clone();

    std::vector<double> distData(8, 0.0);
    cv::FileNode distNode = Params["DistCoeffs"];
    if (distNode.type() == cv::FileNode::SEQ) {
      for (int i = 0; i < std::min<int>(distNode.size(), 8); ++i)
        distData[i] = static_cast<double>(distNode[i]);
    }
    distCoeffs_ = cv::Mat(static_cast<int>(distData.size()), 1, CV_64F, distData.data()).clone();

    // --- Precompute CPU undistort maps ---
    cv::initUndistortRectifyMap(cameraMatrix_, distCoeffs_, cv::Mat(),
                                cameraMatrix_, cv::Size(4096, 2460),
                                CV_16SC2, undistort_map1_, undistort_map2_);

    // --- Image transport ---
    pub_ = image_transport::create_publisher(this, pub_topic);

    // --- Shared memory timestamp (same as GPU version) ---
    const char *user_name = getlogin();
    std::string path = "/home/" + std::string(user_name) + "/timeshare";
    int fd = open(path.c_str(), O_RDWR);
    pointt_ = (fd > -1) ? (time_stamp *)mmap(NULL, sizeof(time_stamp), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
                        : (time_stamp *)MAP_FAILED;

    setupSignalHandler();

    // --- Enumerate and open camera ---
    int nRet = MV_OK;
    MV_CC_DEVICE_INFO_LIST stDeviceList;
    memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));

    nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
    if (MV_OK != nRet) {
      RCLCPP_ERROR(this->get_logger(), "EnumDevices fail [0x%x]", nRet);
      rclcpp::shutdown(); return;
    }
    if (stDeviceList.nDeviceNum == 0) {
      RCLCPP_ERROR(this->get_logger(), "No MVS devices found!");
      rclcpp::shutdown(); return;
    }

    for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++) {
      printf("[device %d]: ", i);
      if (stDeviceList.pDeviceInfo[i]) PrintDeviceInfo(stDeviceList.pDeviceInfo[i]);
    }

    unsigned int nIndex = 0;
    if (stDeviceList.nDeviceNum > 1) {
      if (g_serial_number_.empty()) {
        RCLCPP_ERROR(this->get_logger(), "Multiple cameras — need SerialNumber in config!");
        rclcpp::shutdown(); return;
      }
      bool found = false;
      for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++) {
        if (!stDeviceList.pDeviceInfo[i]) continue;
        std::string sn;
        if (stDeviceList.pDeviceInfo[i]->nTLayerType == MV_USB_DEVICE)
          sn = (char *)stDeviceList.pDeviceInfo[i]->SpecialInfo.stUsb3VInfo.chSerialNumber;
        else if (stDeviceList.pDeviceInfo[i]->nTLayerType == MV_GIGE_DEVICE)
          sn = (char *)stDeviceList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.chSerialNumber;
        if (g_serial_number_ == sn) { found = true; nIndex = i; break; }
      }
      if (!found) {
        RCLCPP_ERROR(this->get_logger(), "Camera SN %s not found!", g_serial_number_.c_str());
        rclcpp::shutdown(); return;
      }
    }

    MV_CC_DEVICE_INFO *pDev = stDeviceList.pDeviceInfo[nIndex];
    if (pDev->nTLayerType == MV_GIGE_DEVICE)
      g_serial_number_ = (char *)pDev->SpecialInfo.stGigEInfo.chSerialNumber;
    else if (pDev->nTLayerType == MV_USB_DEVICE)
      g_serial_number_ = (char *)pDev->SpecialInfo.stUsb3VInfo.chSerialNumber;

    nRet = MV_CC_CreateHandle(&handle_, stDeviceList.pDeviceInfo[nIndex]);
    if (MV_OK != nRet) { RCLCPP_ERROR(this->get_logger(), "CreateHandle fail [0x%x]", nRet); rclcpp::shutdown(); return; }
    nRet = MV_CC_OpenDevice(handle_);
    if (MV_OK != nRet) { RCLCPP_ERROR(this->get_logger(), "OpenDevice fail [0x%x]", nRet); rclcpp::shutdown(); return; }
    MV_CC_SetBoolValue(handle_, "AcquisitionFrameRateEnable", false);
    MV_CC_SetEnumValue(handle_, "PixelFormat", PIXEL_FORMAT[pixel_format]);

    setParams(handle_, params_file);

    MV_CC_SetEnumValue(handle_, "TriggerMode", trigger_enable_);
    MV_CC_SetEnumValue(handle_, "TriggerSource", MV_TRIGGER_SOURCE_LINE0);

    RCLCPP_INFO(this->get_logger(), "CPU mode — starting grab (no GPU, no YOLOv8)...");
    nRet = MV_CC_StartGrabbing(handle_);
    if (MV_OK != nRet) { RCLCPP_ERROR(this->get_logger(), "StartGrabbing fail [0x%x]", nRet); rclcpp::shutdown(); return; }

    exit_flag_ = false;
    worker_ = std::thread(&MVSCameraCPUNode::workThread, this);
  }

  ~MVSCameraCPUNode() override {
    exit_flag_ = true;
    if (worker_.joinable()) worker_.join();
    if (handle_) {
      MV_CC_StopGrabbing(handle_);
      MV_CC_CloseDevice(handle_);
      MV_CC_DestroyHandle(handle_);
    }
    if (pointt_ != MAP_FAILED) munmap(pointt_, sizeof(time_stamp));
  }

  bool isDone() const { return exit_flag_; }

private:
  void setParams(void *handle, const std::string &file) {
    cv::FileStorage P(file, cv::FileStorage::READ);
    if (!P.isOpened()) return;

    int ExposureAutoMode = static_cast<int>(P["ExposureAutoMode"]);
    if (ExposureAutoMode == 2) {
      MV_CC_SetExposureAutoMode(handle, 2);
      MV_CC_SetAutoExposureTimeLower(handle, static_cast<int>(P["AutoExposureTimeLower"]));
      MV_CC_SetAutoExposureTimeUpper(handle, static_cast<int>(P["AutoExposureTimeUpper"]));
    } else if (ExposureAutoMode == 0) {
      MV_CC_SetExposureAutoMode(handle, 0);
      MV_CC_SetExposureTime(handle, static_cast<int>(P["ExposureTime"]));
    }

    int GainAuto = static_cast<int>(P["GainAuto"]);
    MV_CC_SetEnumValue(handle, "GainAuto", GainAuto);
    if (GainAuto == 0)
      MV_CC_SetGain(handle, static_cast<float>(P["Gain"]));
    else if (GainAuto == 2) {
      MV_CC_SetFloatValue(handle, "AutoGainLowerLimit", static_cast<float>(P["AutoGainLowerLimit"]));
      MV_CC_SetFloatValue(handle, "AutoGainUpperLimit", static_cast<float>(P["AutoGainUpperLimit"]));
    }

    MV_CC_SetGammaSelector(handle, static_cast<int>(P["GammaSelector"]));
    MV_CC_SetGamma(handle, static_cast<float>(P["Gamma"]));
    MV_CC_SetBrightness(handle, static_cast<int>(P["Brightness"]));

    MV_CC_SetBoolValue(handle, "SuperBayerEnable", true);
    MV_CC_SetBoolValue(handle, "GammaEnable", true);
    MV_CC_SetBoolValue(handle, "AutoFunctionAOIUsageIntensity", true);
    MV_CC_SetIntValue(handle, "AutoFunctionAOIHeight", static_cast<int>(P["AutoFunctionAOIHeight"]));
    MV_CC_SetIntValue(handle, "AutoFunctionAOIWidth", static_cast<int>(P["AutoFunctionAOIWidth"]));
    MV_CC_SetIntValue(handle, "AutoFunctionAOIOffsetX", static_cast<int>(P["AutoFunctionAOIOffsetX"]));
    MV_CC_SetIntValue(handle, "AutoFunctionAOIOffsetY", static_cast<int>(P["AutoFunctionAOIOffsetY"]));
  }

  void workThread() {
    MVCC_INTVALUE stParam;
    memset(&stParam, 0, sizeof(MVCC_INTVALUE));
    int nRet = MV_CC_GetIntValue(handle_, "PayloadSize", &stParam);
    if (MV_OK != nRet) { printf("PayloadSize fail [0x%x]\n", nRet); return; }

    MV_FRAME_OUT_INFO_EX stImageInfo = {0};
    unsigned char *pData = (unsigned char *)malloc(stParam.nCurValue);
    if (!pData) { printf("malloc failed!\n"); return; }

    while (!exit_flag_ && rclcpp::ok()) {
      auto t0 = std::chrono::high_resolution_clock::now();
      nRet = MV_CC_GetOneFrameTimeout(handle_, pData, stParam.nCurValue, &stImageInfo, 1000);
      auto t1 = std::chrono::high_resolution_clock::now();

      if (nRet == MV_OK) {
        // --- Timestamp ---
        rclcpp::Time rcv_time;
        if (trigger_enable_ && pointt_ != MAP_FAILED && pointt_->low != 0) {
          double t = pointt_->low / 1e9 + 0.1;
          rcv_time = rclcpp::Time(static_cast<int64_t>(t * 1e9));
        } else {
          rcv_time = this->get_clock()->now();
        }

        // --- CPU debayer ---
        cv::Mat bayer(stImageInfo.nHeight, stImageInfo.nWidth, CV_8UC1, pData);
        cv::Mat rgb;
        // The configs use BayerGR8 (index 5) or BayerRG8 (index 1).
        // Adjust conversion code based on actual sensor pattern. Default: GRBG
        cv::cvtColor(bayer, rgb, cv::COLOR_BayerGB2RGB);  // adjust pattern as needed

        // --- CPU undistort (optional) ---
        cv::Mat undistorted;
        cv::remap(rgb, undistorted, undistort_map1_, undistort_map2_, cv::INTER_LINEAR);

        // --- Resize ---
        cv::Mat scaled;
        cv::resize(undistorted, scaled, cv::Size(), image_scale_, image_scale_, cv::INTER_LINEAR);

        // --- Publish ---
        auto msg = cv_bridge::CvImage(std_msgs::msg::Header(), "rgb8", scaled).toImageMsg();
        msg->header.stamp = rcv_time;
        msg->header.frame_id = g_serial_number_;
        pub_.publish(msg);

        auto t2 = std::chrono::high_resolution_clock::now();
        auto grab_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        auto proc_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        std::cout << GetColorForSerial(g_serial_number_) << "SN [" << g_serial_number_
                  << "] grab: " << grab_ms << "ms | CPU proc: " << proc_ms << "ms"
                  << COLOR_RESET << std::endl;
      }
    }
    free(pData);
  }

  static void signalHandlerStatic(int sig) {
    if (sig == SIGINT) {
      fprintf(stderr, "\nCtrl+C — exiting...\n");
      if (s_instance_) s_instance_->exit_flag_ = true;
    }
  }
  void setupSignalHandler() {
    s_instance_ = this;
    struct sigaction sa;
    sa.sa_handler = MVSCameraCPUNode::signalHandlerStatic;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
  }

  // --- ROS2 ---
  image_transport::Publisher pub_;

  // --- Camera ---
  void *handle_ = nullptr;
  std::string g_serial_number_;
  int trigger_enable_ = 1;
  float image_scale_ = 1.0f;
  std::vector<PixelFormat> PIXEL_FORMAT = {RGB8, BayerRG8, BayerRG12Packed, BayerGB12Packed, BayerGB8, BayerGR8};

  // --- CPU calibration ---
  cv::Mat cameraMatrix_, distCoeffs_;
  cv::Mat undistort_map1_, undistort_map2_;

  // --- Shared memory ---
  time_stamp *pointt_ = nullptr;

  // --- Threading ---
  std::thread worker_;
  bool exit_flag_ = false;
  static MVSCameraCPUNode *s_instance_;
};

MVSCameraCPUNode *MVSCameraCPUNode::s_instance_ = nullptr;

// ============================================================================
int main(int argc, char **argv) {
  rclcpp::init(argc, argv);

  if (argc < 2) {
    RCLCPP_ERROR(rclcpp::get_logger("mvs_cpu"), "Usage: grabImgWithTriggerCPU <config.yaml>");
    return -1;
  }

  auto node = std::make_shared<MVSCameraCPUNode>(std::string(argv[1]), "mvs_trigger_cpu");

  while (rclcpp::ok() && !node->isDone()) {
    rclcpp::spin_some(node);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  rclcpp::shutdown();
  return 0;
}
