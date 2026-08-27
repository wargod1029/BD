#!/usr/bin/env python3
"""Simple ROS2 image viewer — subscribes to a topic and displays with OpenCV."""
import sys
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from cv_bridge import CvBridge
import cv2

class ImageViewer(Node):
    def __init__(self, topic):
        super().__init__('image_viewer')
        self.bridge = CvBridge()
        self.sub = self.create_subscription(Image, topic, self.callback, 10)
        self.get_logger().info(f'Listening on {topic}...')

    def callback(self, msg):
        cv_image = self.bridge.imgmsg_to_cv2(msg, 'bgr8')
        # Resize to fit screen (max 640px on longest side)
        h, w = cv_image.shape[:2]
        scale = min(1280 / max(h, w), 1.0)
        if scale < 1.0:
            cv_image = cv2.resize(cv_image, (int(w * scale), int(h * scale)))
        cv2.imshow('Camera', cv_image)
        cv2.waitKey(1)

def main():
    rclpy.init()
    topic = sys.argv[1] if len(sys.argv) > 1 else '/DB1597646/image'
    node = ImageViewer(topic)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    cv2.destroyAllWindows()
    rclpy.shutdown()

if __name__ == '__main__':
    main()
