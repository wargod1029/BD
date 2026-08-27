"""Launch 6 MVS cameras (full array)."""
import os
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg_share = FindPackageShare('mvs_ros_driver')

    cameras = [
        ('DA5148680', 'DA5148680.yaml'),
        ('DA5324655', 'DA5324655.yaml'),
        ('DA5148683', 'DA5148683.yaml'),
        ('DA4930148', 'DA4930148.yaml'),
        ('DA6102933', 'DA6102933.yaml'),
        ('DA5324645', 'DA5324645.yaml'),
    ]

    nodes = []
    for name, config_file in cameras:
        nodes.append(Node(
            package='mvs_ros_driver',
            executable='grabImgWithTrigger',
            name=name,
            arguments=[PathJoinSubstitution([pkg_share, 'config', config_file])],
            output='screen',
            respawn=True,
        ))

    # RViz (disabled by default - uncomment to enable)
    # nodes.append(Node(
    #     package='rviz2',
    #     executable='rviz2',
    #     name='rviz',
    #     arguments=['-d', PathJoinSubstitution([pkg_share, 'rviz_cfg', 'mvs_camera.rviz'])],
    # ))

    return LaunchDescription(nodes)
