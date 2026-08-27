source /opt/ros/humble/setup.bash
rm -rf build install log                      # clean any previous build
colcon build --packages-select mvs_ros_driver \
  --symlink-install --cmake-args -DBUILD_CPU_ONLY=ON
source install/setup.bash