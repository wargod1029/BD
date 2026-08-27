# Ouster + Eagleye GNSS/IMU Localization Workspace

**Date:** 2026-07-13
**ROS Distro:** Humble
**Workspace:** `/home/risksis/Documents/intern/ROS_noetic/ouster_humble_ws`

---

## 1. Hardware Inventory

| Component | Model | Connection | Status |
|-----------|-------|------------|--------|
| GNSS Receiver | **u-blox ZED-F9P** (HPG 1.40, L1/L5 dual-band) | USB `/dev/ttyACM0` | ✅ Detected, streaming NMEA |
| LiDAR | Ouster (model TBD) | Ethernet | ✅ Driver built |
| IMU | TBD | TBD | ❌ Not yet connected |

### u-blox ZED-F9P Details

- **USB ID:** 1546:01a9
- **Serial symlink:** `/dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00`
- **Firmware:** HPG 1.40 (EXT CORE 1.00)
- **Protocol:** UBX + NMEA
- **Baud rate:** 38400
- **GNSS Constellations:** GPS, GLONASS, Galileo, BeiDou, SBAS, QZSS
- **NMEA message rate:** ~10 Hz aggregate (individual messages at ~1 Hz each)
- **Note:** Currently **no position fix** — receiver is indoors, needs clear sky view

---

## 2. Workspace Setup & Build

### 2.1 Source Packages (in `src/`)

| Package | Branch | Purpose |
|---------|--------|---------|
| `eagleye` | `main-ros2` | Core localization (GNSS + IMU fusion) |
| `rtklib_ros_bridge` | `ros2-v0.1.0` | RTKLIB ↔ ROS bridge |
| `llh_converter` | `ros2` | Geodetic coordinate conversion |
| `nmea_ros_bridge` | `ros2-v0.1.0` | NMEA sentence TCP/UDP bridge |
| `gnss_compass_ros` | `main-ros2` | Dual-antenna GNSS heading |
| `ouster-ros` | (existing) | Ouster LiDAR ROS2 driver |

### 2.2 System Dependencies

```bash
# GeographicLib (for geoid/coordinate conversion)
sudo apt-get install -y libgeographic-dev geographiclib-tools geographiclib-doc
sudo geographiclib-get-geoids best

# GSIGEO geoid model (for Japan)
sudo mkdir -p /usr/share/GSIGEO
sudo cp src/llh_converter/data/gsigeo2011_ver2_1.asc /usr/share/GSIGEO/

# RTKLIB (MapIV fork)
sudo apt-get install -y gfortran
cd $HOME
git clone -b rtklib_ros_bridge_b34 https://github.com/MapIV/RTKLIB.git
cd RTKLIB/lib/iers/gcc/ && make
cd RTKLIB/app/consapp && make
```

### 2.3 Build (Completed ✅)

```bash
source /opt/ros/humble/setup.bash
colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release
```

**Result:** All 17 packages built successfully (warnings only, no errors).

> **Issue fixed during build:** Duplicate packages existed at both workspace root and `src/`. Root-level copies of `eagleye`, `gnss_compass_ros`, `llh_converter`, `nmea_ros_bridge`, and `rtklib_ros_bridge` were removed — source code belongs in `src/`.

---

## 3. GNSS Data Pipeline

### 3.1 Architecture

```
u-blox ZED-F9P                serial_tcp_bridge.py          nmea_ros_bridge              ROS Topics
┌─────────────┐    NMEA      ┌──────────────────┐   TCP    ┌──────────────┐   nmea_msgs   ┌──────────────────┐
│ /dev/ttyACM0 │──────────────│ TCP Server        │──────────│ nmea_tcp      │──────────────│ /nmea_sentence    │
│ 38400 baud   │  serial      │ 0.0.0.0:62002    │ client   │ (Node)        │   /Sentence   │                  │
└─────────────┘              └──────────────────┘          └──────────────┘               └──────────────────┘
```

**Why this architecture:**
- The u-blox connects via USB-serial (`/dev/ttyACM0`), but `nmea_ros_bridge` only supports TCP and UDP — it has no serial driver.
- Solution: `serial_tcp_bridge.py` acts as a serial-to-TCP relay. It reads raw NMEA from the serial port and serves it on a TCP socket.
- `nmea_tcp` connects as a TCP client, parses NMEA `$` sentences, and publishes them as `nmea_msgs/Sentence` on the `/nmea_sentence` ROS topic.
- Eagleye subscribes to this topic for GNSS position and velocity data.

### 3.2 How to Start GNSS Data Flow

**Step 1: Start the serial-to-TCP bridge** (runs at 38400 baud by default)
```bash
python3 serial_tcp_bridge.py &
```

**Step 2: Launch nmea_tcp node**
```bash
source install/setup.bash
ros2 run nmea_ros_bridge nmea_tcp --ros-args --params-file config/nmea_tcp_local.yaml
```

**Step 3: Verify NMEA data is flowing**
```bash
source install/setup.bash
ros2 topic echo /nmea_sentence --field sentence
```

Expected output:
```
$GNGGA,,,,,,0,00,99.99,,,,,,*56
$GNGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99,1*33
$GPGSV,1,1,01,15,,,15,1*65
$GNGLL,,,,,,V,N*7A
...
```

### 3.3 Key Files Created

| File | Purpose |
|------|---------|
| `serial_tcp_bridge.py` | Python script: reads serial u-blox → TCP server on port 62002 |
| `config/nmea_tcp_local.yaml` | nmea_tcp config pointing to `127.0.0.1:62002` |

---

## 4. Current Status & Next Steps

### 4.1 What's Working ✅

- [x] All ROS2 packages cloned and built
- [x] u-blox ZED-F9P detected on USB, outputting NMEA at 9600 baud
- [x] Serial-to-TCP bridge operational
- [x] NMEA sentences publishing on ROS topic `/nmea_sentence`
- [x] RTKLIB built and ready
- [x] GeographicLib + geoid data installed
- [x] Ouster LiDAR driver built

### 4.2 What Needs Attention ⚠️

- **GNSS fix:** Receiver is indoors with no sky view. Move near a window or outdoors. The ZED-F9P needs ~4 satellites with good SNR for a 3D fix.
- **GNSS update rate:** Default NMEA output is ~1 Hz per message type. Eagleye expects 5 Hz. Options:
  - Use **RTKLIB** path (recommended) — RTKLIB talks directly to u-blox in UBX binary protocol, can achieve 5-20 Hz with raw Doppler measurements
  - Reconfigure u-blox via u-center or `ubxtool` to output NMEA at 5 Hz
- **IMU:** No IMU detected yet. Eagleye needs `/imu/data_raw` at 50 Hz.
- **Wheel speed:** No CAN bus or wheel speed sensor detected. Eagleye can operate in CAN-less mode with RTK, but accuracy degrades in poor GNSS conditions.

### 4.3 Next Steps (Priority Order)

1. **Get GNSS fix** — move receiver to clear sky, verify position data in NMEA sentences
2. **Set up RTKLIB path** — configure RTKLIB to read u-blox directly via serial (bypasses the TCP bridge for better performance and Doppler velocity)
3. **Connect IMU** — required for full eagleye localization
4. **Configure eagleye parameters** — adjust `eagleye_config.yaml` and `sensors_tf.yaml` for your sensor setup
5. **Full eagleye launch** — `ros2 launch eagleye_rt eagleye_rt.launch.xml`

---

## 5. Eagleye Launch Reference

### 5.1 RTKLIB Path (Recommended for ZED-F9P)

```bash
# Terminal 1: Start RTKLIB
cd ~/RTKLIB && bash rtklib_ros_bridge.sh

# Terminal 2: RTKLIB ROS bridge
ros2 run rtklib_bridge rtklib_bridge --ros-args --params-file src/rtklib_ros_bridge/rtklib_bridge/param/param.yaml

# Terminal 3: Eagleye
ros2 launch eagleye_rt eagleye_rt.launch.xml
```

### 5.2 NMEA Path (Current Setup)

```bash
# Terminal 1: Serial bridge
python3 serial_tcp_bridge.py

# Terminal 2: NMEA bridge
ros2 run nmea_ros_bridge nmea_tcp --ros-args --params-file config/nmea_tcp_local.yaml

# Terminal 3: Eagleye
ros2 launch eagleye_rt eagleye_rt.launch.xml
```

### 5.3 Eagleye Input Topics

| Topic | Message Type | Source | Required |
|-------|-------------|--------|----------|
| `/imu/data_raw` | `sensor_msgs/Imu` | IMU driver | ✅ Required (50 Hz) |
| `/can_twist` | `geometry_msgs/TwistStamped` | CAN/wheel speed | ⚠️ Recommended |
| `/nmea_sentence` | `nmea_msgs/Sentence` | nmea_ros_bridge | ✅ Required (NMEA path) |
| `/rtklib_nav` | `rtklib_msgs/RtklibNav` | rtklib_bridge | ✅ Required (RTKLIB path) |

### 5.4 Eagleye Output Topics

| Topic | Message Type | Description |
|-------|-------------|-------------|
| `/eagleye/fix` | `sensor_msgs/NavSatFix` | Estimated position |
| `/eagleye/twist` | `geometry_msgs/TwistStamped` | Estimated velocity |
| `/eagleye/twist_with_covariance` | `geometry_msgs/TwistWithCovarianceStamped` | Velocity with uncertainty |

> **Note:** Eagleye requires ~100 seconds of data accumulation before stable estimates are output.

### 5.5 Configuration Files

| File | Purpose |
|------|---------|
| `src/eagleye/eagleye_rt/config/eagleye_config.yaml` | Algorithm parameters (GNSS rate, IMU rate, filter settings) |
| `src/eagleye/eagleye_util/tf/config/sensors_tf.yaml` | Sensor transforms relative to `base_link` |

---

## 6. Troubleshooting

### GNSS receiver not detected
```bash
lsusb | grep u-blox          # Should show "u-blox AG"
ls /dev/ttyACM*               # Should show /dev/ttyACM0
ls /dev/serial/by-id/         # Symlink to u-blox device
```

### Serial port busy
```bash
fuser /dev/ttyACM0            # Find process holding the port
kill <PID>                    # Kill it
```

### No NMEA data on ROS topic
```bash
# Check bridge is running
ps aux | grep serial_tcp_bridge

# Check nmea_tcp is connected
ros2 topic list | grep nmea

# Check raw serial data
timeout 5 cat /dev/ttyACM0
```

### Build fails with "Duplicate package names"
This means packages exist both in workspace root and `src/`. Keep only the `src/` copies:
```bash
rm -rf eagleye gnss_compass_ros llh_converter nmea_ros_bridge rtklib_ros_bridge
```

---

## 7. Key Resources

- [Eagleye GitHub](https://github.com/MapIV/eagleye)
- [RTKLIB (MapIV fork)](https://github.com/MapIV/RTKLIB)
- [u-blox ZED-F9P Datasheet](https://www.u-blox.com/en/product/zed-f9p-module)
- [Autoware + Eagleye Integration Guide](https://autowarefoundation.github.io/autoware-documentation/)
- [Eagleye Demo Video](https://youtu.be/u8Nan38BkDw)
