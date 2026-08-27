"""Launch two MVS cameras (DB1597646 + DB1597645) with LIVE per-camera tuning.

Usage:
    ros2 launch mvs_ros_driver my_two_cam_tunable.launch.py

Each camera runs `test_CPU_grab`, the CPU driver whose image knobs are exposed as
ROS2 dynamic parameters. Because each instance gets a distinct node name, the two
cameras are tuned completely independently while streaming — no restart, no recompile.

Topics:
    /DB1597646/image
    /DB1597645/image

Live tuning (each camera independent):
    ros2 param set /DB1597646 ExposureAutoMode 0
    ros2 param set /DB1597646 ExposureTime 3000
    ros2 param set /DB1597645 ExposureAutoMode 0
    ros2 param set /DB1597645 ExposureTime 10000

    ros2 param set /DB1597646 Gain 12.0
    ros2 param set /DB1597645 Gamma 0.7
    ros2 param set /DB1597645 image_scale 0.5

Tunable parameters (per node): ExposureAutoMode, ExposureTime,
AutoExposureTimeLower, AutoExposureTimeUpper, GainAuto, Gain, AutoGainLowerLimit,
AutoGainUpperLimit, GammaSelector, Gamma, Brightness, image_scale.
"""
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg_share = FindPackageShare('mvs_ros_driver')

    cameras = [
        ('DB1597646', 'DB1597646.yaml'),
        ('DB1597645', 'DB1597645.yaml'),
    ]

    nodes = []
    for name, config_file in cameras:
        nodes.append(Node(
            package='mvs_ros_driver',
            executable='test_CPU_grab',
            name=name,
            arguments=[PathJoinSubstitution([pkg_share, 'config', config_file])],
            output='screen',
            respawn=False,
        ))

    return LaunchDescription(nodes)
