# ROS 2 Jazzy UDP bridge

Copy `motor_udp_bridge.py` to Ubuntu, then run it in a terminal separate from
the micro-ROS Agent:

```bash
source /opt/ros/jazzy/setup.bash
python3 motor_udp_bridge.py
```

The bridge learns the ESP32's DHCP address from its periodic status packets.
If status packets are blocked, set the board address explicitly:

```bash
python3 motor_udp_bridge.py --ros-args -p esp_ip:=10.106.198.123
```

Test from another sourced Jazzy terminal:

```bash
ros2 topic echo /motor/status
ros2 topic pub --once /motor/cmd std_msgs/msg/Int32 "{data: 160}"
ros2 topic pub --once /motor/cmd std_msgs/msg/Int32 "{data: -160}"
ros2 topic pub --once /motor/cmd std_msgs/msg/Int32 "{data: 0}"
```
