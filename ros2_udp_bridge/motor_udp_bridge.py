#!/usr/bin/env python3
"""Bridge /motor/cmd and /motor/status between ROS 2 Jazzy and an ESP32 UDP peer."""

import socket
from typing import Optional

import rclpy
from rclpy.node import Node
from std_msgs.msg import Int32


class MotorUdpBridge(Node):
    def __init__(self) -> None:
        super().__init__('motor_udp_bridge')
        self.declare_parameter('esp_ip', '')
        self.declare_parameter('command_port', 8888)
        self.declare_parameter('status_port', 8889)

        self.command_port = self.get_parameter('command_port').value
        self.explicit_esp_ip = self.get_parameter('esp_ip').value.strip()
        status_port = self.get_parameter('status_port').value
        self.learned_esp_ip: Optional[str] = None

        self.status_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.status_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.status_socket.bind(('0.0.0.0', status_port))
        self.status_socket.setblocking(False)

        self.status_publisher = self.create_publisher(Int32, '/motor/status', 10)
        self.create_subscription(Int32, '/motor/cmd', self.send_command, 10)
        self.create_timer(0.02, self.receive_status)
        self.get_logger().info(
            f'Listening for ESP32 status on UDP {status_port}; '
            f'command port is {self.command_port}.')

    def destination_ip(self) -> Optional[str]:
        return self.explicit_esp_ip or self.learned_esp_ip

    def send_command(self, message: Int32) -> None:
        destination = self.destination_ip()
        if destination is None:
            self.get_logger().warning(
                'ESP32 address is unknown. Wait for STATUS packets, or start with '
                '`-p esp_ip:=<ESP32_IP>`.')
            return

        command = max(-255, min(255, message.data))
        payload = f'CMD {command}\n'.encode('ascii')
        self.status_socket.sendto(payload, (destination, self.command_port))

    def receive_status(self) -> None:
        while True:
            try:
                payload, address = self.status_socket.recvfrom(64)
            except BlockingIOError:
                return

            try:
                parts = payload.decode('ascii').strip().split()
                if len(parts) != 2 or parts[0] != 'STATUS':
                    continue
                command = int(parts[1])
            except (UnicodeDecodeError, ValueError):
                continue

            self.learned_esp_ip = address[0]
            message = Int32()
            message.data = command
            self.status_publisher.publish(message)


def main() -> None:
    rclpy.init()
    node = MotorUdpBridge()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
