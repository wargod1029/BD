#include "MvCameraControl.h"
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.hpp>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <rclcpp/rclcpp.hpp>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>
#include <cuda_runtime.h>
#include <cuda.h>
#include <cudaEGL.h>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudawarping.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <jetson-utils/cudaBayer.h>
#include <jetson-utils/cudaYUV.h>
#include <jetson-utils/logging.h>
#include <unordered_map>
#include <thread>
#include "NvInfer.h"
#include "NvOnnxParser.h"
#include "yolov8.h"

using namespace std;

// Pixel format enum (MVS SDK values)
enum PixelFormat : unsigned int {
  RGB8 = 0x02180014,
  BayerRG8 = 0x01080009,
  BayerRG12Packed = 0x010C002B,
  BayerGB12Packed = 0x010C002C,
  BayerGB8 = 0x0108000A,
  BayerGR8 = 0x01080008
};

// Timestamp structure for shared memory
struct time_stamp {
  int64_t high;
  int64_t low;
};

// Color output helpers
#define COLOR_RESET   "\x1b[0m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"

static const int kLightColourCodes[] = {
    190, 159, 225, 210, 215, 118, 226, 51, 201, 196, 208, 83, 99, 129, 162, 46, 220, 141
};
static const size_t kLightColourCount = sizeof(kLightColourCodes)/sizeof(kLightColourCodes[0]);

inline std::string GetColorForSerial(const std::string& serial)
{
    size_t h = std::hash<std::string>{}(serial);
    int colour_index = kLightColourCodes[h % kLightColourCount];
    return "\x1b[38;5;" + std::to_string(colour_index) + "m";
}

// Precompute undistortion maps on GPU
bool precomputeUndistortionMaps(int width, int height,
                                const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs,
                                float*& d_mapx, float*& d_mapy)
{
    cv::Mat newCameraMatrix = cameraMatrix.clone();

    cv::Mat mapx, mapy;
    cv::initUndistortRectifyMap(cameraMatrix, distCoeffs, cv::Mat(),
                                newCameraMatrix, cv::Size(width, height),
                                CV_32FC1, mapx, mapy);

    size_t mapSize = width * height * sizeof(float);
    cudaMalloc(&d_mapx, mapSize);
    cudaMalloc(&d_mapy, mapSize);

    cudaMemcpy(d_mapx, mapx.data, mapSize, cudaMemcpyHostToDevice);
    cudaMemcpy(d_mapy, mapy.data, mapSize, cudaMemcpyHostToDevice);

    return true;
}

// Undistort RGB image on GPU
bool undistortRGB(const uchar3* inputRGB, uchar3* outputRGB, int width, int height,
                  float* d_mapx, float* d_mapy)
{
    cv::cuda::GpuMat d_inputGpuMat(height, width, CV_8UC3, (void*)inputRGB);
    cv::cuda::GpuMat d_outputGpuMat(height, width, CV_8UC3, (void*)outputRGB);
    cv::cuda::GpuMat d_mapx_gpu(height, width, CV_32FC1, d_mapx);
    cv::cuda::GpuMat d_mapy_gpu(height, width, CV_32FC1, d_mapy);
    cv::cuda::remap(d_inputGpuMat, d_outputGpuMat, d_mapx_gpu, d_mapy_gpu, cv::INTER_LINEAR);
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        std::cerr << "CUDA error in undistortRGB: " << cudaGetErrorString(err) << std::endl;
        return false;
    }
    return true;
}

// Print MVS device info
bool PrintDeviceInfo(MV_CC_DEVICE_INFO* pstMVDevInfo)
{
  if (NULL == pstMVDevInfo)
  {
    printf("The Pointer of pstMVDevInfo is NULL!\n");
    return false;
  }
  if (pstMVDevInfo->nTLayerType == MV_GIGE_DEVICE)
  {
    int nIp1 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0xff000000) >> 24);
    int nIp2 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x00ff0000) >> 16);
    int nIp3 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x0000ff00) >> 8);
    int nIp4 = (pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x000000ff);

    printf("Device Model Name: %s\n", pstMVDevInfo->SpecialInfo.stGigEInfo.chModelName);
    printf("CurrentIp: %d.%d.%d.%d\n", nIp1, nIp2, nIp3, nIp4);
    printf("SerialNumber: %s\n", pstMVDevInfo->SpecialInfo.stGigEInfo.chSerialNumber);
  }
  else if (pstMVDevInfo->nTLayerType == MV_USB_DEVICE)
  {
    printf("Device Model Name: %s\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chModelName);
    printf("SerialNumber: %s\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chSerialNumber);
  }
  else
  {
    printf("Not support.\n");
  }
  return true;
}

// ============================================================================
// MVS Camera ROS2 Node
// ============================================================================
class MVSCameraNode : public rclcpp::Node
{
public:
  MVSCameraNode(const std::string& params_file, const std::string& node_name)
  : Node(node_name)
  {
    // --- Declare ROS2 parameters ---
    this->declare_parameter("model_onnx_path", "");
    this->declare_parameter("model_trt_path", "");

    // --- Parse YAML config file via OpenCV FileStorage ---
    cv::FileStorage Params(params_file, cv::FileStorage::READ);
    if (!Params.isOpened()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to open settings file at: %s", params_file.c_str());
      rclcpp::shutdown();
      return;
    }

    trigger_enable_ = static_cast<int>(Params["TriggerEnable"]);
    g_serial_number_ = static_cast<std::string>(Params["SerialNumber"]);
    std::string pub_topic = static_cast<std::string>(Params["TopicName"]);
    int pixel_format = static_cast<int>(Params["PixelFormat"]);
    image_scale_ = static_cast<float>(Params["image_scale"]);
    if (image_scale_ < 0.1f) image_scale_ = 1.0f;

    // --- Parse camera calibration ---
    std::vector<double> camData;
    cv::FileNode camNode = Params["CameraMatrix"];
    if (camNode.type() == cv::FileNode::SEQ && camNode.size() == 9) {
        for (int i = 0; i < 9; ++i) {
            camData.push_back(static_cast<double>(camNode[i]));
        }
    } else {
        RCLCPP_WARN(this->get_logger(), "Invalid or missing CameraMatrix in config file.");
        camData = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    }
    cameraMatrix_ = cv::Mat(3, 3, CV_64F, camData.data()).clone();

    std::vector<double> distData(8, 0.0);
    cv::FileNode distNode = Params["DistCoeffs"];
    if (distNode.type() == cv::FileNode::SEQ) {
        for (int i = 0; i < std::min<int>(distNode.size(), 8); ++i) {
            distData[i] = static_cast<double>(distNode[i]);
        }
    } else {
        RCLCPP_WARN(this->get_logger(), "Invalid or missing DistCoeffs in config file - using zeros.");
    }
    distCoeffs_ = cv::Mat(static_cast<int>(distData.size()), 1, CV_64F, distData.data()).clone();

    std::cout << "Camera Matrix:\n" << cameraMatrix_ << std::endl;
    std::cout << "Distortion Coefficients:\n" << distCoeffs_ << std::endl;

    // --- Precompute undistortion maps on GPU ---
    bool success = precomputeUndistortionMaps(4096, 2460, cameraMatrix_, distCoeffs_,
                                              d_mapx_, d_mapy_);
    if (!success) {
      RCLCPP_WARN(this->get_logger(), "Failed to precompute undistortion maps.");
    }

    // --- Resolve model paths ---
    std::string onnx_path = this->get_parameter("model_onnx_path").as_string();
    std::string trt_path = this->get_parameter("model_trt_path").as_string();

    // Fall back to default paths relative to install share directory if not specified
    if (onnx_path.empty()) {
      onnx_path = "models/best.onnx";  // relative to working dir
    }
    if (trt_path.empty()) {
      trt_path = "models/best.engine.NVIDIAGeForceRTX3080.fp16.1.1";
    }

    RCLCPP_INFO(this->get_logger(), "ONNX model path: %s", onnx_path.c_str());
    RCLCPP_INFO(this->get_logger(), "TensorRT engine path: %s", trt_path.c_str());

    // --- Create YOLOv8 detector ---
    YoloV8Config config;
    yoloV8_ = std::make_unique<YoloV8>(onnx_path, trt_path, config);

    // --- Set up image transport ---
    pub_ = image_transport::create_publisher(this, pub_topic);

    // --- Set up shared memory for timestamp ---
    const char* user_name = getlogin();
    std::string path_for_time_stamp = "/home/" + std::string(user_name) + "/timeshare";
    const char* shared_file_name = path_for_time_stamp.c_str();

    int fd = open(shared_file_name, O_RDWR);
    if (fd > -1) {
      pointt_ = (time_stamp*)mmap(NULL, sizeof(time_stamp), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    } else {
      pointt_ = (time_stamp*)MAP_FAILED;
    }

    // --- Set up signal handler ---
    setupSignalHandler();

    // --- Enumerate and open camera ---
    int nRet = MV_OK;
    MV_CC_DEVICE_INFO_LIST stDeviceList;
    memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));

    nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
    if (MV_OK != nRet) {
      RCLCPP_ERROR(this->get_logger(), "MV_CC_EnumDevices fail! nRet [0x%x]", nRet);
      rclcpp::shutdown();
      return;
    }
    if (stDeviceList.nDeviceNum == 0) {
      RCLCPP_ERROR(this->get_logger(), "Find No Devices!");
      rclcpp::shutdown();
      return;
    }

    for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++) {
        printf("[device %d]:\n", i);
        MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
        if (pDeviceInfo) {
          PrintDeviceInfo(pDeviceInfo);
        }
    }

    unsigned int nIndex = 0;
    if (stDeviceList.nDeviceNum > 1) {
      if (g_serial_number_.empty()) {
        RCLCPP_ERROR(this->get_logger(), "Multiple devices found, but no SerialNumber specified in config!");
        rclcpp::shutdown();
        return;
      }
      bool find_expect_camera = false;
      for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++) {
        if (stDeviceList.pDeviceInfo[i] == NULL) continue;
        std::string serial_number;
        if (stDeviceList.pDeviceInfo[i]->nTLayerType == MV_USB_DEVICE) {
          serial_number = std::string((char*)stDeviceList.pDeviceInfo[i]->SpecialInfo.stUsb3VInfo.chSerialNumber);
        } else if (stDeviceList.pDeviceInfo[i]->nTLayerType == MV_GIGE_DEVICE) {
          serial_number = std::string((char*)stDeviceList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.chSerialNumber);
        }
        if (g_serial_number_ == serial_number) {
          find_expect_camera = true;
          nIndex = i;
          break;
        }
      }
      if (!find_expect_camera) {
        RCLCPP_ERROR(this->get_logger(), "Can not find the camera with serial number %s",
                     g_serial_number_.c_str());
        rclcpp::shutdown();
        return;
      }
    }

    MV_CC_DEVICE_INFO* pSelectedDevInfo = stDeviceList.pDeviceInfo[nIndex];
    if (pSelectedDevInfo->nTLayerType == MV_GIGE_DEVICE) {
        g_serial_number_ = (char*)pSelectedDevInfo->SpecialInfo.stGigEInfo.chSerialNumber;
    } else if (pSelectedDevInfo->nTLayerType == MV_USB_DEVICE) {
        g_serial_number_ = (char*)pSelectedDevInfo->SpecialInfo.stUsb3VInfo.chSerialNumber;
    }

    nRet = MV_CC_CreateHandle(&handle_, stDeviceList.pDeviceInfo[nIndex]);
    if (MV_OK != nRet) {
      RCLCPP_ERROR(this->get_logger(), "MV_CC_CreateHandle fail! nRet [0x%x]", nRet);
      rclcpp::shutdown();
      return;
    }
    nRet = MV_CC_OpenDevice(handle_);
    if (MV_OK != nRet) {
      RCLCPP_ERROR(this->get_logger(), "MV_CC_OpenDevice fail! nRet [0x%x]", nRet);
      rclcpp::shutdown();
      return;
    }
    nRet = MV_CC_SetBoolValue(handle_, "AcquisitionFrameRateEnable", false);
    if (MV_OK != nRet) {
      RCLCPP_ERROR(this->get_logger(), "set AcquisitionFrameRateEnable fail! nRet [0x%x]", nRet);
      rclcpp::shutdown();
      return;
    }
    nRet = MV_CC_SetEnumValue(handle_, "PixelFormat", PIXEL_FORMAT[pixel_format]);
    if (nRet != MV_OK) {
      RCLCPP_ERROR(this->get_logger(), "Pixel setting can't work.");
      rclcpp::shutdown();
      return;
    }
    setParams(handle_, params_file);
    nRet = MV_CC_SetEnumValue(handle_, "TriggerMode", trigger_enable_);
    if (MV_OK != nRet) {
      RCLCPP_ERROR(this->get_logger(), "MV_CC_SetTriggerMode fail! nRet [0x%x]", nRet);
      rclcpp::shutdown();
      return;
    }
    nRet = MV_CC_SetEnumValue(handle_, "TriggerSource", MV_TRIGGER_SOURCE_LINE0);
    if (MV_OK != nRet) {
      RCLCPP_ERROR(this->get_logger(), "MV_CC_SetTriggerSource fail! nRet [0x%x]", nRet);
      rclcpp::shutdown();
      return;
    }

    RCLCPP_INFO(this->get_logger(), "Finish all params set! Start grabbing...");
    nRet = MV_CC_StartGrabbing(handle_);
    if (MV_OK != nRet) {
      RCLCPP_ERROR(this->get_logger(), "Start Grabbing fail.");
      rclcpp::shutdown();
      return;
    }

    // --- Start the grabbing thread ---
    exit_flag_ = false;
    worker_thread_ = std::thread(&MVSCameraNode::workThread, this);
  }

  ~MVSCameraNode() override
  {
    exit_flag_ = true;
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }

    if (handle_) {
      MV_CC_StopGrabbing(handle_);
      MV_CC_CloseDevice(handle_);
      MV_CC_DestroyHandle(handle_);
    }

    if (pointt_ != MAP_FAILED) {
      munmap(pointt_, sizeof(time_stamp));
    }
    if (d_mapx_) cudaFree(d_mapx_);
    if (d_mapy_) cudaFree(d_mapy_);
  }

  bool isDone() const { return exit_flag_; }

private:
  void setParams(void* handle, const std::string& params_file)
  {
    cv::FileStorage Params(params_file, cv::FileStorage::READ);
    if (!Params.isOpened()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to open settings file at: %s", params_file.c_str());
      return;
    }
    int ExposureTimeLower = static_cast<int>(Params["AutoExposureTimeLower"]);
    int ExposureTimeUpper = static_cast<int>(Params["AutoExposureTimeUpper"]);
    int ExposureTime = static_cast<int>(Params["ExposureTime"]);
    int ExposureAutoMode = static_cast<int>(Params["ExposureAutoMode"]);
    int GainAuto = static_cast<int>(Params["GainAuto"]);
    float Gain = static_cast<float>(Params["Gain"]);
    float AutoGainLowerLimit = static_cast<float>(Params["AutoGainLowerLimit"]);
    float AutoGainUpperLimit = static_cast<float>(Params["AutoGainUpperLimit"]);
    float Gamma = static_cast<float>(Params["Gamma"]);
    int GammaSlector = static_cast<int>(Params["GammaSelector"]);
    int Brightness = static_cast<int>(Params["Brightness"]);
    bool SuperBayerEnable = true;
    bool GammaEnable = true;
    bool AutoFunctionAOIUsageIntensity = true;
    int AutoFunctionAOIHeight = static_cast<int>(Params["AutoFunctionAOIHeight"]);
    int AutoFunctionAOIWidth = static_cast<int>(Params["AutoFunctionAOIWidth"]);
    int AutoFunctionAOIOffsetX = static_cast<int>(Params["AutoFunctionAOIOffsetX"]);
    int AutoFunctionAOIOffsetY = static_cast<int>(Params["AutoFunctionAOIOffsetY"]);
    std::cout << "Brightness: " << Brightness << std::endl;

    int nRet;
    nRet = MV_CC_SetExposureAutoMode(handle, ExposureAutoMode);
    if (ExposureAutoMode == 2) {
      nRet = MV_CC_SetAutoExposureTimeLower(handle, ExposureTimeLower);
      nRet = MV_CC_SetAutoExposureTimeUpper(handle, ExposureTimeUpper);
    } else if (ExposureAutoMode == 0) {
      nRet = MV_CC_SetExposureTime(handle, ExposureTime);
    }
    nRet = MV_CC_SetEnumValue(handle, "GainAuto", GainAuto);
    if (GainAuto == 0) {
      nRet = MV_CC_SetGain(handle, Gain);
    } else if (GainAuto == 2) {
      nRet = MV_CC_SetFloatValue(handle, "AutoGainLowerLimit", AutoGainLowerLimit);
      nRet = MV_CC_SetFloatValue(handle, "AutoGainUpperLimit", AutoGainUpperLimit);
    }
    nRet = MV_CC_SetGammaSelector(handle, GammaSlector);
    nRet = MV_CC_SetGamma(handle, Gamma);
    nRet = MV_CC_SetBrightness(handle, Brightness);
    // Fine-tune camera settings
    nRet = MV_CC_SetBoolValue(handle, "SuperBayerEnable", SuperBayerEnable);
    nRet = MV_CC_SetBoolValue(handle, "GammaEnable", GammaEnable);
    nRet = MV_CC_SetBoolValue(handle, "AutoFunctionAOIUsageIntensity", AutoFunctionAOIUsageIntensity);
    nRet = MV_CC_SetIntValue(handle, "AutoFunctionAOIHeight", AutoFunctionAOIHeight);
    nRet = MV_CC_SetIntValue(handle, "AutoFunctionAOIWidth", AutoFunctionAOIWidth);
    nRet = MV_CC_SetIntValue(handle, "AutoFunctionAOIOffsetX", AutoFunctionAOIOffsetX);
    nRet = MV_CC_SetIntValue(handle, "AutoFunctionAOIOffsetY", AutoFunctionAOIOffsetY);
  }

  static void signalHandlerStatic(int signal)
  {
    if (signal == SIGINT) {
      fprintf(stderr, "\nReceived Ctrl+C, exiting...\n");
      if (s_node_instance_) {
        s_node_instance_->exit_flag_ = true;
      }
    }
  }

  void setupSignalHandler()
  {
    s_node_instance_ = this;
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = MVSCameraNode::signalHandlerStatic;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);
  }

  void workThread()
  {
    int nRet = MV_OK;

    MVCC_INTVALUE stParam;
    memset(&stParam, 0, sizeof(MVCC_INTVALUE));
    nRet = MV_CC_GetIntValue(handle_, "PayloadSize", &stParam);
    if (MV_OK != nRet) {
      printf("Get PayloadSize fail! nRet [0x%x]\n", nRet);
      return;
    }

    MV_FRAME_OUT_INFO_EX stImageInfo = {0};

    unsigned char* pData = (unsigned char*)malloc(sizeof(unsigned char) * stParam.nCurValue);
    if (pData == nullptr) {
      printf("Memory allocation failed!\n");
      return;
    }

    cudaMalloc(&bayerGPU_, 4096 * 2460);
    cudaMalloc(&rgbGPU_, 4096 * 2460 * sizeof(uchar3));
    cudaMalloc(&undistortedGPU_, 4096 * 2460 * sizeof(uchar3));

    while (!exit_flag_ && rclcpp::ok()) {
      auto grab_start = std::chrono::high_resolution_clock::now();
      nRet = MV_CC_GetOneFrameTimeout(handle_, pData, stParam.nCurValue, &stImageInfo, 1000);
      auto grab_end = std::chrono::high_resolution_clock::now();
      auto grab_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(grab_end - grab_start).count();

      if (nRet == MV_OK) {
        rclcpp::Time rcv_time;
        if (trigger_enable_ && pointt_ != MAP_FAILED && pointt_->low != 0) {
          // Use timestamp from shared memory
          int64_t b = pointt_->low;
          double time_pc = b / 1000000000.0 + 0.1;
          rcv_time = rclcpp::Time(static_cast<int64_t>(time_pc * 1e9));
          std::cout << "Received timestamp from shared memory: " << rcv_time.seconds() << std::endl;
        } else {
          rcv_time = this->get_clock()->now();
        }

        // Step 1: Copy Bayer data to GPU
        cudaMemcpy(bayerGPU_, pData, stImageInfo.nFrameLen, cudaMemcpyHostToDevice);

        // Step 2: Debayer on GPU
        cudaBayerToRGB(bayerGPU_, rgbGPU_, stImageInfo.nWidth, stImageInfo.nHeight, IMAGE_BAYER_GRBG);

        // Step 3: Undistort on GPU
        undistortRGB(rgbGPU_, undistortedGPU_, stImageInfo.nWidth, stImageInfo.nHeight,
                    d_mapx_, d_mapy_);

        // Step 4: Wrap in GpuMat
        cv::cuda::GpuMat img_full_res_gpu(stImageInfo.nHeight, stImageInfo.nWidth, CV_8UC3, undistortedGPU_);

        // Step 5: Resize on GPU before detection
        cv::cuda::GpuMat img_scaled_gpu;
        cv::cuda::resize(img_full_res_gpu, img_scaled_gpu, cv::Size(), image_scale_, image_scale_, cv::INTER_LINEAR);

        auto proc_start = std::chrono::high_resolution_clock::now();

        // Step 6: Run detection and PII blurring on the scaled image
        const auto objects = yoloV8_->detectObjects(img_scaled_gpu);
        yoloV8_->blurDetectedObjectsOnGPU(img_scaled_gpu, objects);

        // Step 7: Download the final processed image from GPU to CPU
        cv::Mat img_final_cpu;
        img_scaled_gpu.download(img_final_cpu);

        // Step 8: Convert to ROS Image and publish
        sensor_msgs::msg::Image::SharedPtr msg =
            cv_bridge::CvImage(std_msgs::msg::Header(), "rgb8", img_final_cpu).toImageMsg();
        msg->header.stamp = rcv_time;
        pub_.publish(msg);

        auto proc_end = std::chrono::high_resolution_clock::now();
        auto proc_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(proc_end - proc_start).count();
        std::string color = GetColorForSerial(g_serial_number_);
        std::cout << color << "SN [" << g_serial_number_ << "] objects: " << objects.size()
                  << " | grab: " << grab_duration_ms << " ms, processing: " << proc_duration_ms
                  << " ms" << COLOR_RESET << std::endl;
      }
    }

    free(pData);
    cudaFree(bayerGPU_);
    cudaFree(rgbGPU_);
    cudaFree(undistortedGPU_);
  }

  // --- ROS2 members ---
  image_transport::Publisher pub_;

  // --- Camera / MVS members ---
  void* handle_ = nullptr;
  std::string g_serial_number_;
  int trigger_enable_ = 1;
  float image_scale_ = 1.0f;
  std::vector<PixelFormat> PIXEL_FORMAT = { RGB8, BayerRG8, BayerRG12Packed, BayerGB12Packed, BayerGB8, BayerGR8 };

  // --- Calibration ---
  cv::Mat cameraMatrix_;
  cv::Mat distCoeffs_;
  float* d_mapx_ = nullptr;
  float* d_mapy_ = nullptr;

  // --- YOLOv8 ---
  std::unique_ptr<YoloV8> yoloV8_;

  // --- GPU buffers ---
  uint8_t* bayerGPU_ = nullptr;
  uchar3* rgbGPU_ = nullptr;
  uchar3* undistortedGPU_ = nullptr;

  // --- Shared memory ---
  time_stamp* pointt_ = nullptr;

  // --- Threading ---
  std::thread worker_thread_;
  bool exit_flag_ = false;

  // --- Signal handling ---
  static MVSCameraNode* s_node_instance_;
};

MVSCameraNode* MVSCameraNode::s_node_instance_ = nullptr;

// ============================================================================
// Main
// ============================================================================
int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);

  if (argc < 2) {
    RCLCPP_ERROR(rclcpp::get_logger("mvs_driver"), "Usage: grabImgWithTrigger <config.yaml>");
    return -1;
  }

  std::string params_file = std::string(argv[1]);

  auto node = std::make_shared<MVSCameraNode>(params_file, "mvs_trigger");

  // Spin the node minimally — the workThread does the heavy lifting
  while (rclcpp::ok() && !node->isDone()) {
    rclcpp::spin_some(node);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  rclcpp::shutdown();
  return 0;
}
