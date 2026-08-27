"""Launch single camera in CPU-only mode (no GPU / CUDA / TensorRT required).

Usage:
    ros2 launch mvs_ros_driver test_cpu.launch.py
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
            executable='grabImgWithTriggerCPU',
            name='mvs_trigger_cpu',
            arguments=[PathJoinSubstitution([pkg_share, 'config', 'DB1597646.yaml'])],
            output='screen',
            respawn=False,
        ),
    ])
