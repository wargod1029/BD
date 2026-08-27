# Copyright 2023 Ouster, Inc.
#

"""Launch a sensor node along with os_cloud and os_"""

from pathlib import Path
import launch
import lifecycle_msgs.msg
from ament_index_python.packages import get_package_share_directory
from launch_ros.actions import LifecycleNode
from launch.actions import (DeclareLaunchArgument, IncludeLaunchDescription,
                            RegisterEventHandler, EmitEvent, LogInfo)
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.events import matches_action
from launch_ros.events.lifecycle import ChangeState
from launch_ros.event_handlers import OnStateTransition


def generate_launch_description():
    """
    Generate launch description for running ouster_ros components separately each
    component will run in a separate process).
    """
    ouster_ros_pkg_dir = get_package_share_directory('ouster_ros')
    default_params_file = \
        Path(ouster_ros_pkg_dir) / 'config' / 'driver_params.yaml'
    params_file = LaunchConfiguration('params_file')
    params_file_arg = DeclareLaunchArgument('params_file',
                                            default_value=str(
                                                default_params_file),
                                            description='name or path to the parameters file to use.')

    ouster_ns = LaunchConfiguration('ouster_ns')
    ouster_ns_arg = DeclareLaunchArgument(
        'ouster_ns', default_value='ouster')

    rviz_enable = LaunchConfiguration('viz')
    rviz_enable_arg = DeclareLaunchArgument('viz', default_value='True')

    os_driver_name = LaunchConfiguration('os_driver_name')
    os_driver_name_arg = DeclareLaunchArgument(
        'os_driver_name', default_value='os_driver')

    sensor_hostname = LaunchConfiguration('sensor_hostname')
    sensor_hostname_arg = DeclareLaunchArgument(
        'sensor_hostname', default_value='')

    udp_dest = LaunchConfiguration('udp_dest')
    udp_dest_arg = DeclareLaunchArgument('udp_dest', default_value='')

    udp_profile_lidar = LaunchConfiguration('udp_profile_lidar')
    udp_profile_lidar_arg = DeclareLaunchArgument(
        'udp_profile_lidar', default_value='')

    lidar_port = LaunchConfiguration('lidar_port')
    lidar_port_arg = DeclareLaunchArgument('lidar_port', default_value='0')

    imu_port = LaunchConfiguration('imu_port')
    imu_port_arg = DeclareLaunchArgument('imu_port', default_value='0')

    metadata = LaunchConfiguration('metadata')
    metadata_arg = DeclareLaunchArgument('metadata', default_value='')

    os_driver = LifecycleNode(
        package='ouster_ros',
        executable='os_driver',
        name=os_driver_name,
        namespace=ouster_ns,
        parameters=[params_file, {
            'sensor_hostname': sensor_hostname,
            'udp_dest': udp_dest,
            'udp_profile_lidar': udp_profile_lidar,
            'lidar_port': lidar_port,
            'imu_port': imu_port,
            'metadata': metadata,
        }],
        output='screen',
    )

    sensor_configure_event = EmitEvent(
        event=ChangeState(
            lifecycle_node_matcher=matches_action(os_driver),
            transition_id=lifecycle_msgs.msg.Transition.TRANSITION_CONFIGURE,
        )
    )

    sensor_activate_event = RegisterEventHandler(
        OnStateTransition(
            target_lifecycle_node=os_driver, goal_state='inactive',
            entities=[
                LogInfo(msg="os_driver activating..."),
                EmitEvent(event=ChangeState(
                    lifecycle_node_matcher=matches_action(os_driver),
                    transition_id=lifecycle_msgs.msg.Transition.TRANSITION_ACTIVATE,
                )),
            ],
            handle_once=True
        )
    )

    sensor_finalized_event = RegisterEventHandler(
        OnStateTransition(
            target_lifecycle_node=os_driver, goal_state='finalized',
            entities=[
                LogInfo(
                    msg="Failed to communicate with the sensor in a timely manner."),
                EmitEvent(event=launch.events.Shutdown(
                    reason="Couldn't communicate with sensor"))
            ],
        )
    )

    rviz_launch_file_path = \
        Path(ouster_ros_pkg_dir) / 'launch' / 'rviz.launch.py'
    rviz_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([str(rviz_launch_file_path)]),
        condition=IfCondition(rviz_enable)
    )

    return launch.LaunchDescription([
        params_file_arg,
        ouster_ns_arg,
        rviz_enable_arg,
        os_driver_name_arg,
        sensor_hostname_arg,
        udp_dest_arg,
        udp_profile_lidar_arg,
        lidar_port_arg,
        imu_port_arg,
        metadata_arg,
        rviz_launch,
        os_driver,
        sensor_configure_event,
        sensor_activate_event,
        sensor_finalized_event
    ])
