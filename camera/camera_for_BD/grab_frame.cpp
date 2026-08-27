// Simple USB3 camera frame grabber — CPU only, no GPU needed
// Usage: ./grab_frame [num_frames] [output_prefix]
// Saves frames as PNG files (requires OpenCV for encoding)
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <opencv2/opencv.hpp>
#include "MvCameraControl.h"

int main(int argc, char* argv[]) {
    int numFrames = (argc > 1) ? atoi(argv[1]) : 5;
    const char* prefix = (argc > 2) ? argv[2] : "frame";

    // 1. Find camera
    MV_CC_DEVICE_INFO_LIST stDeviceList;
    memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    int nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
    if (MV_OK != nRet || stDeviceList.nDeviceNum == 0) {
        printf("No camera found!\n");
        return -1;
    }
    printf("Found camera: %s\n\n",
        stDeviceList.pDeviceInfo[0]->nTLayerType == MV_USB_DEVICE ?
        (char*)stDeviceList.pDeviceInfo[0]->SpecialInfo.stUsb3VInfo.chSerialNumber :
        (char*)stDeviceList.pDeviceInfo[0]->SpecialInfo.stGigEInfo.chSerialNumber);

    // 2. Open device
    void* handle = NULL;
    nRet = MV_CC_CreateHandle(&handle, stDeviceList.pDeviceInfo[0]);
    if (MV_OK != nRet) { printf("CreateHandle failed: 0x%x\n", nRet); return -1; }

    nRet = MV_CC_OpenDevice(handle);
    if (MV_OK != nRet) { printf("OpenDevice failed: 0x%x\n", nRet); MV_CC_DestroyHandle(handle); return -1; }

    // 3. Get camera info
    MVCC_INTVALUE stParam;
    memset(&stParam, 0, sizeof(MVCC_INTVALUE));
    MV_CC_GetIntValue(handle, "PayloadSize", &stParam);
    unsigned int payloadSize = stParam.nCurValue;
    MV_CC_GetIntValue(handle, "Width", &stParam);
    int width = stParam.nCurValue;
    MV_CC_GetIntValue(handle, "Height", &stParam);
    int height = stParam.nCurValue;
    printf("Resolution: %d x %d\n", width, height);

    // 4. Set exposure and gain
    MV_CC_SetEnumValue(handle, "ExposureAuto", 0); // Off
    MV_CC_SetFloatValue(handle, "ExposureTime", 45000.0); // 5ms
    MV_CC_SetFloatValue(handle, "Gain", 0.0);
    MV_CC_SetFloatValue(handle, "Gamma", 0.7);

    // 5. Start grabbing
    nRet = MV_CC_StartGrabbing(handle);
    if (MV_OK != nRet) { printf("StartGrabbing failed: 0x%x\n", nRet); MV_CC_CloseDevice(handle); MV_CC_DestroyHandle(handle); return -1; }

    printf("Grabbing %d frames...\n", numFrames);

    // 6. Grab frames — BGR24 output is width * height * 3
    unsigned int bgrSize = width * height * 3;
    unsigned char* pData = (unsigned char*)malloc(bgrSize);
    for (int i = 0; i < numFrames; i++) {
        MV_FRAME_OUT_INFO_EX stFrameInfo;
        memset(&stFrameInfo, 0, sizeof(MV_FRAME_OUT_INFO_EX));
        memset(pData, 0, bgrSize);

        nRet = MV_CC_GetImageForBGR(handle, pData, bgrSize, &stFrameInfo, 2000);
        if (MV_OK == nRet) {
            char filename[256];
            snprintf(filename, sizeof(filename), "%s_%04d.png", prefix, i);

            // Wrap raw BGR data into an OpenCV Mat and save as PNG
            cv::Mat img(height, width, CV_8UC3, pData);
            cv::imwrite(filename, img);

            printf("  [%d/%d] Saved %s (%d x %d)\n", i+1, numFrames, filename, width, height);
        } else {
            printf("  [%d/%d] Grab failed: 0x%x\n", i+1, numFrames, nRet);
        }
    }

    // 7. Cleanup
    free(pData);
    MV_CC_StopGrabbing(handle);
    MV_CC_CloseDevice(handle);
    MV_CC_DestroyHandle(handle);
    printf("\nDone.\n");
    return 0;
}
