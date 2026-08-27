# MVS ROS2 Driver — Setup & User Manual

This is the **from-scratch** guide: take a brand-new Linux machine from nothing to a
running Hikrobot MVS camera in ROS2, then tune it and scale to multiple cameras. The
original `USER_MANUAL.md` assumes ROS2 and the MVS SDK are already installed; this one
starts before either and also documents the newer CPU features (live tuning, multi-camera).

> **Shortcut:** every setup step below is automated by `./install_deps.sh` (see
> [Fast path](#fast-path--installdepssh)). Read this manual when you want to understand
> each step, install a component by hand, or debug why the script failed.

---

## 0. What you are setting up

- **Package:** `mvs_ros_driver` — a ROS2 driver for Hikrobot MVS machine-vision cameras
  (USB3, vendor `0x2bdf`).
- **Three executables:**

  | Executable | Mode | Live tuning | Use for |
  |---|---|---|---|
  | `grabImgWithTriggerCPU` | CPU | ❌ (config applied once at startup) | plain single/multi-camera, lowest overhead |
  | `test_CPU_grab` | CPU | ✅ (ROS2 dynamic parameters) | dialing in exposure/gain/gamma live, per-camera tuning |
  | `grabImgWithTrigger` | GPU | ❌ | production: CUDA debayer + YOLOv8 PII blur (TensorRT) |

- **Two build modes** (`-DBUILD_CPU_ONLY`), which select the CPU vs GPU executables:

  | Mode | Builds | Dependencies |
  |---|---|---|
  | **CPU-only** | `grabImgWithTriggerCPU`, `test_CPU_grab` | ROS2 + system OpenCV + MVS SDK |
  | **GPU** | `grabImgWithTrigger` | + CUDA 12.1 + TensorRT + jetson-utils + CUDA OpenCV |

- **Target OS:** Ubuntu 22.04 (matches ROS2 Humble).

Pick **CPU-only** to validate the pipeline and tune images on any machine; pick **GPU** for
the production multi-camera / PII-masking deployment.

---

## 1. Install Ubuntu 22.04

Install Ubuntu 22.04 (Desktop or Server). Confirm:

```bash
lsb_release -a        # expect "jammy" / 22.04
uname -m              # x86_64 (or aarch64 on Jetson)
```

---

## 2. Install ROS2 Humble

```bash
# 2.1 Locale
sudo apt update && sudo apt install -y locales
sudo locale-gen en_US en_US.UTF-8
sudo update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8
export LANG=en_US.UTF-8

# 2.2 Repositories
sudo apt install -y software-properties-common
sudo add-apt-repository universe
sudo apt update && sudo apt install -y curl gnupg lsb-release

# 2.3 ROS2 apt key + source
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
  http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
  | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null
sudo apt update

# 2.4 Install (base install is enough; desktop adds RViz2)
sudo apt install -y ros-humble-ros-base
```

**Verify:**
```bash
source /opt/ros/humble/setup.bash
ros2 --help           # should print the CLI help
```

---

## 3. Install build tools + rosdep

```bash
sudo apt install -y \
  build-essential cmake git wget pkg-config \
  python3-pip python3-colcon-common-extensions \
  python3-rosdep python3-vcstool \
  libpcl-dev libopencv-dev

sudo rosdep init          # OK if it says "already initialized"
rosdep update
```

> `libpcl-dev` and `libopencv-dev` are needed by the CPU build (`OpenCV` is a hard
> requirement of both modes; `PCL` is required only in GPU mode).

---

## 4. Install the MVS SDK (Hikrobot)

This is the **one dependency that cannot be installed from apt**. It comes from Hikrobot.

1. Download the **MVS** ("Machine Vision Software") Linux SDK from the Hikrobot website
   (register at their portal; pick the Linux x86_64 build).
2. Unpack and run the installer **with sudo** so it installs to its default location
   `/opt/MVS`:

   ```bash
   sudo bash ./MVS-*.run          # or the unpacked install script; follow prompts
   ```

3. Confirm the layout the build expects:
   ```bash
   ls /opt/MVS/include/MvCameraControl.h    # headers
   ls /opt/MVS/lib/64/libMvCameraControl.so # x86_64 library
   ls /opt/MVS/bin/set_usb_priority.sh       # USB permission script
   ```

> The `CMakeLists.txt` hard-codes `/opt/MVS/include` and `/opt/MVS/lib/64` (and
> `/opt/MVS/lib/aarch64` on Jetson), so the SDK **must** live at `/opt/MVS`.

---

## 5. Grant USB access (udev rules)

Hikrobot USB cameras are owned by root by default; the SDK returns
`MV_E_ACCESS_DENIED (0x80000203)` until you install their udev rule:

```bash
sudo /opt/MVS/bin/set_usb_priority.sh --add=2bdf
```

Then **unplug and replug** the camera (or reboot).

**Verify** the device is visible:
```bash
lsusb | grep -i 2bdf     # should list the camera
```

---

## 6. Get the code (clone + submodules)

The workspace is the `ROS_driver/` folder, which contains the `mvs_ros_driver` package
**as a git submodule** (with its own nested submodules for the TensorRT libraries).

```bash
git clone <this-repo-url> risksis-intern
cd risksis-intern
git submodule update --init --recursive     # pulls mvs_ros_driver + libs/tensorrt-cpp-api + libs/YOLOv8-TensorRT-CPP

cd BD/camera/ROS_driver                     # <- workspace root
```

You should see `mvs_ros_driver/` populated (with `src/`, `launch/`, `config/`, `models/`).

---

## 7. Install the package's ROS2 dependencies

```bash
cd BD/camera/ROS_driver
rosdep install --from-paths mvs_ros_driver --ignore-src -r -y \
  --skip-keys="MvCameraControl jetson-utils tensorrt_cpp_api YoloV8_TRT"
```

The `--skip-keys` list is expected: those are the SDK / GPU libraries that rosdep does
not know about (MVS SDK from §4, the rest from the GPU appendix).

`package.xml` declares the ROS2 deps (`rclcpp`, `cv_bridge`, `image_transport`,
`sensor_msgs`, `std_msgs`); this step installs any that §3 missed.

---

## 8. Build

### CPU-only (recommended first)

```bash
source /opt/ros/humble/setup.bash
rm -rf build install log                      # clean any previous build
colcon build --packages-select mvs_ros_driver \
  --symlink-install --cmake-args -DBUILD_CPU_ONLY=ON
source install/setup.bash
```

### GPU (full pipeline)

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select mvs_ros_driver --symlink-install
```

> **GPU prerequisites first** — see the [GPU appendix](#appendix-a--gpu-mode-setup).
> In particular `CMakeLists.txt` hard-codes a custom CUDA OpenCV at
> `set(OpenCV_DIR "/home/kodifly/libraries/opencv/build")`; on a **new** GPU machine you
> must edit that path to your own CUDA-enabled OpenCV build, and provide `jetson-utils`
> (normally Jetson-only — on x86 you need a port). The GPU build will fail until these
> two are in place.

**Verify the executable exists:**
```bash
ros2 pkg prefix mvs_ros_driver          # prints the install/share path
ls $(ros2 pkg prefix mvs_ros_driver)/lib/mvs_ros_driver/   # grabImgWithTriggerCPU, test_CPU_grab, or grabImgWithTrigger
```

> **⚠ Adding launch/config files requires a rebuild.** With `--symlink-install`, the
> compiled binaries are symlinked (edits to `src/` only need a build when the C++ changes),
> but `launch/` and `config/` are **copied** at build time. A `.launch.py` or `.yaml` added
> after the last build will not be found by `ros2 launch` / the node until you re-run
> `colcon build`. (A camera that "doesn't show up on `ros2 topic list`" is very often this —
> its config wasn't installed yet.)

---

## 9. Environment (~/.bashrc)

Add these so every new terminal is ready:

```bash
# ROS2 Humble
source /opt/ros/humble/setup.bash

# MVS SDK runtime libraries
export LD_LIBRARY_PATH="/opt/MVS/bin:/opt/MVS/lib/64:/opt/MVS/lib/32:$LD_LIBRARY_PATH"
```

```bash
nano ~/.bashrc     # paste the two blocks above, save
source ~/.bashrc
```

> In each terminal you use the driver, also run `source <workspace>/install/setup.bash`
> (or add it to `.bashrc` after building).

---

## 10. Find your camera and write a config

### 10.1 Discover the serial number

```bash
./find_camera        # if the prebuilt binary runs (lists serial, model, interface)
```

If it doesn't run on the new machine, rebuild it from source:

```bash
g++ -o find_camera find_camera.cpp -I/opt/MVS/include \
  -Wl,-rpath=/opt/MVS/lib/64:/opt/MVS/bin \
  -L/opt/MVS/lib/64 -L/opt/MVS/bin -lMvCameraControl -lpthread
```

(You can also read the serial from the Hikrobot MVS GUI, or the label on the camera.)

### 10.2 Create a config

Copy an existing config and edit `SerialNumber` + `TopicName`:

```bash
cp mvs_ros_driver/config/DB1597646.yaml mvs_ros_driver/config/my_camera.yaml
```

Key fields:

| Parameter | Meaning |
|---|---|
| `SerialNumber` | camera serial to open (required when >1 camera is attached) |
| `TopicName` | ROS2 topic the images are published on |
| `TriggerEnable` | `0` free-run, `1` hardware trigger (use `0` for a single camera) |
| `ExposureAutoMode` | `0` manual, `1` once, `2` continuous |
| `ExposureTime` | manual exposure in µs (only effective when `ExposureAutoMode = 0`) |
| `GainAuto` | `0` manual, `1` once, `2` continuous |
| `Gain` | analog gain (only effective when `GainAuto = 0`) |
| `Gamma` / `GammaSelector` | gamma value / selector |
| `Brightness` | brightness offset |
| `image_scale` | `1.0` full / `0.5` half / `0.25` quarter |
| `PixelFormat` | `0`=RGB8, `1`=BayerRG8, … `5`=BayerGR8 (typical for these cameras) |
| `CameraMatrix` / `DistCoeffs` | intrinsics / distortion (identity = no undistortion) |

> These are **runtime** settings — changing a value in the YAML never requires a
> recompile (only a relaunch, or a live `ros2 param set` on the tunable node). What
> *is* compile-time: the debayer pattern (`cv::COLOR_BayerGB2RGB`), the undistort output
> size (`4096×2460`), and a few forced SDK flags, all hard-coded in the C++ source.

---

## 11. Run a single camera

| Launch file | Executable | Notes |
|---|---|---|
| `test_cpu.launch.py` | `grabImgWithTriggerCPU` | plain CPU, config `DB1597646.yaml` |
| `test_CPU_grab.launch.py` | `test_CPU_grab` | CPU with live tuning (§12), config `DB1597646.yaml` |
| `hikrobot.launch.py` | `grabImgWithTrigger` | GPU, config `DA5324655.yaml` |

```bash
ros2 launch mvs_ros_driver test_cpu.launch.py        # plain CPU
ros2 launch mvs_ros_driver test_CPU_grab.launch.py   # live-tunable CPU (§12)
ros2 launch mvs_ros_driver hikrobot.launch.py        # GPU
```

### Where the image is

The node publishes on the topic named by `TopicName` in its config — for `DB1597646.yaml`
that is **`/DB1597646/image`** (encoding `rgb8`, already debayered and undistorted).

```bash
# Confirm it is flowing (headless):
ros2 topic list                        # expect /DB1597646/image
ros2 topic hz /DB1597646/image         # live frame rate

# View the live feed:
python3 view_image.py                  # defaults to /DB1597646/image
python3 view_image.py /DB1597646/image # explicit topic
```

Or use `rqt_image_view`, or RViz2 (`QT_QPA_PLATFORM=xcb rviz2` → Add → By topic → the
topic). The ROS node only **publishes**; it does not write any image files to disk. To save
frames, use the standalone `grab_frame` tool or `ros2 bag record -o my_bag /DB1597646/image`.

---

## 12. Live tuning (`test_CPU_grab`)

`test_CPU_grab` runs the exact same pipeline as `grabImgWithTriggerCPU` (grab → CPU
debayer → undistort → resize → publish), **plus** ROS2 dynamic parameters so the image
knobs can be tuned **while the node is running — no restart, no recompile**. Dial in
exposure/gain/gamma live and watch the result in the viewer in real time.

### 12.1 Launch (single camera)

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch mvs_ros_driver test_CPU_grab.launch.py
```

The launch file uses `config/DB1597646.yaml` and names the node `test_CPU_grab`. Edit the
launch file (or the config's `SerialNumber`) if your camera has a different serial.

### 12.2 Tunable parameters

All of these are declared at startup (defaults read from the config YAML) and can be changed
live. `ros2 param set` writes straight through to the MVS SDK on the next frame.

```bash
ros2 param list /test_CPU_grab        # see all parameters + current values
ros2 param get  /test_CPU_grab ExposureTime
ros2 param set  /test_CPU_grab ExposureTime 1500
```

| Parameter | Type | Meaning | Example |
|---|---|---|---|
| `ExposureAutoMode` | int | `0` manual (off) · `1` once · `2` continuous | `2` |
| `ExposureTime` | int (µs) | manual exposure — only effective when `ExposureAutoMode = 0` | `1500` |
| `AutoExposureTimeLower` | int (µs) | lower bound when in auto-exposure | `50` |
| `AutoExposureTimeUpper` | int (µs) | upper bound when in auto-exposure | `200000` |
| `GainAuto` | int | `0` manual · `1` once · `2` continuous | `0` |
| `Gain` | double (dB) | analog gain — only effective when `GainAuto = 0` | `12.0` |
| `AutoGainLowerLimit` | double | lower gain bound when `GainAuto = 2` | `0.0` |
| `AutoGainUpperLimit` | double | upper gain bound when `GainAuto = 2` | `20.0` |
| `GammaSelector` | int | gamma channel selector | `0` |
| `Gamma` | double | gamma value | `0.7` |
| `Brightness` | int | image brightness offset | `80` |
| `image_scale` | double | output resize factor (`1.0` full · `0.5` half · `0.25` quarter) | `0.5` |

Examples:

```bash
# switch to manual exposure and set 1.5 ms
ros2 param set /test_CPU_grab ExposureAutoMode 0
ros2 param set /test_CPU_grab ExposureTime 1500

# manual gain
ros2 param set /test_CPU_grab GainAuto 0
ros2 param set /test_CPU_grab Gain 12.0

# darken / brighten, then drop resolution to halve CPU load
ros2 param set /test_CPU_grab Gamma 0.7
ros2 param set /test_CPU_grab Brightness 80
ros2 param set /test_CPU_grab image_scale 0.5
```

Notes:

- `ExposureTime` only has an effect while `ExposureAutoMode = 0`; `Gain` only while
  `GainAuto = 0`. Change the `*Auto` knob first, then the value.
- `image_scale` clamps to `1.0` if you set it below `0.1`; it changes the published image
  size on the next frame.
- Changes apply immediately and are lost on exit — to make a tuned value **permanent**,
  write it into the config YAML (same parameter names) and relaunch.
- Parameters not in this table (e.g. `DB1597646.image.enable_pub_plugins`) belong to
  `image_transport` and are intentionally ignored by the tuning callback.

---

## 13. Multi-camera

Running more than one camera is just "one ROS2 node per camera", where each node needs
**three things to be unique**:

| What | Why it must be unique |
|---|---|
| **`SerialNumber`** (config YAML) | the driver opens the correct physical camera. With >1 camera attached it **refuses to start without a serial** (`Multiple cameras — need SerialNumber in config!`). |
| **`TopicName`** (config YAML) | each camera publishes on its own topic (e.g. `left_camera/image` vs `right_camera/image`). |
| **node `name`** (launch file) | ROS2 requires distinct node names; it is also the prefix for that node's parameters. |

The one-time discovery step is the same as §10.1 — run `./find_camera` and write down every
serial, or read the labels on the cameras.

### 13.1 Free-run vs hardware trigger

- **Free-run (`TriggerEnable: 0`)** — each camera grabs as fast as it can, independently.
  Simplest to set up; frames are **not** synchronized between cameras. Fine for
  non-synchronized multi-camera work.
- **Hardware trigger (`TriggerEnable: 1`)** — the cameras are synced by wiring their trigger
  lines (`TriggerSource = LINE0`, hardcoded in the driver). One camera (or an external
  signal generator) acts as master and pulses LINE0, which is daisy-chained to the others.
  Use this when frames must be captured at the same instant (e.g. a stereo pair). The
  GPU multi-camera configs (`left_camera_trigger.yaml`, the `DA*.yaml` files) ship with
  `TriggerEnable: 1`.

> **USB3 bandwidth:** several high-res cameras on one host can saturate the USB3 bus and
> drop frames. The 6-camera configs therefore use `image_scale: 0.25` and auto-exposure.
> If you see dropped/failed frames at full resolution, lower `image_scale` (or use a lower
> `PixelFormat`) per camera.

### 13.2 Step by step (N cameras)

1. **Discover serials** — `./find_camera` (or the MVS GUI). Note each serial.
2. **One config per camera** — copy an existing config and set `SerialNumber` + `TopicName`:
   ```bash
   cd mvs_ros_driver/config
   cp DB1597646.yaml cam1.yaml        # repeat for cam2, cam3, ...
   # edit SerialNumber: "<serial1>"   and   TopicName: "cam1/image"
   ```
   Set `TriggerEnable` per §13.1, and pick a sensible `image_scale` for the bus.
3. **One `Node` per camera in a launch file** — see §13.4.
4. **Build** (launch/config files are copy-installed — see §8).
5. **Launch** — `ros2 launch mvs_ros_driver <your-file>.launch.py`.
6. **Verify** — every topic should be present and flowing:
   ```bash
   ros2 topic list                      # expect cam1/image, cam2/image, ...
   ros2 topic hz /cam1/image
   python3 view_image.py /cam1/image    # view each feed
   ```

### 13.3 Live per-camera tuning

Launch one `test_CPU_grab` instance per camera (distinct `name=` → distinct parameter
prefix), and tune each independently at runtime:

```bash
ros2 launch mvs_ros_driver my_two_cam_tunable.launch.py
```

```bash
# camera 1 — fixed 3 ms exposure
ros2 param set /DB1597646 ExposureAutoMode 0
ros2 param set /DB1597646 ExposureTime 3000

# camera 2 — fixed 10 ms exposure (independent!)
ros2 param set /DB1597645 ExposureAutoMode 0
ros2 param set /DB1597645 ExposureTime 10000

ros2 param set /DB1597646 Gain 12.0
ros2 param set /DB1597645 Gamma 0.7
```

Each node exposes the full §12.2 parameter set under its own prefix
(`/DB1597646`, `/DB1597645`), so no setting bleeds across cameras.

### 13.4 Minimal two-camera launch file (CPU)

The ready-made `my_two_cam.launch.py` drives `DB1597646.yaml` + `DB1597645.yaml` free-run
via `grabImgWithTriggerCPU`. The template below is what it looks like — copy and edit for
your own camera set:

```python
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare

def generate_launch_description():
    pkg_share = FindPackageShare('mvs_ros_driver')
    return LaunchDescription([
        Node(package='mvs_ros_driver',
             executable='grabImgWithTriggerCPU',      # or 'test_CPU_grab' for live tuning
             name='cam1',
             arguments=[PathJoinSubstitution([pkg_share, 'config', 'cam1.yaml'])],
             output='screen'),
        Node(package='mvs_ros_driver',
             executable='grabImgWithTriggerCPU',
             name='cam2',
             arguments=[PathJoinSubstitution([pkg_share, 'config', 'cam2.yaml'])],
             output='screen'),
    ])
```

For the GPU build, replace `grabImgWithTriggerCPU` with `grabImgWithTrigger` (and set
`TriggerEnable: 1` + wire LINE0 if you need sync).

> `grabImgWithTriggerCPU` and `test_CPU_grab` both take their node name from the launch
> `name=` (the launch name overrides the constructor's default and becomes the parameter
> prefix, e.g. `/cam1`). Use `grabImgWithTriggerCPU` for plain multi-camera running;
> use `test_CPU_grab` when you want live tuning on each camera.

### 13.5 Ready-made launch files

| Launch file | Cameras | Executable | Mode |
|---|---|---|---|
| `test_cpu.launch.py` | 1 | `grabImgWithTriggerCPU` | CPU |
| `test_CPU_grab.launch.py` | 1 | `test_CPU_grab` | CPU (live tuning) |
| `hikrobot.launch.py` | 1 | `grabImgWithTrigger` | GPU |
| `my_two_cam.launch.py` | 2 (DB1597646 + DB1597645, free-run) | `grabImgWithTriggerCPU` | CPU |
| `my_two_cam_tunable.launch.py` | 2 (DB1597646 + DB1597645, free-run) | `test_CPU_grab` | CPU (live tuning) |
| `mvs_multiple_camera.launch.py` | 2 (left/right stereo pair, triggered) | `grabImgWithTrigger` | GPU |
| `mvs_multi_cam.launch.py` | 6 (full array, triggered) | `grabImgWithTrigger` | GPU |

To adapt one for your own cameras, copy it and edit the `name=` / config paths (and
`executable=` if you are switching CPU↔GPU). Remember to update the `SerialNumber` in each
referenced config to match your physical cameras — the shipped serials will not match a new
set of hardware.

---

## Fast path (`install_deps.sh`)

The workspace ships an idempotent installer that performs §2–§9 automatically:

```bash
./install_deps.sh              # CPU: system tools + ROS2 Humble + ROS2 deps + udev + rosdep
./install_deps.sh --build      # the above, then colcon build (CPU)
./install_deps.sh --gpu        # add CUDA / TensorRT / jetson-utils (GPU machine)
./install_deps.sh --gpu --build
```

It **cannot** install the MVS SDK (§4) — that still has to be done by hand first. It
detects `/opt/MVS` and sets up the udev rule if the SDK is present.

---

## Appendix A — GPU-mode setup

Only for the `grabImgWithTrigger` (CUDA + YOLOv8) build. All of this is in addition to
§2–§5.

1. **NVIDIA driver + CUDA 12.1** (see NVIDIA's install docs or the `--gpu` path of
   `install_deps.sh`).
2. **TensorRT** (`libnvinfer-dev`, `libnvinfer-plugin-dev`, `libnvonnxparsers-dev`,
   `libnvparsers-dev`).
3. **CUDA-enabled OpenCV** — build it yourself and point
   `CMakeLists.txt`'s `OpenCV_DIR` at it (default is `/home/kodifly/libraries/opencv/build`).
4. **jetson-utils** — provides `cudaBayerToRGB`; native on Jetson, needs a port on x86.
5. **Models** — the `models/` dir ships `best.onnx` and pre-built TensorRT engines
   (`*.engine.*fp16.1.1`); the node also accepts `ModelONNXPath` / `ModelTRTPath` in the
   config YAML. Re-export the engine if you change GPU.

Then build without `-DBUILD_CPU_ONLY=ON` (§8).

---

## Troubleshooting (quick reference)

| Symptom | Fix |
|---|---|
| `ros2: command not found` | `source /opt/ros/humble/setup.bash` |
| `Package 'mvs_ros_driver' not found` | `source install/setup.bash` in the workspace |
| `libMvCameraControl.so: cannot open shared object file` | export `LD_LIBRARY_PATH=/opt/MVS/bin:/opt/MVS/lib/64:...` (§9) |
| `MV_E_ACCESS_DENIED` | run `sudo /opt/MVS/bin/set_usb_priority.sh --add=2bdf`, then replug (§5) |
| `Multiple devices found, but no SerialNumber` | set `SerialNumber` in your config YAML |
| Camera missing from `ros2 topic list` / `ros2 launch` can't find a file | new `launch/`/`config/` file wasn't installed — re-run `colcon build` (§8) |
| CPU build can't find OpenCV | `sudo apt install -y libopencv-dev` |
| Build picks up an old workspace's OpenCV/cv_bridge | `unset CMAKE_PREFIX_PATH PKG_CONFIG_PATH PYTHONPATH` then rebuild |
| `libboost_regex.so.1.71.0: cannot open` | `ln -sf /usr/lib/x86_64-linux-gnu/libboost_regex.so.1.74.0 ~/.local/lib/libboost_regex.so.1.71.0` |
| RViz2 Qt "xcb" plugin error | `QT_QPA_PLATFORM=xcb rviz2` |
| `rosdep` not initialized | `sudo rosdep init && rosdep update` |
| `colcon` not found | `sudo apt install -y python3-colcon-common-extensions` |

---

## File reference

```
ROS_driver/                       # <- workspace root (build with colcon here)
├── install_deps.sh               # automated installer (§Fast path)
├── USER_MANUAL.md                # original manual (assumes deps already installed)
├── USER_MANUAL_2.md              # this file
├── view_image.py                 # Python OpenCV image viewer
├── find_camera / find_camera.cpp # enumerate cameras + serials
├── grab_frame.cpp                # standalone frame saver (writes PNGs, no ROS)
└── mvs_ros_driver/               # the ROS2 package (git submodule)
    ├── CMakeLists.txt            # CPU vs GPU mode (-DBUILD_CPU_ONLY=ON)
    ├── package.xml               # ROS2 manifest
    ├── config/                   # per-camera YAML (SerialNumber, exposure, …)
    │   ├── DB1597646.yaml        # single-camera CPU (free-run)
    │   ├── DB1597645.yaml        # second camera, same model
    │   ├── DA*.yaml              # 6-camera GPU array (triggered, calibrated)
    │   └── left/right_camera_trigger.yaml  # stereo pair (triggered)
    ├── launch/                   # *.launch.py files (§11, §13.5)
    ├── src/grab_trigger_cpu.cpp  # CPU driver source
    ├── src/test_CPU_grab.cpp     # CPU driver with live-tunable params (§12)
    ├── src/grab_trigger.cpp      # GPU driver source (CUDA + YOLOv8)
    ├── models/                   # ONNX + TensorRT engines
    └── rviz_cfg/                 # RViz2 layout
```
