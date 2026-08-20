# ESP-IDF micro-ROS Jazzy motor controller for ESP32-S3

This is the buildable ESP32-S3 implementation. It uses the official Jazzy
`micro_ros_espidf_component`, ESP-IDF's native ESP32-S3 target, and Wi-Fi UDP.

## Hardware

| L298N pin | ESP32-S3 pin |
| --- | --- |
| IN1 | GPIO4 |
| IN2 | GPIO5 |
| ENA | GPIO6 |
| GND | GND |

Remove the L298N ENA jumper before connecting GPIO6. The motor driver and the
ESP32 must share ground.

## Configure and build (Ubuntu VM — recommended)

Build in Ubuntu rather than native Windows. The micro-ROS component builds a
host-side Jazzy dependency workspace and requires the Unix toolchain that is
already natural on Ubuntu. Keep the micro-ROS Agent running in one terminal;
use a separate, clean terminal for the build (do not source ROS 2 there).

Install ESP-IDF 5.3.5 and its prerequisites once:

```bash
sudo apt update
sudo apt install -y git flex bison gperf cmake ninja-build ccache libffi-dev libssl-dev dfu-util python3-pip python3-venv
mkdir -p ~/esp
cd ~/esp
git clone -b v5.3.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

Copy this project to an ASCII-only directory in Ubuntu, then build it:

```bash
. ~/esp/esp-idf/export.sh
cd ~/esp/projects/ros2_motor_controller_idf
pip3 install catkin_pkg colcon-common-extensions lark
idf.py set-target esp32s3
idf.py menuconfig
```

In `menuconfig`, open **micro-ROS Settings** and select:

1. `micro-ROS over eProsima Micro XRCE-DDS`;
2. `WLAN interface`;
3. under **WiFi Configuration**, enter the Honor Wi-Fi SSID and password;
4. set **micro-ROS Agent IP** to `10.106.198.105` and port to `8888`.

Then build and flash through the CH340 serial port (normally `/dev/ttyUSB0`):

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

In VMware, connect the CH340 USB device to the Ubuntu guest first. Confirm the
port with `ls /dev/ttyUSB*`.

The first build downloads and compiles the micro-ROS static library, so it can
take several minutes. Keep the Ubuntu Agent running during the network test:

```bash
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888 -v6
```

## ROS test

```bash
source /opt/ros/jazzy/setup.bash
ros2 topic echo /motor/status
ros2 topic pub --once /motor/cmd std_msgs/msg/Int32 "{data: 160}"
ros2 topic pub --once /motor/cmd std_msgs/msg/Int32 "{data: -160}"
ros2 topic pub --once /motor/cmd std_msgs/msg/Int32 "{data: 0}"
```

`/motor/cmd` accepts `-255` through `255`. A zero command dynamically brakes.
The controller also brakes automatically after 300 ms without a command.
