# MVS ROS2 Driver — User Manual

## Quick Start

```bash
# Every terminal needs these 3 lines first:
source /opt/ros/humble/setup.bash
source install/setup.bash
export LD_LIBRARY_PATH="/opt/MVS/bin:/opt/MVS/lib/64:/opt/MVS/lib/32:$HOME/.local/lib:$LD_LIBRARY_PATH"

# Launch a single camera (CPU mode):
ros2 launch mvs_ros_driver test_cpu.launch.py

# In another terminal, view the feed:
python3 view_image.py
```

---

## 1. Environment Setup

These must run in **every new terminal** before using the driver.

### One-time (add to `~/.bashrc`)

```bash
echo 'export LD_LIBRARY_PATH="/opt/MVS/bin:/opt/MVS/lib/64:/opt/MVS/lib/32:$HOME/.local/lib:$LD_LIBRARY_PATH"' >> ~/.bashrc
```

### Per-terminal (run each time)

```bash
source /opt/ros/humble/setup.bash
source ~/Documents/intern/risksis-intern/BD/camera/ROS_driver/install/setup.bash
```

---

## 2. Building

### CPU mode (this machine, no GPU)

```bash
cd ~/Documents/intern/risksis-intern/BD/camera/ROS_driver
rm -rf build install log
source /opt/ros/humble/setup.bash
colcon build --packages-select mvs_ros_driver --cmake-args -DBUILD_CPU_ONLY=ON
```

### GPU mode (kodifly machine, RTX 3080)

```bash
cd <workspace>
rm -rf build install log
source /opt/ros/humble/setup.bash
colcon build --packages-select mvs_ros_driver
```

---

## 3. Camera Config Files

All configs are YAML files in `mvs_ros_driver/config/`. Each controls one camera instance.

### Key parameters

| Parameter | Meaning | Example |
|---|---|---|
| `SerialNumber` | Camera serial to open (required if multiple cameras) | `"DB1597646"` |
| `TopicName` | ROS topic the images are published on | `"DB1597646/image"` |
| `TriggerEnable` | `0` = free-run, `1` = hardware trigger | `0` |
| `ExposureAutoMode` | `0` = manual, `1` = once, `2` = continuous | `2` |
| `ExposureTime` | Manual exposure in µs (when AutoMode = 0) | `3000` |
| `AutoExposureTimeLower` | Min auto exposure (µs) | `100` |
| `AutoExposureTimeUpper` | Max auto exposure (µs) | `5000` |
| `image_scale` | Resize factor: `1.0` = full, `0.5` = half, `0.25` = quarter | `0.5` |
| `GainAuto` | `0` = manual, `1` = once, `2` = continuous | `2` |
| `Gain` | Manual gain (0–17.0) | `15.0` |
| `Gamma` | Gamma correction value | `0.7` |
| `GammaSelector` | `0` = user, `1` = sRGB | `1` |
| `Brightness` | Brightness level | `80` |
| `PixelFormat` | `0`=RGB8, `1`=BayerRG8, `2`=BayerRG12Packed, `3`=BayerGB12Packed, `4`=BayerGB8, `5`=BayerGR8 | `5` |
| `CameraMatrix` | 3×3 intrinsic matrix (9 values) | calibration data |
| `DistCoeffs` | Distortion coefficients (up to 8 values) | calibration data |

### Example: single camera free-run config

```yaml
SerialNumber: "DB1597646"
TopicName: "DB1597646/image"
TriggerEnable: 0
ExposureAutoMode: 2
AutoExposureTimeLower: 100
AutoExposureTimeUpper: 5000
image_scale: 0.5
GainAuto: 2
PixelFormat: 5
CameraMatrix: [1,0,0, 0,1,0, 0,0,1]
DistCoeffs: [0,0,0,0, 0,0,0,0]
```

---

## 4. Launching

### Available launch files

| Launch file | Cameras | Mode |
|---|---|---|
| `test_cpu.launch.py` | 1 camera (DB1597646) | CPU, free-run |
| `hikrobot.launch.py` | 1 camera (DA5324655) | GPU, trigger |
| `mvs_camera_trigger.launch.py` | 1 camera (left) | GPU, trigger |
| `mvs_multiple_camera.launch.py` | 2 cameras (left/right) | GPU, trigger |
| `mvs_multi_cam.launch.py` | 6 cameras | GPU, trigger |

### Custom camera

Create your own launch file or run directly:

```bash
ros2 run mvs_ros_driver grabImgWithTriggerCPU \
  $(ros2 pkg prefix mvs_ros_driver)/share/mvs_ros_driver/config/my_camera.yaml
```

---

## 5. Viewing Images

### Python viewer (included)

```bash
python3 view_image.py                          # default topic /DB1597646/image
python3 view_image.py /DA5324655/image         # specific topic
```

Controls: press `q` or `Ctrl+C` to quit. Adjust window size by editing the `scale` value (line 21 of `view_image.py`).

### Command-line check (no GUI)

```bash
ros2 topic hz /DB1597646/image            # frame rate
ros2 topic echo /DB1597646/image --no-arr --once  # one message header
ros2 topic list                            # all active topics
```

### RViz2 (GUI)

```bash
QT_QPA_PLATFORM=xcb rviz2
```

Then: Add → By topic → select `/DB1597646/image` → OK.

---

## 6. Multi-Camera Setup

Each camera needs its own config file and node instance.

### Example: 2 cameras

**config/cam_a.yaml:**
```yaml
SerialNumber: "DA4930148"
TopicName: "cam_a/image"
TriggerEnable: 0
...
```

**config/cam_b.yaml:**
```yaml
SerialNumber: "DA5148680"
TopicName: "cam_b/image"
TriggerEnable: 0
...
```

**launch file:**
```python
from launch import LaunchDescription
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch.substitutions import PathJoinSubstitution

def generate_launch_description():
    pkg = FindPackageShare('mvs_ros_driver')
    cfg = lambda f: [PathJoinSubstitution([pkg, 'config', f])]
    return LaunchDescription([
        Node(package='mvs_ros_driver', executable='grabImgWithTriggerCPU',
             name='cam_a', arguments=cfg('cam_a.yaml'), output='screen'),
        Node(package='mvs_ros_driver', executable='grabImgWithTriggerCPU',
             name='cam_b', arguments=cfg('cam_b.yaml'), output='screen'),
    ])
```

---

## 7. Troubleshooting

### Build Issues

| Symptom | Fix |
|---|---|
| `colcon: build directory was created by catkin_make` | `rm -rf build devel` |
| `colcon: Duplicate package names` — mvs_ros_driver found in both root and `src/` | `rm -rf src/mvs_ros_driver` (old ROS1 copy) |
| `colcon build` fails with OpenCV 4.11 errors | Old `isds_ws` is polluting PATH — run: `unset CMAKE_PREFIX_PATH PKG_CONFIG_PATH PYTHONPATH` then rebuild |
| Binary links against `libopencv_imgcodecs.so.411` but system has 4.5 | Rebuild with explicit system OpenCV: `colcon build --cmake-args -DBUILD_CPU_ONLY=ON -DOpenCV_DIR=/usr/lib/x86_64-linux-gnu/cmake/opencv4 --cmake-clean-first` |
| System OpenCV not found during CPU build | Install it: `sudo apt-get install -y libopencv-dev` |
| `ament_cmake` or ROS2 packages not found | Run `source /opt/ros/humble/setup.bash` before building |
| Build picks up old workspace's libs | Check `CMAKE_PREFIX_PATH` — unset it if it contains old workspace paths: `unset CMAKE_PREFIX_PATH` |

### Runtime Issues

| Symptom | Fix |
|---|---|
| `Package 'mvs_ros_driver' not found` | Run `source install/setup.bash` in the workspace root |
| `libMvCameraControl.so: cannot open shared object file` | Run: `export LD_LIBRARY_PATH="/opt/MVS/bin:/opt/MVS/lib/64:/opt/MVS/lib/32:$LD_LIBRARY_PATH"` |
| `libboost_regex.so.1.71.0: cannot open` | Already symlinked in `~/.local/lib`, but verify: `ls ~/.local/lib/libboost_regex.so.1.71.0`. If missing: `ln -sf /usr/lib/x86_64-linux-gnu/libboost_regex.so.1.74.0 ~/.local/lib/libboost_regex.so.1.71.0` |
| `libopencv_imgcodecs.so.411: cannot open` at runtime | Old workspace libs interfering. Either: (a) `mv ~/Documents/intern/isds_ws/install/lib/libcv_bridge.so ~/Documents/intern/isds_ws/install/lib/libcv_bridge.so.bak`, or (b) rebuild with `unset CMAKE_PREFIX_PATH` |
| `MV_E_ACCESS_DENIED` or camera not found | `sudo /opt/MVS/bin/set_usb_priority.sh --add=2bdf` then unplug/replug the camera |
| `Multiple devices found, but no SerialNumber` | Add `SerialNumber` to your config YAML matching your camera |
| Segfault on startup (exit code -11) | Old workspace `libcv_bridge.so` or `libtensorrt_cpp_api.so` being loaded instead of ROS2 versions. Rename the old ones: `mv ~/Documents/intern/isds_ws/install/lib/libcv_bridge.so ~/Documents/intern/isds_ws/install/lib/libcv_bridge.so.bak` |
| ROS2 `ros2` CLI breaks after setting `LD_LIBRARY_PATH` | Don't `unset` then `export` — always prepend to the existing value. Use: `export LD_LIBRARY_PATH="/opt/MVS/bin:...:$LD_LIBRARY_PATH"` |

### Viewing / GUI Issues

| Symptom | Fix |
|---|---|
| `rqt_image_view` not installed | `sudo apt-get install -y ros-humble-rqt-image-view` |
| RViz2: `Could not find Qt platform plugin "xcb"` | Run: `QT_QPA_PLATFORM=xcb rviz2` |
| `rqt_image_view` also has Qt error | Same fix: `QT_QPA_PLATFORM=xcb rqt_image_view` |
| `cv2.imshow` window too large | Edit `view_image.py` line 21 — change `1280` to `640` or `480` |

### System Issues

| Symptom | Fix |
|---|---|
| `apt update` / `apt install` GPG key conflict | Two ROS sources exist. Run: `sudo rm -f /etc/apt/sources.list.d/ros2.list && sudo apt-get update` |
| `rosdep` not initialized | `sudo rosdep init && rosdep update` |
| `colcon` not found | `sudo apt-get install -y python3-colcon-common-extensions` |

---

## 8. File Reference

```
ROS_driver/
├── install_deps.sh              # Dependency installer
├── view_image.py                # Image viewer script
├── mvs_ros_driver/
│   ├── CMakeLists.txt           # Build config (CPU/GPU mode)
│   ├── package.xml              # ROS2 package manifest
│   ├── config/                  # Camera YAML configs
│   ├── launch/                  # .launch.py files
│   ├── src/
│   │   ├── grab_trigger.cpp     # GPU driver (CUDA + YOLOv8)
│   │   └── grab_trigger_cpu.cpp # CPU driver (OpenCV only)
│   ├── include/yolov8.h         # YOLOv8 header
│   ├── models/                  # ONNX / TensorRT engine files
│   └── rviz_cfg/                # RViz config
└── .claude/
    ├── README.md                # Session history & decisions
    └── USER_MANUAL.md           # This file
```

---

## 9. CPU vs GPU Mode

| Feature | CPU (`grabImgWithTriggerCPU`) | GPU (`grabImgWithTrigger`) |
|---|---|---|
| Debayering | OpenCV CPU | CUDA (`cudaBayerToRGB`) |
| Undistortion | OpenCV CPU `remap` | CUDA (`cv::cuda::remap`) |
| YOLOv8 PII blur | No | Yes (TensorRT) |
| Dependencies | ROS2 + OpenCV + MVS SDK | + CUDA 12 + TensorRT + jetson-utils |
| Build flag | `-DBUILD_CPU_ONLY=ON` | (none, default) |
| Use case | Testing, single cam, no GPU | Production, multi-cam, PII masking |
