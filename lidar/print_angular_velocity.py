#!/usr/bin/env python3
"""Print angular velocity from Ouster IMU."""
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Imu


class AngularVelocityPrinter(Node):
    def __init__(self):
        super().__init__('angular_velocity_printer')
        self.sub = self.create_subscription(Imu, '/ouster/imu', self.callback, 10)
        self.last_print = self.get_clock().now()

    def callback(self, msg: Imu):
        now = self.get_clock().now()
        if (now - self.last_print).nanoseconds / 1e9 < 1.0:  # throttle to 1 Hz
            return
        self.last_print = now

        gx = msg.angular_velocity.x  # rad/s
        gy = msg.angular_velocity.y
        gz = msg.angular_velocity.z

        # Convert to deg/s for readability
        gx_d = gx * 57.2958
        gy_d = gy * 57.2958
        gz_d = gz * 57.2958

        self.get_logger().info(
            f'Gyro (deg/s):  x={gx_d:8.2f}  y={gy_d:8.2f}  z={gz_d:8.2f}'
        )


def main():
    rclpy.init()
    node = AngularVelocityPrinter()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    rclpy.shutdown()


if __name__ == '__main__':
    main()
