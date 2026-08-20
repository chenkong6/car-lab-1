# Archived PlatformIO attempt — verify S3 support before use

This project is retained as a reference for the motor-control logic. The
official `micro_ros_platformio` support matrix names the classic `esp32dev`
target; this ESP32-S3-N16R8 project must use the ESP-IDF micro-ROS component
instead unless S3 support is explicitly added and verified upstream.

## Wiring

| L298N pin | ESP32-S3 pin |
| --- | --- |
| IN1 | GPIO4 |
| IN2 | GPIO5 |
| ENA | GPIO6 |
| GND | GND |

Remove the L298N ENA jumper before connecting GPIO6.  The ESP32 and L298N
must share ground.

## Local setup

1. Copy `include/secrets.h.example` to `include/secrets.h` and enter the Wi-Fi password locally.
2. Keep the Ubuntu Agent running: `ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888 -v6`.
3. In this directory, build and upload with `pio run --target upload`.

## ROS commands

```bash
source /opt/ros/jazzy/setup.bash
ros2 topic echo /motor/status
ros2 topic pub --once /motor/cmd std_msgs/msg/Int16 "{data: 160}"
ros2 topic pub --once /motor/cmd std_msgs/msg/Int16 "{data: -160}"
ros2 topic pub --once /motor/cmd std_msgs/msg/Int16 "{data: 0}"
```

The command range is `-255` to `255`.  Positive values rotate forward,
negative values reverse, and zero invokes dynamic braking.  If no command is
received for 300 ms, the controller brakes automatically.
