"""Launch a single camera in CPU mode with live-tunable parameters.

The `test_CPU_grab` executable exposes the image knobs (exposure, gain, gamma,
brightness, scale) as ROS2 dynamic parameters, so they can be tuned at runtime:

    ros2 launch mvs_ros_driver test_CPU_grab.launch.py
    ros2 param set /test_CPU_grab ExposureTime 1500
    ros2 param set /test_CPU_grab Gain 12.0
"""
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg_share = FindPackageShare('mvs_ros_driver')

    return LaunchDescription([
        Node(
            package='mvs_ros_driver',
            executable='test_CPU_grab',
            name='test_CPU_grab',
            arguments=[PathJoinSubstitution([pkg_share, 'config', 'DB1597646.yaml'])],
            output='screen',
            respawn=False,
        ),
    ])
