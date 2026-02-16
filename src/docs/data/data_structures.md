# 核心数据结构（建议的“契约”，保证可维护）

## 设备模型（抽象统一入口）

Device {
  id: uint8
  type: enum { RTC, TEMP, STEPPER, BUZZER, UI, IR }
  status: bitfield
}

## 动作（Action）

Action {
  type: enum { MOVE_TO, MOVE_STEP, STOP, HOME, BEEP, LED_PATTERN, MODE_SET }
  arg1: int16
  arg2: int16
}

## 时间日程任务（ScheduleTask）

ScheduleTask {
  enable: uint8
  hour: uint8
  min: uint8
  action: Action
  repeat: enum { DAILY, ONCE, EVERY_N_MIN }  // 可裁剪
  interval_min: uint16  // repeat=EVERY_N_MIN时有效
}

## 温度规则（TempRule）

TempRule {
  enable: uint8
  th_high_x10: int16     // 温度*10，避免浮点
  th_low_x10: int16
  cooldown_ms: uint16
  on_high: Action
  on_recover: Action
}

## 配置存储镜像（ConfigBlob）

ConfigBlob {
  magic: 0xA55A
  version: uint8
  crc16: uint16
  tasks[N]
  temp_rule
  ir_map[M]          // 红外键->Action映射
  stepper_profile    // 步数/速度参数
}

评审意见（必须重视）：如果你不做 magic+version+crc，配置损坏时系统会“读到垃圾还当真”，这是嵌入式常见致命缺陷。
