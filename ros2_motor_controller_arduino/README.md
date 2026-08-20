# Archived Arduino IDE attempt — not compatible with ESP32-S3

This folder is retained as a reference for the motor-control logic only. Do not
build it for this ESP32-S3-N16R8 board. The Jazzy `micro_ros_arduino` release
ships a precompiled library for the classic ESP32 target but not ESP32-S3, so
Arduino IDE cannot link a micro-ROS client for this board.

Use the ESP-IDF micro-ROS project instead. It builds the client for the
ESP32-S3 target rather than trying to link the incompatible Arduino archive.

## Local configuration and upload

1. Copy `secrets.h.example` to `secrets.h` in this folder and enter the Wi-Fi password locally.
2. Open `ros2_motor_controller_arduino.ino` in Arduino IDE and upload through the CH340 serial port.
3. Keep the Agent running in Ubuntu:

   ```bash
   ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888 -v6
   ```

## Wiring

| L298N | ESP32-S3 |
| --- | --- |
| IN1 | GPIO4 |
| IN2 | GPIO5 |
| ENA | GPIO6 |
| GND | GND |

Remove the L298N ENA jumper before attaching GPIO6.

## Test

```bash
source /opt/ros/jazzy/setup.bash
ros2 topic echo /motor/status
ros2 topic pub --once /motor/cmd std_msgs/msg/Int16 "{data: 160}"
ros2 topic pub --once /motor/cmd std_msgs/msg/Int16 "{data: -160}"
ros2 topic pub --once /motor/cmd std_msgs/msg/Int16 "{data: 0}"
```

Commands range from -255 to 255. Zero, or no command for 300 ms, dynamically brakes the motor.
