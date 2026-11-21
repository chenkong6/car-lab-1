# AA42 · STM32 滑台 HID 控制器

基于 **STM32F103C8T6 + TB6600 + 42 步进电机 + 2GT 同步带** 的 300 mm 滑台控制工程。

上位机通过 **USB HID 自定义设备**（VID `0x0483` / PID `0x572B`）发送回零、点动和绝对位置指令；单片机每 20 ms 回传一次当前位置与状态。工程附带 Windows 下的 Python 监控、控制、循环演示脚本，以及一个用于现场展示的网页。

---

## 1. 功能一览

- USB HID 自定义设备，免驱（Windows 自带 HID 驱动）
- 绝对位置控制，单位 0.001 mm，行程 0–300 mm
- 回零：向负方向运动，碰到复位开关后把该点设为 0
- 点动：回零失败时人工点动，用于把滑台挪到复位开关
- 20 ms 周期回传位置、运动状态、复位键、0 点、回零状态和错误码
- 网页实时展示位置与状态，适合演示
- 预留 ROS 2 接口约定（见第 10 节）

> 注意：`stm-tb6600-42/` 只是引脚和定时器配置的参考工程，本工程没有修改它；实际固件在 `key-tets/key-test/`。

---

## 2. 仓库结构

```text
AA42/
├─ key-tets/key-test/          # 实际使用的固件工程（CubeMX + Keil）
│  ├─ Core/
│  │  ├─ Inc/main.h
│  │  ├─ Inc/stm32f1xx_hal_conf.h
│  │  └─ Src/main.c            # 按键、步进、回零、HID 回传
│  ├─ USB_DEVICE/
│  │  ├─ App/usbd_custom_hid_if.c   # 报告描述符 + 0x13/0x14/0x15 命令解析
│  │  ├─ App/usbd_desc.c            # VID/PID
│  │  └─ Target/usbd_conf.h         # 报告长度、OUT 缓冲
│  ├─ MDK-ARM/key-test.uvprojx      # Keil 工程
│  └─ tools/
│     ├─ hid_key_detect.py     # HID 枚举/读取基础模块
│     ├─ hid_monitor.py        # 终端实时状态
│     ├─ hid_move.py           # 发目标 / 回零 / 点动
│     ├─ hid_cycle.py          # 回零 → 300 → 0 循环
│     ├─ hid_demo_server.py    # 网页演示服务
│     └─ demo/index.html       # 演示页面
├─ stm-tb6600-42/              # 参考工程，未使用
└─ README.md
```

---

## 3. 硬件清单

| 名称 | 说明 |
| --- | --- |
| STM32F103C8T6 核心板 | Blue Pill，8 MHz 晶振，USB 口 |
| TB6600 步进驱动器 | 9–42 V 供电，拨码设细分和电流 |
| 42 步进电机 | 两相四线 |
| 30 齿 2GT 同步轮 + 2GT 皮带 | 节距 2 mm，一圈 60 mm |
| 12–24 V 电源 | 给 TB6600 和电机 |
| 复位开关 | 接 PB12，安装在 0 点位置 |
| USB 线 | STM32 连电脑，HID 通信 |
| ST-Link | 下载调试（SWD） |

---

## 4. 接线

### 4.1 STM32 ↔ TB6600

| STM32 | TB6600 | 说明 |
| --- | --- | --- |
| PA6 | PUL+ | TIM3_CH1，1 kHz 脉冲 |
| PA0 | DIR+ | 方向，高/低对应正/反 |
| GND | PUL− | 信号地 |
| GND | DIR− | 信号地 |
| — | ENA+ / ENA− | 不接，驱动器默认使能 |

> PA6 在原小车项目里记录过损坏；如果这块板 PA6 确实坏，需要改用其他定时器通道并同步修改 `Stepper_Init()` 和接线。

### 4.2 TB6600 ↔ 电机

| TB6600 | 42 电机 |
| --- | --- |
| A+ / A− | A 相两根线 |
| B+ / B− | B 相两根线 |

用万用表确认同相：同一相通，不同相不通。

### 4.3 电源与共地

```text
12–24V 电源
  +  ──────────────  TB6600 VCC
  −  ──┬───────────  TB6600 GND
       │
       ├───────────  STM32 GND
       ├───────────  TB6600 PUL−
       └───────────  TB6600 DIR−
```

**电源负极、TB6600 GND、PUL−、DIR−、STM32 GND 必须接在一起。**
24 V 正只进 TB6600 的 VCC，不要接进 STM32。

### 4.4 复位开关

```text
PB12 ──── 复位开关 ──── GND
```

- PB12 使用内部上拉，按下为低电平。
- 开关安装在滑台 0 点位置，滑台走到 0 点时会压下它。
- 按键同时充当硬件限位：向负方向运动时碰到它立即停止，并把该点设为 0。

### 4.5 USB 与下载

- HID 通信：STM32 板载 USB 口（PA11/PA12）直接连电脑。
- ST-Link：`SWDIO→PA13`、`SWCLK→PA14`、`GND→GND`、`3.3V→3.3V`。
- BOOT0 跳帽接 **0**（GND 一侧）。

### 4.6 TB6600 拨码

细分设 **1600 步/圈**：

| S1 | S2 | S3 |
| --- | --- | --- |
| OFF | ON | OFF |

电流由 S4–S6 设置，按电机铭牌额定电流对照驱动器侧面表格。

---

## 5. 机械参数与换算

| 项目 | 值 |
| --- | --- |
| 同步轮 | 30 齿 |
| 2GT 节距 | 2 mm |
| 一圈行程 | 30 × 2 = 60 mm |
| 细分 | 1600 步/圈 |
| 步距 | 60 / 1600 = 0.0375 mm/步 |
| 每毫米步数 | 1600 / 60 = 26.667 步/mm |
| 最大行程 | 300 mm = 8000 步 |
| 脉冲频率 | 1 kHz |
| 速度 | 1000 / 26.667 = 37.5 mm/s |

固件中对应 `main.c` 顶部：

```c
#define STEPS_PER_REV   1600
#define MM_PER_REV      60
#define STEP_PULSE_HZ   1000U
#define MAX_TRAVEL_UM   300000
```

如果更换同步轮齿数、细分或皮带，需要同步修改这里。

---

## 6. 固件说明

### 6.1 CubeMX 配置

- MCU：`STM32F103C8Tx`
- 时钟：HSE 8 MHz → PLL ×9 → 72 MHz；USB 预分频 `/1.5` → 48 MHz
- SYS：Debug = Serial Wire
- PB12：GPIO_Input，Pull-up，Label `KEY`
- USB：Device (FS)
- Middleware：USB_DEVICE，Class = Custom Human Interface Device (HID)
- PA6 / PA0 / TIM3 没有写进 `.ioc`，是在 `main.c` 的 `Stepper_Init()` 里手工初始化的（PA6 = TIM3_CH1，PA0 = DIR）

> 重新用 CubeMX 生成代码时，`main.c` 的 USER CODE 区会保留；但 `stm32f1xx_hal_conf.h` 里的 `HAL_TIM_MODULE_ENABLED` 可能被还原，生成后要检查。

### 6.2 运动控制

- TIM3：PSC = 71，ARR = 999 → 1 MHz 计数、1 kHz PWM、50% 占空比
- 每个 PWM 周期产生一个 PUL 上升沿，TIM3 更新中断里计步
- 走完目标步数自动停；回零/负方向运动时每步检查 PB12，碰到立即停

### 6.3 回零流程

1. 上位机发回零命令 `0x14`
2. 固件清 `homed`，向负方向走，最多 8000 步（300 mm）
3. 每步检查 PB12；碰到开关 → 停、位置清 0、`homed=1`
4. 走满 300 mm 仍未碰到 → 停、`homed=0`、`err=3`（回零失败）

### 6.4 点动

回零失败后，可用 `0x15` 点动，单次最大 ±5 mm，人工看着把滑台挪到开关；碰到开关后自动置 0、`homed=1`。

### 6.5 绝对移动

- 只有 `homed=1` 才接受 `0x13`
- 目标范围 0–300000（0.001 mm）
- 超出范围或未回零 → 拒绝并置错误码，不动作
- 目标与当前位置相同 → 不动

---

## 7. USB HID 协议

### 7.1 设备信息

| 项目 | 值 |
| --- | --- |
| VID | `0x0483` |
| PID | `0x572B` |
| HID 报告描述符 | 97 字节 |
| EP1 IN | 8 字节，中断，5 ms |
| Feature Report | Report ID `0xA0`，15 字节数据（含 ID 共 16 字节） |

Windows 下设备路径形如：

```text
\\?\hid#vid_0483&pid_572b#...#{4d1e55b2-f16f-11cf-88cb-001111000030}
```

### 7.2 输入报告 1：按键

7 字节：

```text
[0x01][throttle][X_L][X_H][Y_L][Y_H][buttons]
```

- 目前 throttle/X/Y 为 0，仅按键变化时发送。
- `buttons` bit0：**0 = 按下，1 = 松开**（低电平有效）。
- 其余位保留。

用于兼容原来的 7 字节手柄协议。

### 7.3 输入报告 2：状态（每 20 ms）

7 字节：

```text
[0x02][pos0][pos1][pos2][pos3][status][error]
```

`pos`：int32 小端，单位 0.001 mm，范围 0–300000。

`status`：

| bit | 含义 |
| --- | --- |
| 0 | 运动中 |
| 1 | 复位键按下 |
| 2 | 位于 0 点（位置正好为 0） |
| 3 | 已回零（homed） |
| 4 | 存在错误 |

`error`：

| 值 | 含义 |
| --- | --- |
| 0 | 正常 |
| 1 | 未回零，拒绝绝对移动 |
| 2 | 超程或点动非法 |
| 3 | 回零失败（300 mm 内没碰到开关） |

示例：位置 12.345 mm、已回零、无错误：

```text
02 39 30 00 00 08 00
```

### 7.4 特征报告 0xA0：命令

主机侧 `HidD_SetFeature` 传 16 字节，第 0 字节是报告 ID `0xA0`；设备收到的数据段是 15 字节，第 0 字节是命令字。

| 命令 | 数据 | 说明 |
| --- | --- | --- |
| `0x13` | int32 目标位置，0.001 mm，小端 | 绝对移动，需已回零，范围 0–300000 |
| `0x14` | 无 | 回零：负方向找开关，最多 300 mm |
| `0x15` | int32 相对距离，0.001 mm，小端 | 人工点动，仅未回零时可用，单次 ±5 mm |

示例：

```text
去 100 mm：A0 13 A0 86 01 00 00 00 00 00 00 00 00 00 00 00
回零     ：A0 14 00 00 00 00 00 00 00 00 00 00 00 00 00 00
点动 +2mm：A0 15 D0 07 00 00 00 00 00 00 00 00 00 00 00 00
```

> 固件同时兼容“数据段里带 `0xA0`”的主机实现。

---

## 8. 电脑端工具

### 8.1 环境

- Windows 10/11
- Python 3.8+
- 不需要额外 pip 包，全部使用 `ctypes` 调 `setupapi.dll` / `hid.dll` / `kernel32.dll`
- 不需要 CH340 等串口驱动，HID 免驱

### 8.2 脚本

| 脚本 | 作用 |
| --- | --- |
| `hid_key_detect.py` | HID 枚举和读取基础模块，也可单独运行看按键 |
| `hid_monitor.py` | 终端实时显示位置/状态 |
| `hid_move.py` | 发绝对目标、回零、点动 |
| `hid_cycle.py` | 循环：回零 → 300 mm → 0 mm |
| `hid_demo_server.py` | 启动网页演示服务，读取 HID 并提供 `/api/state` |
| `demo/index.html` | 演示页面 |

### 8.3 常用命令

```bash
# 实时状态
python hid_monitor.py

# 回零
python hid_move.py reset

# 绝对移动到 100 mm
python hid_move.py 100

# 回零失败后人工点动 +2 mm / -2 mm
python hid_move.py jog 2
python hid_move.py jog -2

# 循环演示（需要先启动 hid_demo_server.py）
python hid_cycle.py
```

### 8.4 网页演示

```bash
python hid_demo_server.py
```

浏览器打开 <http://127.0.0.1:8000>。

页面只读展示，不提供控制按钮；控制仍用 `hid_move.py` 或 `hid_cycle.py`。

> 同一时间只能有一个程序持续读 HID 输入报告。跑网页服务时不要再开 `hid_monitor.py`，否则两边可能抢报告；发送命令不受影响。

---

## 9. 编译与烧录

1. 打开 `key-tets/key-test/MDK-ARM/key-test.uvprojx`
2. 编译器选 **ARM Compiler 6**（本机为 V6.22）
3. 确认工程里有 `startup_stm32f103xb.s`（Startup 组）
4. 确认 `Core/Inc/stm32f1xx_hal_conf.h` 中 `HAL_TIM_MODULE_ENABLED` 已打开
5. Rebuild，0 Error 后 Download
6. BOOT0 = 0，复位运行

常见编译问题：

- `unknown type name 'TIM_HandleTypeDef'`：TIM 模块没开，见第 4 步
- `No section matches selector ... FIRST/LAST`：工程缺 `startup_stm32f103xb.s`
- AC5 报错：工程目标里切换为 AC6

---

## 10. ROS 2 接口（规划）

目前仓库里还没有 ROS 2 包；下面是建议的接口约定，后续按这个实现即可。

### 10.1 节点

```text
slide_stage_hid
```

- 运行在能直接访问 HID 的机器上（当前脚本是 Windows）
- 读取输入报告 2，发布状态
- 接收目标/回零/点动，转成 `0xA0` 特征报告
- 参数：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `vid` | `0x0483` | USB VID |
| `pid` | `0x572B` | USB PID |
| `max_travel_m` | `0.300` | 最大行程 |
| `steps_per_mm` | `26.667` | 换算 |
| `poll_hz` | `50` | 状态发布频率 |
| `frame_id` | `slide_stage_base` | 坐标帧 |

### 10.2 话题

| 话题 | 类型 | 方向 | 说明 |
| --- | --- | --- | --- |
| `/slide_stage/state` | `aa42_msgs/msg/SlideStageState` | 发布 | 位置、速度、状态、错误 |
| `/slide_stage/target` | `std_msgs/msg/Float64` | 订阅 | 目标位置，单位 m |

`SlideStageState.msg` 建议：

```text
std_msgs/Header header
float64 position_m
float64 velocity_mps
bool moving
bool homed
bool at_zero
bool switch_active
uint8 error_code
```

### 10.3 服务

| 服务 | 类型 | 说明 |
| --- | --- | --- |
| `/slide_stage/home` | `std_srvs/srv/Trigger` | 回零，成功返回 `success` |
| `/slide_stage/jog` | `aa42_msgs/srv/Jog` | 人工点动，`float64 delta_m` |

`Jog.srv`：

```text
float64 delta_m
---
bool success
uint8 error_code
```

### 10.4 Action

| Action | 说明 |
| --- | --- |
| `/slide_stage/move_to` | 移动到绝对位置，带 feedback |

`MoveTo.action` 建议：

```text
float64 target_m
float64 max_velocity_mps
---
bool success
uint8 error_code
---
float64 position_m
bool moving
```

### 10.5 约定

- 单位统一用 **米**（ROS 惯例），HID 协议用 0.001 mm，节点内部换算。
- `position_m` 以回零后的 0 点为原点，正向为远离复位开关。
- 未 `homed` 时拒绝 `move_to`，返回 `error_code=1`。
- 超出 `[0, max_travel_m]` 返回 `error_code=2`。
- 回零失败返回 `error_code=3`。
- 节点应把 HID 的 20 ms 状态缓存，按 `poll_hz` 发布。
- 若 ROS 2 跑在 Linux，需要把 HID 访问改成 `hidraw` 或通过 Windows 侧的小型网桥转发。

---

## 11. 演示流程

1. 烧录固件，接好 TB6600、电机、电源、复位开关。
2. 上电，滑台应保持不动；网页显示 `homed=0`。
3. 启动服务：

   ```bash
   python hid_demo_server.py
   ```

4. 浏览器打开 <http://127.0.0.1:8000>。
5. 回零：

   ```bash
   python hid_move.py reset
   ```

   页面应显示“回零中”，碰到开关后 `pos=0.000`、`homed=1`。
6. 手动演示：

   ```bash
   python hid_move.py 100
   python hid_move.py 250
   python hid_move.py 0
   ```

7. 自动循环：

   ```bash
   python hid_cycle.py
   ```

   `Ctrl+C` 停止。

---

## 12. 故障排查

| 现象 | 检查 |
| --- | --- |
| 电脑找不到 0483:572B | USB 线、BOOT0、固件是否真的烧进去；设备管理器看 HID |
| `homed=0`、`err=1` | 正常，先执行 `reset` 回零 |
| 回零失败 `err=3` | 复位开关是否接 PB12/GND、滑台是否在 300 mm 内、开关是否被压住 |
| 电机有声音但不转 | 复位开关是否一直压着；PUL/DIR 是否接反；PUL−/DIR− 是否共地；拨码电流是否太小 |
| 完全不动 | TB6600 电源、ENA、细分拨码、PA6 是否损坏 |
| 位置不准 | 同步轮齿数/细分是否与 `STEPS_PER_REV`、`MM_PER_REV` 一致；皮带是否打滑 |
| 页面显示“未连接数据服务” | `hid_demo_server.py` 没启动，或端口不是 8000 |
| 回零后第一次移动不动 | 确认开关已松开；固件只在负方向检查开关，正方向应能离开 |
| Keil 编译报 TIM 类型未定义 | `HAL_TIM_MODULE_ENABLED` 没开 |
| Keil 链接报 `RESET` 段 | 工程没加 `startup_stm32f103xb.s` |

---

## 13. 安全

- 最大行程 300 mm 是软件限制；物理上仍应留出缓冲，避免撞限位。
- 回零和点动时人不要把手放在滑台与开关之间。
- 点动只允许在未回零时使用，且单次不超过 5 mm，必须人工看着。
- TB6600 电流按电机额定设置，不要用 24 V 直接给 STM32 供电。
- 电机、驱动器、STM32 必须共地；插拔电机线前先断电。
- 演示前先空跑一次回零和 300 mm 往返，确认方向、限位和急停方式。

---

## 14. 当前限制

- 只有 Windows 端脚本；ROS 2 包尚未实现。
- 运动为梯形以外的固定速度（1 kHz），没有加减速曲线。
- 位置为开环步数累计，皮带打滑或丢步会累积误差；回零可重新建立 0 点。
- 网页只读，不提供控制按钮。
