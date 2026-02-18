# TimestampProbe

XRobot 框架下的时间戳探测模块，配合上位机 timestamp_calibrator 测量系统通信延迟。

## 功能

- 接收上位机发送的 ts_ping 数据包，立即附加 MCU 当前的微秒时间戳并回传 ts_pong。
- 订阅 ahrs_quaternion 主题，在回调函数中附加 MCU 当前的微秒时间戳，随后发布 ahrs_stamped 主题。
- 提供 RamFS 命令 `ts_probe`，用于在终端查看模块运行状态，包括 ping 接收计数、AHRS 转发计数以及 MCU 当前时间。

## 数据结构

模块主要处理以下三种数据结构：

- **TsPing**: PC 发送到 MCU 的测试包。
  - `uint32_t sequence`: 序号，用于匹配请求与响应。
- **TsPong**: MCU 返回给 PC 的响应包。
  - `uint32_t sequence`: 对应的请求序号。
  - `uint64_t mcu_timestamp`: MCU 收到请求并处理时的微秒级时间戳。
- **AhrsStamped**: 包含时间戳的姿态信息。
  - `LibXR::Quaternion<float> quaternion`: 四元数姿态数据，直接使用 LibXR 原生类型。
  - `uint64_t mcu_timestamp`: 传感器采样或数据处理完成时的 MCU 微秒级时间戳。

## 配置

在 `ts_calibrate.yaml` 中配置模块。该模块需要 BMI088 驱动和 MadgwickAHRS 算法模块的配合。

配置示例中，需要包含以下部分：
- **SharedTopic**: 用于接收 PC 端发来的 `ts_ping`。
- **SharedTopicClient**: 用于向 PC 端发送 `ts_pong`、`ahrs_stamped`，并从系统中订阅 `ahrs_quaternion`。

构造函数参数 `constructor_args` 说明：
- **quaternion_topic_name**: 指定订阅的原始四元数主题名称。
- **task_stack_depth**: 模块内部任务的堆栈深度。

## 编译

1. 使用 xrobot_gen_main 工具根据配置文件生成主程序代码：
   ```bash
   xrobot_gen_main --config User/RobotConfig/ts_calibrate.yaml --output User/xrobot_main.hpp
   ```
2. 使用标准的 CMake 交叉编译工具链进行编译，并将固件烧录至目标 MCU。

## 工作流程

系统的数据流向如下：

```text
[BMI088] -> 原始陀螺仪/加速度计数据 -> [MadgwickAHRS] -> ahrs_quaternion -> [TimestampProbe] -> ahrs_stamped -> USB -> PC

PC -> ts_ping -> USB -> [TimestampProbe] -> ts_pong -> USB -> PC
```

## 终端命令

在 XRobot 控制台中输入以下命令查看状态：

- `ts_probe`: 输出当前模块的运行统计信息，例如：
  ```text
  TimestampProbe Status:
  Ping Received: 120
  AHRS Forwarded: 5400
  Current MCU Time: 123456789 us
  ```
