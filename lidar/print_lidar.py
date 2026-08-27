#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import PointCloud2


class LidarPrinter(Node):
    def __init__(self):
        super().__init__('lidar_printer')
        self.subscription = self.create_subscription(
            PointCloud2,
            '/ouster/points',
            self.lidar_callback,
            qos_profile_sensor_data,
        )
        self.subscription

    def lidar_callback(self, msg: PointCloud2) -> None:
        self.get_logger().info(
            f"lidar: ts={msg.header.stamp.sec}.{msg.header.stamp.nanosec:09d}, "
            f"frame_id={msg.header.frame_id}, "
            f"width={msg.width}, height={msg.height}, "
            f"point_step={msg.point_step}, row_step={msg.row_step}, "
            f"is_dense={msg.is_dense}, "
            f"data_bytes={len(msg.data)}"
        )


def main(args=None):
    rclpy.init(args=args)
    node = LidarPrinter()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        node.get_logger().info('LiDAR subscriber stopped by user')
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()