# Ouster OS0-128 LiDAR + GNSS/RTK — User Manual

## Hardware Overview

| Component | Model | Details |
|---|---|---|
| LiDAR | Ouster OS0-128 | 128 beams, 1024×10 Hz, SN: `122621004523` |
| Interface | Ethernet | Sensor IP: `169.254.155.51`, Dest IP: `169.254.127.42` |
| LiDAR port | UDP | `7502` |
| IMU port | UDP | `7503` |
| ROS version | Humble | Ubuntu 22.04 |

---

## 1. Environment Setup

Every new terminal:

```bash
source /opt/ros/humble/setup.bash
source ~/Documents/intern/risksis-intern/BD/lidar/install/setup.bash
```

---

## 2. Quick Start — Launch the LiDAR

### Primary launcher (production)

```bash
# Full system — sensor + point cloud + range image
ros2 launch ouster_ros sensor.composite.launch.py
```

### Restart script (pre-configured for this sensor)

```bash
cd ~/Documents/intern/risksis-intern/BD/lidar
./restart_ouster_driver.sh
```

This script:
- Kills any existing Ouster driver
- Launches the driver with pre-configured settings (sync pulse in, NMEA UART input, etc.)

### Manual launch with parameters

```bash
ros2 launch ouster_ros driver.launch.py \
  sensor_hostname:=169.254.155.51 \
  udp_dest:=169.254.127.42 \
  lidar_port:=7502 \
  imu_port:=7503 \
  lidar_mode:=1024x10 \
  udp_profile_lidar:=RNG19_RFL8_SIG16_NIR16 \
  timestamp_mode:=TIME_FROM_INTERNAL_OSC \
  viz:=false
```

---

## 3. Key Topics

| Topic | Type | Rate | Description |
|---|---|---|---|
| `/ouster/points` | `sensor_msgs/PointCloud2` | 10 Hz | 3D point cloud (128 rings) |
| `/ouster/imu` | `sensor_msgs/Imu` | 100 Hz | IMU data (accel + gyro) |
| `/ouster/range_image` | `sensor_msgs/Image` | 10 Hz | Range image |
| `/ouster/signal_image` | `sensor_msgs/Image` | 10 Hz | Signal intensity image |
| `/ouster/nearir_image` | `sensor_msgs/Image` | 10 Hz | Near-IR image |
| `/ouster/scan` | `sensor_msgs/LaserScan` | 10 Hz | Single-ring laser scan |

### Check what's publishing

```bash
ros2 topic list
ros2 topic hz /ouster/points
ros2 topic hz /ouster/imu
```

---

## 4. Viewing the Point Cloud

### RViz2

```bash
QT_QPA_PLATFORM=xcb rviz2 -d ~/Documents/intern/risksis-intern/BD/lidar/ouster_rviz_config.rviz
```

### Command-line check (no GUI)

```bash
# Print one point cloud message
python3 ~/Documents/intern/risksis-intern/BD/lidar/print_lidar.py

# Check data rates
ros2 topic hz /ouster/points
```

### Ouster Studio / CLI

```bash
ouster-cli source 169.254.155.51 viz
```

---

## 5. Ouster Driver Parameters

Key parameters in `driver_params.yaml` (full list at `Lidar_ROS_driver/src/ouster-ros/ouster-ros/config/driver_params.yaml`):

### Sensor Connection

| Parameter | Default | Description |
|---|---|---|
| `sensor_hostname` | `''` | Sensor IP (e.g. `169.254.155.51`) |
| `udp_dest` | `''` | Host IP where sensor sends UDP data |
| `lidar_port` | `0` | UDP port for lidar data (`7502`) |
| `imu_port` | `0` | UDP port for IMU data (`7503`) |

### LiDAR Mode

| Parameter | Options | Description |
|---|---|---|
| `lidar_mode` | `512x10`, `512x20`, `1024x10`, `1024x20`, `2048x10`, `4096x5` | Resolution × rate |
| `udp_profile_lidar` | `RNG19_RFL8_SIG16_NIR16` (recommended), `RNG15_RFL8_NIR8`, `LEGACY` | Data packet format |
| `udp_profile_imu` | `ACCEL32_GYRO32_NMEA`, `LEGACY` | IMU packet format |

### Time Synchronization

| Parameter | Options | Description |
|---|---|---|
| `timestamp_mode` | `TIME_FROM_INTERNAL_OSC`, `TIME_FROM_SYNC_PULSE_IN`, `TIME_FROM_PTP_1588`, `TIME_FROM_ROS_TIME` | Timestamp source |
| `multipurpose_io_mode` | `OFF`, `INPUT_NMEA_UART`, `OUTPUT_FROM_INTERNAL_OSC`, `OUTPUT_FROM_SYNC_PULSE_IN`, `OUTPUT_FROM_PTP_1588` | Multipurpose IO pin mode |
| `sync_pulse_in_polarity` | `ACTIVE_HIGH`, `ACTIVE_LOW` | Sync pulse input polarity |
| `sync_pulse_out_polarity` | `ACTIVE_HIGH`, `ACTIVE_LOW` | Sync pulse output polarity |

### Processing

| Parameter | Default | Description |
|---|---|---|
| `proc_mask` | `IMU\|PCL\|SCAN\|IMG\|RAW\|TLM` | Which data types to publish |
| `point_type` | `original` | Point cloud format: `original`, `native`, `xyz`, `xyzi`, `xyzir`, `xyzrgb` |
| `organized` | `true` | Organized point cloud (2D grid) |
| `destagger` | `true` | Correct for sensor rotation during scan |
| `min_range` | `0.0` | Min range (meters) |
| `max_range` | `1000.0` | Max range (meters) |
| `scan_ring` | `0` | Which beam for LaserScan output (0–127) |
| `v_reduction` | `1` | Vertical beam reduction: 1, 2, 4, 8, 16 |

### Frame Names

| Parameter | Default |
|---|---|
| `sensor_frame` | `os_sensor` |
| `lidar_frame` | `os_lidar` |
| `imu_frame` | `os_imu` |
| `point_cloud_frame` | `os_lidar` |
| `pub_static_tf` | `true` |

### NMEA / GPS Input

| Parameter | Options | Description |
|---|---|---|
| `nmea_in_polarity` | `ACTIVE_HIGH`, `ACTIVE_LOW` | NMEA UART polarity |
| `nmea_baud_rate` | `BAUD_9600`, `BAUD_115200` | NMEA baud rate |
| `nmea_ignore_valid_char` | `true`, `false` | Accept invalid $GPRMC |
| `nmea_leap_seconds` | `0` | Leap seconds offset |

---

## 6. GNSS / RTK Subsystem

### NMEA ROS Bridge

Converts NMEA GPS sentences to ROS messages.

**Launch (TCP):**
```bash
ros2 launch nmea_ros_bridge nmea_tcp.launch.py
```

**Launch (UDP):**
```bash
ros2 launch nmea_ros_bridge nmea_udp.launch.py
```

**Config files:**
- `src/nmea_ros_bridge/config/tcp_config.yaml`
- `src/nmea_ros_bridge/config/udp_config.yaml`

### GNSS Compass

Dual-antenna GNSS heading.

```bash
ros2 launch gnss_compass_ros gnss_compass.launch.xml
```

### RTKLIB Bridge

RTK correction data (centimeter-level GPS).

```bash
ros2 launch rtklib_bridge rtklib_bridge.launch.xml
```

---

## 7. Eagleye Localization

Localization engine using LiDAR + IMU + GNSS.

```bash
ros2 launch eagleye_rt eagleye_rt.launch.xml
```

Config at: `src/eagleye/eagleye_rt/config/eagleye_config.yaml`

---

## 8. Sensor Metadata

This sensor's metadata is saved at:

```
~/Documents/intern/risksis-intern/BD/lidar/169.254.155-metadata.json
```

Key info from metadata:

| Field | Value |
|---|---|
| Model | OS-0-128 |
| Serial | `122621004523` |
| FW version | `v3.1.0` |
| LiDAR mode | `1024x10` |
| Columns per frame | 1024 |
| Beams | 128 |
| Beam altitude range | +45° to -45.81° |
| UDP profile | `RNG19_RFL8_SIG16_NIR16` |
| IMU accel FSR | NORMAL |
| IMU gyro FSR | NORMAL |
| Min range threshold | 50 cm |

---

## 9. File Reference

```
lidar/
├── restart_ouster_driver.sh      # Quick restart with pre-configured params
├── show_lidar.sh                 # Launch Ouster CLI visualizer
├── print_lidar.py                # Print point cloud messages to console
├── print_imu.py                  # Print IMU messages to console
├── start_rqt_plot.sh             # Launch rqt_plot for data visualization
├── ouster_rviz_config.rviz       # Pre-configured RViz layout
├── 169.254.155-metadata.json     # Sensor calibration/metadata
├── imu.csv                       # Recorded IMU data
├── install/                      # Built workspace (colcon install)
└── Lidar_ROS_driver/
    ├── config/
    │   └── nmea_tcp_local.yaml
    └── src/
        ├── ouster-ros/ouster-ros/    # Ouster ROS2 driver
        │   ├── config/
        │   │   ├── driver_params.yaml
        │   │   ├── os_sensor_cloud_image_params.yaml
        │   │   ├── community_driver_config.yaml
        │   │   └── metadata-qos-override.yaml
        │   └── launch/
        │       └── sensor.composite.launch.py
        ├── nmea_ros_bridge/          # NMEA GPS bridge
        ├── gnss_compass_ros/         # GNSS compass
        ├── rtklib_ros_bridge/        # RTK corrections
        ├── eagleye/                  # Eagleye localization
        └── llh_converter/            # Coordinate converter
```

---

## 10. Common Operations

### Restart the driver

```bash
cd ~/Documents/intern/risksis-intern/BD/lidar
./restart_ouster_driver.sh
```

### Switch to free-run (no external sync)

```bash
ros2 launch ouster_ros driver.launch.py \
  sensor_hostname:=169.254.155.51 \
  udp_dest:=169.254.127.42 \
  timestamp_mode:=TIME_FROM_INTERNAL_OSC \
  multipurpose_io_mode:=OFF
```

### Record sensor metadata

```bash
ros2 launch ouster_ros driver.launch.py \
  sensor_hostname:=169.254.155.51 \
  metadata:=~/sensor_metadata.json
```

### Record a rosbag

```bash
ros2 bag record -a -o ~/lidar_recording
# or specific topics:
ros2 bag record /ouster/points /ouster/imu -o ~/lidar_data
```

### Change return order (multi-return mode)

Add to launch parameters:
```bash
return_order:=STRONGEST_TO_WEAKEST  # or FARTHEST_TO_NEAREST, NEAREST_TO_FARTHEST
```

---

## 11. Troubleshooting

### Setup & Build

| Symptom | Fix |
|---|---|
| `executable 'os_driver' not found on the libexec directory` | Either: (a) workspace not sourced — run `source install/setup.bash`, or (b) `COLCON_IGNORE` blocking — run `rm -f ~/Documents/intern/risksis-intern/BD/lidar/install/COLCON_IGNORE`, or (c) permissions missing — run `chmod +x install/ouster_ros/lib/ouster_ros/*` |
| `Package 'ouster_ros' not found` | Source the workspace: `source /opt/ros/humble/setup.bash && source ~/Documents/intern/risksis-intern/BD/lidar/install/setup.bash` |
| `ament_index` can't find package even after sourcing | Check for `COLCON_IGNORE` files in the install directory and remove them |
| Build fails with dependency errors | Run `rosdep install --from-paths src --ignore-src -y` first |
| `colcon build` can't find `rclcpp_components` | Install: `sudo apt-get install ros-humble-rclcpp-components` |

### Connection

| Symptom | Fix |
|---|---|
| No data on `/ouster/points` | Check sensor is powered and network reachable: `ping 169.254.155.51` |
| Driver can't connect to sensor | Verify `sensor_hostname` and `udp_dest` are set correctly. The host machine must have the IP `169.254.127.42` on its network interface |
| `udp_dest` IP not on any local interface | Add the IP: `sudo ip addr add 169.254.127.42/16 dev eth0` (replace `eth0` with your interface) |
| Driver exits immediately after starting | Check the sensor is powered on and the Ethernet cable is connected. Try `force_reinit:=true` |
| Sensor not responding after config change | Power-cycle the sensor (unplug/replug) and retry |

### Data Quality

| Symptom | Fix |
|---|---|
| Wrong timestamp | Check `timestamp_mode` — use `TIME_FROM_INTERNAL_OSC` for simple setup, `TIME_FROM_SYNC_PULSE_IN` with external GNSS |
| No IMU data | Ensure `imu_port` is set (7503) and `proc_mask` includes `IMU` |
| Point cloud looks garbled or empty | Check `udp_profile_lidar` matches sensor config. For OS0 with FW 3.1+, use `RNG19_RFL8_SIG16_NIR16` |
| Low point cloud rate | Check `lidar_mode` — `1024x10` = 10 Hz at 1024 columns |
| Only getting partial scans (azimuth window) | Check `azimuth_window_start` and `azimuth_window_end` — default is full 360° |
| Sensor reconfiguration not sticking after reboot | Set `persist_config:=true` then reconfigure once |

### Viewing

| Symptom | Fix |
|---|---|
| RViz can't find xcb plugin | Run: `QT_QPA_PLATFORM=xcb rviz2` |
| RViz2 shows no point cloud | Add display → By topic → `/ouster/points`. Check "Fixed Frame" is `os_lidar` or `os_sensor` |
| `rqt` / `rqt_plot` fails with Qt error | `QT_QPA_PLATFORM=xcb rqt` |
| Ouster Studio / CLI not installed | Install separately from https://ouster.com/downloads |

### Multi-Sensor / GNSS

| Symptom | Fix |
|---|---|
| NMEA bridge not receiving data | Check serial device exists (`ls /dev/ttyACM0`), baud rate matches, and `serial_tcp_bridge.py` is running |
| Eagleye not converging | GNSS needs good sky view; wait 30-60 seconds for initial convergence; check `/eagleye/fix` topic |
| NMEA sentences arriving but LiDAR timestamp still wrong | Check `multipurpose_io_mode` is `INPUT_NMEA_UART` and `nmea_baud_rate` matches the GNSS receiver |

### Persistent Issues

| Symptom | Fix |
|---|---|
| Driver dies after losing connection | Set `attempt_reconnect:=true` and adjust `dormant_period_between_reconnects` |
| Too many reconnect attempts | Set `max_failed_reconnect_attempts` to a reasonable number (default is max int) |
| Multiple ouster_ros workspaces conflict | Remove unused workspace: `rm -rf ~/Documents/intern/ROS_noetic/ouster_humble_ws/build` |
| Old ROS1 workspace interfering | If you have `ouster_SDK/ouster-ros` (ROS1), ensure it's not sourced in your `.bashrc` |
