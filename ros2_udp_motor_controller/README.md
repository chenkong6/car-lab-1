# Arduino ESP32-S3 UDP motor controller

This is the recommended firmware for the ESP32-S3-N16R8. It needs only the
ordinary Arduino ESP32 Wi-Fi library; it does not use micro-ROS or ESP-IDF.

## Arduino setup

1. Use your normal ESP32-S3 Arduino board package and select the same board
   configuration that successfully uploaded the previous web-controller sketch.
2. Copy `secrets.h.example` to `secrets.h` in this folder and enter the Honor
   Wi-Fi credentials locally.
3. Upload `ros2_udp_motor_controller.ino` through the CH340 serial port.
4. Open Arduino Serial Monitor at **115200 baud**. On success it prints
   `WiFi connected. ESP32 IP: ...`; record this IP for troubleshooting.

## Wiring

| L298N | ESP32-S3 |
| --- | --- |
| IN1 | GPIO4 |
| IN2 | GPIO5 |
| ENA | GPIO6 |
| GND | GND |

Remove the ENA jumper before connecting GPIO6. ESP32 and L298N must share
ground.

The ESP32 listens for `CMD <signed PWM>` on UDP port `8888`, sends
`STATUS <signed PWM>` to the Ubuntu VM on UDP port `8889`, dynamically brakes
on zero, and automatically brakes after 300 ms without a command.
