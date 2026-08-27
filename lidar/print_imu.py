#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Imu


class ImuPrinter(Node):
    def __init__(self):
        super().__init__('imu_printer')
        self.subscription = self.create_subscription(
            Imu,
            '/ouster/imu',
            self.imu_callback,
            qos_profile_sensor_data,
        )
        self.subscription

    def imu_callback(self, msg: Imu) -> None:
        self.get_logger().info(
            f"imu: ts={msg.header.stamp.sec}.{msg.header.stamp.nanosec:09d}, "
            f"ang_vel=({msg.angular_velocity.x:.6f}, {msg.angular_velocity.y:.6f}, {msg.angular_velocity.z:.6f}), "
            f"lin_acc=({msg.linear_acceleration.x:.6f}, {msg.linear_acceleration.y:.6f}, {msg.linear_acceleration.z:.6f})"
        )


def main(args=None):
    rclpy.init(args=args)
    node = ImuPrinter()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        node.get_logger().info('IMU subscriber stopped by user')
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
