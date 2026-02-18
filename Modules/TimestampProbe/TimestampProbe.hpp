#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 时间戳延迟标定模块，用于测量上下位机通信延迟。接收上位机发来的 ts_ping，立即附加 MCU 微秒时间戳后回传 ts_pong，同时周期性发送带时间戳的 AHRS 四元数数据 (ahrs_stamped)。 / Timestamp probe module for measuring communication latency between upper and lower computers. Receives ts_ping from host, immediately attaches MCU microsecond timestamp and echoes back as ts_pong, while also periodically forwarding timestamped AHRS quaternion data (ahrs_stamped).
constructor_args:
  - quaternion_topic_name: "ahrs_quaternion"
  - task_stack_depth: 2048
template_args: []
required_hardware: ramfs
depends: []
=== END MANIFEST === */
// clang-format on

#include "app_framework.hpp"
#include "libxr.hpp"
#include "transform.hpp"

/**
 * @brief 时间戳探测数据包 — Ping (上位机 → MCU)
 *
 * 上位机发送 sequence，MCU 收到后在 Pong 中回传。
 */
struct TsPing {
  uint32_t sequence;  ///< 序列号
};

/**
 * @brief 时间戳探测数据包 — Pong (MCU → 上位机)
 *
 * MCU 收到 Ping 后立即记录时间戳并回传。
 */
struct TsPong {
  uint32_t sequence;       ///< 来自 Ping 的序列号
  uint64_t mcu_timestamp;  ///< MCU 收到 Ping 时的微秒时间戳
};

/**
 * @brief 带时间戳的 AHRS 四元数数据 (MCU → 上位机)
 *
 * 在原始 ahrs_quaternion 数据基础上，附加 MCU 端的微秒时间戳，
 * 使上位机能够精确计算从 AHRS 更新到上位机接收之间的延迟。
 */
struct AhrsStamped {
  LibXR::Quaternion<float> quaternion;  ///< 四元数姿态
  uint64_t mcu_timestamp;               ///< AHRS Publish 时刻的 MCU 微秒时间戳
};

class TimestampProbe : public LibXR::Application {
 public:
  TimestampProbe(LibXR::HardwareContainer &hw, LibXR::ApplicationManager &app,
                 const char *quaternion_topic_name, uint32_t task_stack_depth)
      : quaternion_topic_name_(quaternion_topic_name), ts_ping_topic_("ts_ping", sizeof(TsPing)),
        ts_pong_topic_("ts_pong", sizeof(TsPong)),
        ahrs_stamped_topic_("ahrs_stamped", sizeof(AhrsStamped)),
        cmd_file_(LibXR::RamFS::CreateFile("ts_probe", CommandFunc, this)) {
    UNUSED(hw);

    hw.template FindOrExit<LibXR::RamFS>({"ramfs"})->Add(cmd_file_);

    /* 注册 ts_ping 回调: 收到上位机的 ping 后, 立即附加 MCU 时间戳回传 pong */
    void (*ping_cb_fun)(bool, TimestampProbe *, LibXR::RawData &) =
        [](bool, TimestampProbe *self, LibXR::RawData &data) {
          auto ping = reinterpret_cast<TsPing *>(data.addr_);
          TsPong pong;
          pong.sequence = ping->sequence;
          pong.mcu_timestamp =
              static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());
          self->ts_pong_topic_.Publish(pong);
          self->ping_count_++;
        };
    auto ping_cb = LibXR::Topic::Callback::Create(ping_cb_fun, this);
    ts_ping_topic_.RegisterCallback(ping_cb);

    /* 注册 ahrs_quaternion 回调: 收到 AHRS 更新后, 附加时间戳转发为 ahrs_stamped */

    thread_.Create(this, ThreadFunc, "ts_probe", task_stack_depth,
                   LibXR::Thread::Priority::MEDIUM);

    app.Register(*this);
  }

  static void ThreadFunc(TimestampProbe *self) {
    /* 等待 ahrs_quaternion topic 被创建 */
    auto quat_handle =
        LibXR::Topic::WaitTopic(self->quaternion_topic_name_, 10000);
    if (quat_handle == nullptr) {
      XR_LOG_ERROR("TimestampProbe: ahrs_quaternion topic not found");
      return;
    }

    LibXR::Topic quat_topic(quat_handle);
    LibXR::Quaternion<float> quat;

    /* 注册四元数回调 */
    void (*quat_cb_fun)(bool, TimestampProbe *, LibXR::RawData &) =
        [](bool, TimestampProbe *self, LibXR::RawData &data) {
          auto quat = reinterpret_cast<LibXR::Quaternion<float> *>(data.addr_);
          AhrsStamped stamped;
          stamped.quaternion = *quat;
          stamped.mcu_timestamp =
              static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());
          self->ahrs_stamped_topic_.Publish(stamped);
          self->ahrs_count_++;
        };
    auto quat_cb = LibXR::Topic::Callback::Create(quat_cb_fun, self);
    quat_topic.RegisterCallback(quat_cb);

    /* 线程保持运行 */
    while (true) {
      LibXR::Thread::Sleep(1000);
    }
  }

  void OnMonitor() override {}

 private:
  static int CommandFunc(TimestampProbe *self, int argc, char ** /*argv*/) {
    if (argc == 1) {
      LibXR::STDIO::Printf("TimestampProbe status:\r\n");
      LibXR::STDIO::Printf("  Ping received:  %lu\r\n", self->ping_count_);
      LibXR::STDIO::Printf("  AHRS forwarded: %lu\r\n", self->ahrs_count_);
      LibXR::STDIO::Printf("  MCU time(us):   %llu\r\n",
                           static_cast<uint64_t>(
                               LibXR::Timebase::GetMicroseconds()));
    } else {
      LibXR::STDIO::Printf("Usage: ts_probe\r\n");
    }
    return 0;
  }

  const char *quaternion_topic_name_;

  LibXR::Topic ts_ping_topic_;
  LibXR::Topic ts_pong_topic_;
  LibXR::Topic ahrs_stamped_topic_;

  uint32_t ping_count_ = 0;
  uint32_t ahrs_count_ = 0;

  LibXR::Thread thread_;
  LibXR::RamFS::File cmd_file_;
};
