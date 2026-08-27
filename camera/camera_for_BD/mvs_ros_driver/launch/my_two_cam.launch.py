"""Launch two MVS cameras (DB1597646 + DB1597645) in CPU-only mode.

Usage:
    ros2 launch mvs_ros_driver my_two_cam.launch.py

Each camera runs the CPU driver `grabImgWithTriggerCPU` (grab -> CPU debayer ->
undistort -> resize -> publish). Both configs are free-run (TriggerEnable: 0),
so the frames are NOT synchronized between the two cameras.

Topics:
    /DB1597646/image
    /DB1597645/image

To add a third camera, copy one of the config YAMLs, set its SerialNumber +
TopicName, and append another (name, config) tuple below.
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
            executable='grabImgWithTriggerCPU',
            name=name,
            arguments=[PathJoinSubstitution([pkg_share, 'config', config_file])],
            output='screen',
            respawn=False,
        ))

    return LaunchDescription(nodes)
