# Protocol V1 (TCP/UART Unified Frame)

本文件定义可直接实现的通信协议 V1，目标是：
- PC UI 与 MCU 使用同一帧格式。
- TCP 与 UART 仅作为传输介质，协议层保持一致。
- 支持请求/应答（ACK/NACK）与状态推送（EVENT）。

---

## 1. Frame Format

### 1.1 Byte Layout

```
+--------+--------+------+-----+------+---------+----------+
| SOF1   | SOF2   | VER  | CMD | SEQ  | LEN     | PAYLOAD  |
| 0x55   | 0xAA   | 1B   | 1B  | 1B   | 1B      | LEN bytes|
+--------+--------+------+-----+------+---------+----------+
                                         +------------------+
                                         | CHK (1B)         |
                                         +------------------+
```

### 1.2 Field Definitions

- `SOF1/SOF2`: 固定帧头 `0x55 0xAA`。
- `VER`: 协议版本，V1 固定 `0x01`。
- `CMD`: 命令字。
- `SEQ`: 请求序号，范围 `0x00~0xFF`，循环使用。
- `LEN`: payload 长度，范围 `0~64`（V1 建议上限）。
- `PAYLOAD`: 负载数据。
- `CHK`: 校验字节，计算方式见下。

### 1.3 Checksum Rule

V1 使用 8 位累加和校验：

```
CHK = (VER + CMD + SEQ + LEN + sum(PAYLOAD)) & 0xFF
```

接收端重算后与帧内 `CHK` 比较；不一致丢弃并计数。

---

## 2. CMD Map

> 约定：`0x00~0x3F` 为请求（REQ），`0x40~0x7F` 为响应（RSP），`0x80~0xBF` 为事件（EVT）。

| CMD | Name | Dir | Payload | Notes |
|---|---|---|---|---|
| `0x01` | `PING_REQ` | UI->DEV | none | 连通性探测 |
| `0x41` | `PING_RSP` | DEV->UI | `uptime_sec(u32)` | 可用于链路健康显示 |
| `0x02` | `MODE_SET_REQ` | UI->DEV | `mode(u8)` | 0=AUTO,1=MANUAL,2=SAFE |
| `0x03` | `MOVE_TO_REQ` | UI->DEV | `percent(u8)` | 0~100 |
| `0x04` | `STOP_REQ` | UI->DEV | none | 紧急停止当前动作 |
| `0x05` | `BEEP_REQ` | UI->DEV | `count(u8)` | 1~5 |
| `0x06` | `CFG_GET_REQ` | UI->DEV | none | 获取配置 |
| `0x07` | `CFG_SET_REQ` | UI->DEV | `key(u8)+value(i16)` | V1 简化为单 key |
| `0x08` | `STATUS_GET_REQ` | UI->DEV | none | 主动拉取状态 |
| `0x48` | `STATUS_RSP` | DEV->UI | `mode,temp10,pos,err` | 对应 `STATUS_GET_REQ` |
| `0x49` | `CFG_RSP` | DEV->UI | `th,tl,cd,default_mode` | 对应 `CFG_GET_REQ` |
| `0x50` | `ACK` | DEV->UI | `code(u8)` | 对应请求 `SEQ` |
| `0x51` | `NACK` | DEV->UI | `code(u8)` | 对应请求 `SEQ` |
| `0x80` | `STATUS_EVT` | DEV->UI | `mode,temp10,pos,err` | 周期/状态变化推送 |
| `0x81` | `ERROR_EVT` | DEV->UI | `err_code(u8)+ext(u8)` | 主动异常上报 |

---

## 3. Payload Contracts

### 3.1 `MODE_SET_REQ` (`CMD=0x02`)

- Payload: `mode(u8)`
- Allowed values:
  - `0`: AUTO
  - `1`: MANUAL
  - `2`: SAFE
- Return:
  - success -> `ACK(code=0)`
  - invalid mode -> `NACK(code=2)`

### 3.2 `MOVE_TO_REQ` (`CMD=0x03`)

- Payload: `percent(u8)`
- Allowed range: `0~100`
- Return:
  - success -> `ACK(code=0)`
  - out of range -> `NACK(code=2)`
  - forbidden in SAFE -> `NACK(code=4)`

### 3.3 `CFG_SET_REQ` (`CMD=0x07`)

- Payload: `key(u8) + value(i16, little-endian)`
- V1 key map:
  - `0x01`: TH (high threshold, x0.1 C)
  - `0x02`: TL (low threshold, x0.1 C)
  - `0x03`: CD (cooldown, second)
  - `0x04`: default_mode (0/1/2, 仅低字节有效)

### 3.4 `STATUS_RSP` / `STATUS_EVT`

- Payload layout:
  - `mode(u8)`
  - `temp10(i16, LE)`  // 例如 298 表示 29.8C
  - `pos(u8)`          // 0~100
  - `err(u8)`          // 错误码

---

## 4. ACK/NACK Rules

### 4.1 Correlation

- `ACK/NACK` 必须携带与请求相同的 `SEQ`。
- UI 通过 `SEQ` 完成请求匹配，不依赖文本日志匹配。

### 4.2 Error Code Set

| Code | Name | Meaning |
|---|---|---|
| `0` | `OK` | 成功 |
| `1` | `BAD_CMD` | 未知命令 |
| `2` | `BAD_PARAM` | 参数非法 |
| `3` | `BUSY` | 当前忙 |
| `4` | `FORBIDDEN` | 当前模式不允许 |
| `5` | `TIMEOUT` | 执行超时 |
| `6` | `INTERNAL` | 内部错误 |
| `7` | `CRC_FAIL` | 校验失败 |

---

## 5. Timeout & Retry (UI Side)

- 默认超时：`500 ms`
- 最大重试次数：`2`
- 重试条件：超时未收到 `ACK/NACK`
- 不重试条件：
  - 收到 `NACK(BAD_PARAM/BAD_CMD)`（属于逻辑错误）
  - 收到 `NACK(FORBIDDEN)`（属于模式限制）

---

## 6. Example Frames

### 6.1 PING_REQ

- `CMD=0x01`, `SEQ=0x10`, `LEN=0`
- CHK = `01 + 01 + 10 + 00 = 0x12`

```
55 AA 01 01 10 00 12
```

### 6.2 MOVE_TO_REQ(70%)

- `CMD=0x03`, `SEQ=0x11`, `LEN=1`, payload=`46`
- CHK = `01 + 03 + 11 + 01 + 46 = 0x5C`

```
55 AA 01 03 11 01 46 5C
```

### 6.3 ACK(OK)

- `CMD=0x50`, `SEQ=0x11`, `LEN=1`, payload=`00`
- CHK = `01 + 50 + 11 + 01 + 00 = 0x63`

```
55 AA 01 50 11 01 00 63
```

---

## 7. Implementation Checklist

1. 接收缓冲做流式解帧（处理粘包/拆包）。
2. 校验失败计数并丢弃，不影响主循环。
3. 所有 REQ 都回复 `ACK/NACK`。
4. UI 维护 `pending_seq -> request` 表做超时管理。
5. 先实现 `PING/MODE_SET/MOVE_TO/STOP/STATUS_GET` 五条闭环。

