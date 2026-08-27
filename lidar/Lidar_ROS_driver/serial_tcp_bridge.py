#!/usr/bin/env python3
"""
Serial-to-TCP Bridge for u-blox GNSS Receiver

Reads NMEA sentences from a serial-connected u-blox GNSS receiver
and serves them on a TCP port. The nmea_ros_bridge (nmea_tcp node)
connects as a TCP client to this server.

Usage:
    python3 serial_tcp_bridge.py [--port PORT] [--device DEVICE] [--baud BAUD]

Default: TCP server on port 62002, reading /dev/ttyACM0 at 9600 baud
"""

import serial
import socket
import threading
import argparse
import sys
import time


def parse_args():
    parser = argparse.ArgumentParser(description='Serial-to-TCP bridge for GNSS NMEA data')
    parser.add_argument('--port', type=int, default=62002, help='TCP port to listen on (default: 62002)')
    parser.add_argument('--device', type=str, default='/dev/ttyACM0', help='Serial device path (default: /dev/ttyACM0)')
    parser.add_argument('--baud', type=int, default=38400, help='Serial baud rate (default: 38400)')
    return parser.parse_args()


def main():
    args = parse_args()

    # Open serial port
    print(f"[Bridge] Opening serial port {args.device} at {args.baud} baud...")
    try:
        ser = serial.Serial(args.device, args.baud, timeout=1)
        print(f"[Bridge] Serial port opened: {ser.name}")
    except Exception as e:
        print(f"[Bridge] ERROR: Could not open serial port: {e}", file=sys.stderr)
        sys.exit(1)

    # Create TCP server
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', args.port))
    server.listen(1)
    print(f"[Bridge] TCP server listening on 0.0.0.0:{args.port}")

    try:
        while True:
            print(f"[Bridge] Waiting for client connection...")
            client, addr = server.accept()
            print(f"[Bridge] Client connected from {addr}")

            try:
                while True:
                    # Read from serial and write to TCP socket
                    data = ser.readline()
                    if data:
                        try:
                            client.send(data)
                        except (BrokenPipeError, ConnectionResetError):
                            print(f"[Bridge] Client disconnected")
                            break
            except Exception as e:
                print(f"[Bridge] Connection error: {e}")
            finally:
                client.close()
                print(f"[Bridge] Client connection closed")
    except KeyboardInterrupt:
        print(f"\n[Bridge] Shutting down...")
    finally:
        ser.close()
        server.close()
        print(f"[Bridge] Closed")


if __name__ == '__main__':
    main()
