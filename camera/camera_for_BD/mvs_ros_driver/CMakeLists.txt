cmake_minimum_required(VERSION 3.8)
project(mvs_ros_driver)

# C++17 required for std::clamp and structured bindings
add_compile_options(-std=c++17)

# ---------------------------------------------------------------------------
# Build mode: set -DBUILD_CPU_ONLY=ON to skip GPU deps (CUDA, TensorRT, jetson-utils)
# ---------------------------------------------------------------------------
option(BUILD_CPU_ONLY "Build CPU-only driver (no GPU, CUDA, TensorRT)" OFF)

# ---------------------------------------------------------------------------
# Platform-specific MVS SDK library path
# ---------------------------------------------------------------------------
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
  link_directories(/opt/MVS/lib/aarch64/)
  message(STATUS "Running on aarch64")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
  link_directories(/opt/MVS/lib/64/)
  message(STATUS "Running on x86_64")
endif()

# ---------------------------------------------------------------------------
# ROS2 ament dependencies (both modes)
# ---------------------------------------------------------------------------
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)

# ---------------------------------------------------------------------------
# Common include dirs (both modes)
# ---------------------------------------------------------------------------
include_directories(
  include
  /opt/MVS/include)

# ---------------------------------------------------------------------------
# GPU MODE — full CUDA / TensorRT / YOLOv8 pipeline
# ---------------------------------------------------------------------------
if(NOT BUILD_CPU_ONLY)
  message(STATUS "=== GPU MODE: building with CUDA / TensorRT / YOLOv8 ===")

  set(OpenCV_DIR "/home/kodifly/libraries/opencv/build")  # custom OpenCV 4.12.0 CUDA
  find_package(OpenCV REQUIRED COMPONENTS core imgproc dnn cudafeatures2d)
  message(STATUS "OpenCV version: ${OpenCV_VERSION}")

  find_package(PCL REQUIRED)
  find_package(CUDA REQUIRED)
  include_directories(${CUDA_INCLUDE_DIRS})

  find_package(jetson-utils REQUIRED)
  include_directories(${JETSON_MULTIMEDIA_API_INCLUDE_DIR})
  message(STATUS "JETSON_UTILS_INCLUDE_DIRS = ${JETSON_UTILS_INCLUDE_DIRS}")

  # TensorRT inference engine library
  add_library(tensorrt_cpp_api SHARED libs/tensorrt-cpp-api/src/engine.cpp)
  target_include_directories(tensorrt_cpp_api PUBLIC
    libs/tensorrt-cpp-api/src ${CUDA_INCLUDE_DIRS})

  # YoloV8 TensorRT library
  add_library(YoloV8_TRT SHARED src/yolov8.cpp)
  target_link_libraries(YoloV8_TRT PUBLIC
    tensorrt_cpp_api ${OpenCV_LIBS}
    nvinfer nvinfer_plugin nvonnxparser
    opencv_cudaimgproc opencv_cudaarithm)
  target_include_directories(YoloV8_TRT PUBLIC libs/tensorrt-cpp-api/src)

  include_directories(
    /usr/local/cuda/include
    ${OpenCV_INCLUDE_DIRS}
    ${PCL_INCLUDE_DIRS}
    ${CUDA_INCLUDE_DIRS})

  link_directories(/usr/local/cuda/lib64)

  # GPU executable
  add_executable(grabImgWithTrigger src/grab_trigger.cpp)
  ament_target_dependencies(grabImgWithTrigger
    rclcpp cv_bridge image_transport sensor_msgs std_msgs)
  target_link_libraries(grabImgWithTrigger
    ${OpenCV_LIBS}
    MvCameraControl
    cuda cudart ${CUDA_LIBRARIES}
    jetson-utils
    YoloV8_TRT tensorrt_cpp_api
    nvinfer nvinfer_plugin nvonnxparser
    opencv_cudaimgproc opencv_cudaarithm)

  set(GPU_TARGETS grabImgWithTrigger YoloV8_TRT tensorrt_cpp_api)

# ---------------------------------------------------------------------------
# CPU MODE — no GPU, CUDA, TensorRT, or jetson-utils
# ---------------------------------------------------------------------------
else()
  message(STATUS "=== CPU-ONLY MODE: OpenCV CPU + MVS SDK only ===")

  # Force system OpenCV (not any GPU OpenCV from old workspaces)
  set(OpenCV_DIR "/usr/lib/x86_64-linux-gnu/cmake/opencv4")
  find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
  message(STATUS "OpenCV version: ${OpenCV_VERSION} (system)")

  # CPU executable
  add_executable(grabImgWithTriggerCPU src/grab_trigger_cpu.cpp)
  ament_target_dependencies(grabImgWithTriggerCPU
    rclcpp cv_bridge image_transport sensor_msgs std_msgs)
  target_link_libraries(grabImgWithTriggerCPU
    ${OpenCV_LIBS}
    MvCameraControl)

  # CPU executable with live-tunable ROS2 parameters
  add_executable(test_CPU_grab src/test_CPU_grab.cpp)
  ament_target_dependencies(test_CPU_grab
    rclcpp cv_bridge image_transport sensor_msgs std_msgs)
  target_link_libraries(test_CPU_grab
    ${OpenCV_LIBS}
    MvCameraControl)

  set(CPU_TARGETS grabImgWithTriggerCPU test_CPU_grab)
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
if(NOT BUILD_CPU_ONLY)
  install(TARGETS ${GPU_TARGETS}
    RUNTIME DESTINATION lib/${PROJECT_NAME}
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)

  install(DIRECTORY models/
    DESTINATION share/${PROJECT_NAME}/models
    FILES_MATCHING PATTERN "*.onnx" PATTERN "*.engine*" PATTERN "*.pt")
else()
  install(TARGETS ${CPU_TARGETS}
    RUNTIME DESTINATION lib/${PROJECT_NAME})
endif()

# Common data files
install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
  FILES_MATCHING PATTERN "*.py")

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config)

install(DIRECTORY rviz_cfg/
  DESTINATION share/${PROJECT_NAME}/rviz_cfg)

ament_package()
