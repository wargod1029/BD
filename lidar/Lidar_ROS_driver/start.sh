#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# start.sh — Launch the full GNSS + Ouster + Eagleye localization pipeline
#
# Components:
#   1. serial_tcp_bridge.py  — GNSS NMEA from /dev/ttyACM0 → TCP :62002
#   2. nmea_tcp node          — TCP → ROS topic /nmea_sentence
#   3. Ouster lidar driver    — LiDAR + IMU (/ouster/imu)
#   4. Eagleye RT             — GNSS + IMU fusion → /eagleye/pose
#
# Usage:
#   ./start.sh [--no-lidar] [--no-eagleye]
# =============================================================================

WORKSPACE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- Ouster sensor config ------------------------------------------------
SENSOR_HOSTNAME="${SENSOR_HOSTNAME:-169.254.155.51}"
UDP_DEST="${UDP_DEST:-169.254.127.42}"
LIDAR_PORT="${LIDAR_PORT:-7502}"
IMU_PORT="${IMU_PORT:-7503}"
UDP_PROFILE_LIDAR="${UDP_PROFILE_LIDAR:-RNG19_RFL8_SIG16_NIR16}"

# ---- Serial config -------------------------------------------------------
SERIAL_DEVICE="${SERIAL_DEVICE:-/dev/ttyACM0}"
SERIAL_BAUD="${SERIAL_BAUD:-38400}"
TCP_PORT="${TCP_PORT:-62002}"

# ---- Flags ----------------------------------------------------------------
SKIP_LIDAR=false
SKIP_EAGLEYE=false

for arg in "$@"; do
  case "$arg" in
    --no-lidar)  SKIP_LIDAR=true ;;
    --no-eagleye) SKIP_EAGLEYE=true ;;
    *) echo "Unknown option: $arg"; exit 1 ;;
  esac
done

# ---- Source ROS -----------------------------------------------------------
if [ -z "${ROS_DISTRO:-}" ]; then
  echo "[start] Sourcing ROS 2 Humble..."
  source /opt/ros/humble/setup.bash
fi

set +u
source "$WORKSPACE_DIR/install/setup.bash"
set -u

# ---- Cleanup function -----------------------------------------------------
cleanup() {
  echo ""
  echo "[start] Shutting down..."
  pkill -f "serial_tcp_bridge.py" 2>/dev/null || true
  pkill -f "nmea_tcp" 2>/dev/null || true
  if ! $SKIP_LIDAR; then
    pkill -f "ros2 launch ouster_ros" 2>/dev/null || true
  fi
  if ! $SKIP_EAGLEYE; then
    pkill -f "ros2 launch eagleye_rt" 2>/dev/null || true
  fi
  echo "[start] Done."
}
trap cleanup EXIT INT TERM

# =============================================================================
# 1. Serial → TCP Bridge
# =============================================================================
echo "========================================================================"
echo "[start] Step 1: Serial → TCP Bridge"
echo "========================================================================"

# Kill any stale bridge
pkill -f "serial_tcp_bridge.py" 2>/dev/null || true
sleep 0.5

python3 "$WORKSPACE_DIR/serial_tcp_bridge.py" \
  --device "$SERIAL_DEVICE" \
  --baud "$SERIAL_BAUD" \
  --port "$TCP_PORT" &
BRIDGE_PID=$!

# Wait until the TCP port is listening
echo "[start] Waiting for bridge to listen on port $TCP_PORT..."
for i in $(seq 1 30); do
  if ss -tlnp 2>/dev/null | grep -q ":$TCP_PORT "; then
    echo "[start] Bridge is ready (PID $BRIDGE_PID)"
    break
  fi
  sleep 0.5
done

if ! kill -0 "$BRIDGE_PID" 2>/dev/null; then
  echo "[start] ERROR: Bridge failed to start!" >&2
  exit 1
fi

# =============================================================================
# 2. NMEA TCP → ROS Topics
# =============================================================================
echo ""
echo "========================================================================"
echo "[start] Step 2: NMEA TCP → ROS Bridge"
echo "========================================================================"

ros2 launch nmea_ros_bridge nmea_tcp.launch.py &
NMEA_PID=$!
sleep 2

# Check it's publishing
echo "[start] Checking /nmea_sentence topic..."
timeout 3 ros2 topic echo /nmea_sentence --once 2>/dev/null && \
  echo "[start] NMEA bridge OK — topic /nmea_sentence is live" || \
  echo "[start] WARNING: No data on /nmea_sentence yet (waiting for GNSS fix?)"

# =============================================================================
# 3. Ouster Lidar Driver (IMU source)
# =============================================================================
if ! $SKIP_LIDAR; then
  echo ""
  echo "========================================================================"
  echo "[start] Step 3: Ouster Lidar Driver"
  echo "========================================================================"

  pkill -f "ros2 launch ouster_ros driver.launch.py" 2>/dev/null || true
  sleep 1

  ros2 launch ouster_ros driver.launch.py \
    sensor_hostname:="$SENSOR_HOSTNAME" \
    udp_dest:="$UDP_DEST" \
    lidar_port:="$LIDAR_PORT" \
    imu_port:="$IMU_PORT" \
    udp_profile_lidar:="$UDP_PROFILE_LIDAR" \
    viz:=false \
    timestamp_mode:="TIME_FROM_SYNC_PULSE_IN" \
    multipurpose_io_mode:="INPUT_NMEA_UART" \
    sync_pulse_in_polarity:="ACTIVE_HIGH" \
    nmea_in_polarity:="ACTIVE_HIGH" \
    nmea_in_baudrate:="BAUD_38400" \
    force_reinit:=true &
  LIDAR_PID=$!
  sleep 5
  echo "[start] Ouster driver launched (PID $LIDAR_PID)"
else
  echo ""
  echo "[start] Step 3: Skipping Ouster (--no-lidar)"
fi

# =============================================================================
# 4. Eagleye RT (GNSS + IMU Fusion)
# =============================================================================
if ! $SKIP_EAGLEYE; then
  echo ""
  echo "========================================================================"
  echo "[start] Step 4: Eagleye RT Localization"
  echo "========================================================================"

  ros2 launch eagleye_rt eagleye_rt.launch.xml &
  EAGLEYE_PID=$!
  sleep 3
  echo "[start] Eagleye launched (PID $EAGLEYE_PID)"
else
  echo ""
  echo "[start] Step 4: Skipping Eagleye (--no-eagleye)"
fi

# =============================================================================
# Done — print status
# =============================================================================
echo ""
echo "========================================================================"
echo "[start] ALL COMPONENTS LAUNCHED"
echo "========================================================================"
echo ""
echo "  Topics to monitor:"
echo "    ros2 topic echo /nmea_sentence          # Raw GNSS NMEA"
echo "    ros2 topic echo /ouster/imu              # Lidar IMU data"
echo "    ros2 topic echo /eagleye/pose            # Fused pose (map frame)"
echo "    ros2 topic echo /eagleye/heading_interpolate_3rd  # Heading"
echo ""
echo "  Press Ctrl+C to stop everything."
echo ""

# Wait for any process to exit
wait
