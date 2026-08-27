# MVS Camera ROS2 Driver

ROS2 driver for **Hikrobot MVS** machine-vision cameras (USB3, vendor `0x2bdf`),
for Ubuntu 22.04 / ROS2 Humble. Grabs frames from the camera, debayers + undistorts
them on the CPU, and publishes them as a ROS2 image topic.

Works in two build modes:

| Mode | Executable(s) | Notes |
|---|---|---|
| **CPU** (default) | `grabImgWithTriggerCPU`, `test_CPU_grab` | no GPU needed; `test_CPU_grab` adds live tuning |
| **GPU** | `grabImgWithTrigger` | CUDA debayer + YOLOv8 PII blur (TensorRT) |

---

## Folder contents

```
camera_for_BD/
├── mvs_ros_driver/       # the ROS2 package (src, launch, config, CMakeLists)
├── view_image.py         # quick OpenCV viewer (subscribes to an image topic)
├── install_deps.sh       # automated installer (ROS2 + deps + udev + build)
├── find_camera           # lists attached cameras + serials
├── grab_frame(.cpp)      # grabs N frames and saves PNGs (no ROS needed)
├── USER_MANUAL_2.md      # full from-scratch setup + tuning manual
└── USER_MANUAL.md        # original manual (assumes deps already installed)
```

---

## Requirements

- **Ubuntu 22.04** + **ROS2 Humble** (`ros-humble-ros-base`).
- **Hikrobot MVS SDK** installed at `/opt/MVS` (the only non-apt dependency).
- **OpenCV** (`libopencv-dev`).
- USB access: `sudo /opt/MVS/bin/set_usb_priority.sh --add=2bdf` (then replug).

> Full step-by-step install: see **`USER_MANUAL_2.md`**, or run `./install_deps.sh`.

---

## Build (CPU)

```bash
source /opt/ros/humble/setup.bash
cd mvs_ros_driver/..
# build the package (from a workspace containing mvs_ros_driver):
colcon build --packages-select mvs_ros_driver --symlink-install \
  --cmake-args -DBUILD_CPU_ONLY=ON
source install/setup.bash
```

GPU build: drop `-DBUILD_CPU_ONLY=ON` (needs CUDA 12.1 + TensorRT + jetson-utils —
see `USER_MANUAL_2.md` Appendix A).

---

## Run

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash

# single camera — plain driver
ros2 launch mvs_ros_driver test_cpu.launch.py

# single camera — live-tunable (tune exposure/gain/gamma at runtime)
ros2 launch mvs_ros_driver test_CPU_grab.launch.py

# two cameras (DB1597646 + DB1597645)
ros2 launch mvs_ros_driver my_two_cam.launch.py

# two cameras with live per-camera tuning
ros2 launch mvs_ros_driver my_two_cam_tunable.launch.py
```

## View the image

The image is published on the topic named by `TopicName` in the camera's config YAML
(default `DB1597646.yaml` → **`/DB1597646/image`**, encoding `rgb8`).

```bash
python3 view_image.py                 # defaults to /DB1597646/image
python3 view_image.py /DB1597646/image

# headless checks
ros2 topic list                       # list active topics
ros2 topic hz /DB1597646/image        # frame rate
```

Or use `rqt_image_view`, or RViz2 (`QT_QPA_PLATFORM=xcb rviz2` → Add → By topic).

---

## Live tuning

The `test_CPU_grab` node exposes these as ROS2 parameters (change at runtime, no restart):

```bash
ros2 param list /test_CPU_grab
ros2 param set  /test_CPU_grab ExposureAutoMode 0   # 0=manual, 1=once, 2=continuous
ros2 param set  /test_CPU_grab ExposureTime 1500     # µs (only when AutoMode=0)
ros2 param set  /test_CPU_grab Gain 12.0             # dB (only when GainAuto=0)
ros2 param set  /test_CPU_grab Gamma 0.7
ros2 param set  /test_CPU_grab Brightness 80
ros2 param set  /test_CPU_grab image_scale 0.5       # 1.0 full / 0.5 half / 0.25 quarter
```

Full parameter list + per-camera tuning: see `USER_MANUAL_2.md` §12–§13.

---

## Configuring your camera

Each camera has a YAML config in `mvs_ros_driver/config/`. Copy one and set the
`SerialNumber` (find it with `./find_camera`) and `TopicName`:

```bash
cd mvs_ros_driver/config
cp DB1597646.yaml my_camera.yaml     # then edit SerialNumber + TopicName
```

Key fields: `SerialNumber`, `TopicName`, `TriggerEnable` (0=free-run, 1=trigger),
`ExposureAutoMode`/`ExposureTime`, `GainAuto`/`Gain`, `Gamma`, `Brightness`,
`image_scale`, `PixelFormat`.

> These are runtime settings — no recompile needed. The debayer pattern and undistort
> size are compile-time (hard-coded in `mvs_ros_driver/src/*.cpp`).

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `ros2: command not found` | `source /opt/ros/humble/setup.bash` |
| `Package 'mvs_ros_driver' not found` | `source install/setup.bash` |
| `libMvCameraControl.so: cannot open` | `export LD_LIBRARY_PATH=/opt/MVS/bin:/opt/MVS/lib/64:$LD_LIBRARY_PATH` |
| `MV_E_ACCESS_DENIED` | `sudo /opt/MVS/bin/set_usb_priority.sh --add=2bdf`, replug |
| `Multiple cameras — need SerialNumber` | set `SerialNumber` in the config YAML |
| camera missing from `ros2 topic list` | config not installed — re-run `colcon build` |

Full troubleshooting table: `USER_MANUAL_2.md`.
