# Mission: STM32CubeMX 配置两轮差速小车

## Why

为 STM32F103C8T6 两轮差速、前万向轮小车生成可在 Keil 中编译与烧录的初始化工程，并按固定路线自动循环。

## Success looks like

- 能配置 PA0/PA3 的 TIM2 PWM 与 PA1/PA2/PA4/PA5 的方向 GPIO。
- 能生成 Keil MDK-ARM 工程并保留用户控制代码。

## Constraints

- 使用 STM32F103C8T6、Keil、L298N 和约 6V TT 电机。
- GPIO6 已损坏，不使用；没有遥控或 Wi-Fi 模块。

## Out of scope

- 四轮麦克纳姆全向控制、ROS 2 和远程控制。
