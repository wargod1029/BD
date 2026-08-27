"""Launch two MVS cameras (left/right stereo pair) with RViz."""
import os
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg_share = FindPackageShare('mvs_ros_driver')

    return LaunchDescription([
        # Left camera
        Node(
            package='mvs_ros_driver',
            executable='grabImgWithTrigger',
            name='left_camera',
            arguments=[PathJoinSubstitution([pkg_share, 'config', 'left_camera_trigger.yaml'])],
            output='screen',
            respawn=True,
        ),
        # Right camera
        Node(
            package='mvs_ros_driver',
            executable='grabImgWithTrigger',
            name='right_camera',
            arguments=[PathJoinSubstitution([pkg_share, 'config', 'right_camera_trigger.yaml'])],
            output='screen',
            respawn=True,
        ),
        # RViz (disabled by default - uncomment to enable)
        # Node(
        #     package='rviz2',
        #     executable='rviz2',
        #     name='rviz',
        #     arguments=['-d', PathJoinSubstitution([pkg_share, 'rviz_cfg', 'mvs_camera.rviz'])],
        # ),
    ])
