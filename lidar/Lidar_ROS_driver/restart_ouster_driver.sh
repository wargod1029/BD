#!/usr/bin/env bash
set -euo pipefail

WORKSPACE_DIR="/home/risksis/Documents/intern/ROS_noetic/ouster_humble_ws"
SENSOR_HOSTNAME="169.254.155.51"
UDP_DEST="169.254.127.42"
LIDAR_PORT="7502"
IMU_PORT="7503"
UDP_PROFILE_LIDAR="RNG19_RFL8_SIG16_NIR16"
VIZ="false"
TIMESTAMP_MODE="TIME_FROM_SYNC_PULSE_IN"
MULTIPURPOSE_IO_MODE="INPUT_NMEA_UART"
SYNC_PULSE_IN_POLARITY="ACTIVE_HIGH"
NMEA_IN_POLARITY="ACTIVE_HIGH"
NMEA_IN_BAUDRATE="BAUD_38400"

if [ -n "${ROS_DISTRO:-}" ]; then
  true
else
  source /opt/ros/humble/setup.bash
fi

# install/setup.bash may reference variables that are unset when strict mode is enabled.
set +u
source "$WORKSPACE_DIR/install/setup.bash"
set -u

printf "Stopping existing Ouster driver processes...\n"
pkill -f "ros2 launch ouster_ros driver.launch.py" 2>/dev/null || true
pkill -f "os_driver" 2>/dev/null || true
sleep 1

printf "Restarting Ouster driver and force-configuring sensor %s...\n" "$SENSOR_HOSTNAME"
ros2 launch ouster_ros driver.launch.py \
  sensor_hostname:="$SENSOR_HOSTNAME" \
  udp_dest:="$UDP_DEST" \
  lidar_port:="$LIDAR_PORT" \
  imu_port:="$IMU_PORT" \
  udp_profile_lidar:="$UDP_PROFILE_LIDAR" \
  viz:="$VIZ" \
  timestamp_mode:="$TIMESTAMP_MODE" \
  multipurpose_io_mode:="$MULTIPURPOSE_IO_MODE" \
  sync_pulse_in_polarity:="$SYNC_PULSE_IN_POLARITY" \
  nmea_in_polarity:="$NMEA_IN_POLARITY" \
  nmea_in_baudrate:="$NMEA_IN_BAUDRATE" \
  force_reinit:=true

#after viz, is new, maybe is because of those, it gged, just maybe