// Copy this file beside ros2_udp_motor_controller.ino as secrets.h.
// Keep the password in the local secrets.h file only.
#pragma once

#define WIFI_SSID "HONOR-0F13VA"
#define WIFI_PASSWORD "zxcvbnm13643779613"

// Ubuntu VM address, which runs the ROS 2 UDP bridge.
const IPAddress BRIDGE_IP(192, 168, 3, 105);
