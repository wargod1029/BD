"""Launch a single Hikrobot MVS camera with RViz."""
import os
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg_share = FindPackageShare('mvs_ros_driver')

    return LaunchDescription([
        # Camera driver node
        Node(
            package='mvs_ros_driver',
            executable='grabImgWithTrigger',
            name='mvs_camera_trigger',
            arguments=[PathJoinSubstitution([pkg_share, 'config', 'DA5324655.yaml'])],
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
