# STM32F103C8T6 + L298N 固定路径循环小车（Keil）

此程序用于两后轮差速驱动、前万向支撑轮的小车。无遥控、无 Wi-Fi；上电后永久执行固定路线：

```text
前进 3 秒 → 停 1 秒 → 后退 2 秒 → 停 1 秒
→ 左转 1.2 秒 → 停 1 秒 → 右转 1.2 秒 → 停 1 秒
→ 原地左转 1 秒 → 停 1 秒 → 原地右转 1 秒 → 停 2 秒 → 重复
```

## 接线

先拔掉 L298N 的 `ENA`、`ENB` 跳帽，才能由 STM32 输出 PWM 调速。STM32 GND、L298N GND、电机电源负极必须共地。

| L298N | STM32F103C8T6 |
| --- | --- |
| ENA | PA0（TIM2_CH1 PWM） |
| IN1 | PA1 |
| IN2 | PA2 |
| ENB | PA3（TIM2_CH4 PWM） |
| IN3 | PA4 |
| IN4 | PA5 |
| GND | GND（共地） |

PA6、PA7 保留未用。默认 PWM 是 40%；在 `main.c` 顶部修改 `PWM_PERCENT` 为 `20U`、`40U`、`60U` 或 `80U`。

## Keil 设置

1. 新建 MDK 工程，芯片选择 `STM32F103C8`。
2. 添加 Device 的 `startup_stm32f10x_md.s` 和 CMSIS；把本目录的 `main.c` 加入工程。
3. 使用 ST-Link 烧录。

程序的暂停是滑行停止（ENA/ENB PWM=0），这样换向前更安全。若前进方向反了，交换对应 TT 电机在 L298N 输出端的两根电机线。

## 电源安全

额定 6V TT 电机需使用约 6V 电机电源。不要长期以 12V 直接供电。STM32 建议由 USB/ST-Link 或独立稳压 3.3V/5V 供电；不要用 L298N 板载 5V 给开发板供电。
