# mySmart 指令映射与解析设计说明（逐步完善版）

> 目标：先建立“统一语义层”，再把 TCP/串口/按键等输入统一映射到同一套动作模型，避免每加一个通道就重写一套业务逻辑。
>
> 当前阶段：上位机已切到 TCP 直连并能收发数据。本说明用于指导下一步“协议落地 + 解析稳定化”。

---

## 1. 总体设计原则

1. 传输与业务分离。
- 传输层只负责“字节如何到达”（TCP、串口）。
- 协议层只负责“如何切帧、校验、重组”。
- 语义层只负责“命令代表什么业务动作”。

2. 所有输入统一为 ActionRequest。
- 无论是 UI 按钮、TCP 命令、后续串口 CLI，最终都转换成同一种内部请求结构。

3. 所有输出统一为 ActionResult / StatusEvent。
- 设备回包统一成可解析的状态事件，UI 不直接依赖某个通道的原始文本。

4. 先小闭环，再扩展。
- 先跑通最小命令集（PING、MODE_SET、MOVE_TO、STOP、GET_CFG）。
- 再扩展到调度、告警、批量参数等。

---

## 2. 分层模型（建议作为后续实现边界）

### 2.1 Transport 层（传输层）
职责：字节收发，不理解业务。
- 输入：socket bytes
- 输出：raw bytes stream

### 2.2 Protocol 层（协议层）
职责：切帧、校验、重组。
- 输入：raw bytes stream
- 输出：Frame{cmd, seq, payload, checksum_ok}

### 2.3 Semantic 层（语义层）
职责：命令映射与参数验证。
- 输入：Frame
- 输出：ActionRequest 或 ErrorResponse

### 2.4 Action 层（动作层）
职责：执行动作，更新状态。
- 输入：ActionRequest
- 输出：ActionResult + StatusEvent

### 2.5 Presentation 层（UI层）
职责：显示状态、发起请求、显示日志。
- 输入：StatusEvent / ErrorResponse
- 输出：UI 更新

---

## 3. 指令映射关系（V1 版本）

> 注：这里先定义“语义名 + 参数 + 约束”，命令码可按你现有 `bsp_uart` 协议再绑定。

| 语义命令 | 方向 | 参数 | 约束 | 说明 |
|---|---|---|---|---|
| `PING` | UI -> DEV | 无 | 无 | 连通性探测，设备回 `PONG` |
| `MODE_SET` | UI -> DEV | `mode` | AUTO/MANUAL/SAFE | 切换系统模式 |
| `MOVE_TO` | UI -> DEV | `percent` | 0~100 | 执行器目标开度 |
| `STOP` | UI -> DEV | 无 | 无 | 立即停止当前动作 |
| `BEEP` | UI -> DEV | `count` | 1~5 | 蜂鸣提示 |
| `CFG_GET` | UI -> DEV | 无 | 无 | 拉取配置快照 |
| `CFG_SET` | UI -> DEV | `key,value` | key 在白名单 | 更新配置项 |
| `STATUS_GET` | UI -> DEV | 无 | 无 | 主动拉取当前状态 |
| `ACK` | DEV -> UI | `seq,code` | code=0 成功 | 对应请求回执 |
| `STATUS_PUSH` | DEV -> UI | 状态字段集 | 完整性校验 | 被动推送状态 |
| `ERROR_PUSH` | DEV -> UI | `err_code,msg` | err_code 标准化 | 主动异常通知 |

---

## 4. 参数与错误码设计建议

### 4.1 参数校验（必须在语义层完成）

- `mode`：只能为 `{AUTO, MANUAL, SAFE}`。
- `percent`：整数，范围 `[0,100]`。
- `count`：整数，范围 `[1,5]`。
- `CFG_SET`：key 必须白名单，value 必须类型匹配。

### 4.2 错误码（建议统一）

| code | 含义 | 场景 |
|---|---|---|
| `0` | OK | 执行成功 |
| `1` | BAD_CMD | 未知命令 |
| `2` | BAD_PARAM | 参数非法 |
| `3` | BUSY | 当前忙，暂不能执行 |
| `4` | FORBIDDEN | 当前模式不允许 |
| `5` | TIMEOUT | 执行超时 |
| `6` | INTERNAL | 内部错误 |
| `7` | CRC_FAIL | 帧校验失败 |

---

## 5. 推荐时序（请求/响应）

1. UI 发送请求，附带 `seq`。
2. 设备收到后先做语义校验。
3. 校验失败：立即 `ACK(seq, BAD_PARAM/BAD_CMD)`。
4. 校验通过：执行业务动作。
5. 动作结束后返回 `ACK(seq, OK)`。
6. 如状态发生变化，额外发 `STATUS_PUSH`。

说明：
- `ACK` 用于“这条命令有没有被接收并处理”。
- `STATUS_PUSH` 用于“系统当前状态是什么”。
- 两者不要混用，否则 UI 很难处理超时与重发。

---

## 6. 上位机解析流程（逐步完善建议）

### Step 1：输入缓冲与切帧
- 建立接收缓存区（环形或线性均可）。
- 每次 `readyRead` 追加数据后循环尝试解帧。
- 避免“只看一次 readAll”导致粘包/拆包问题。

### Step 2：协议校验
- 验证帧头、长度、checksum。
- 校验失败计数并打印错误日志（不要直接崩溃）。

### Step 3：语义分发
- 根据 `cmd` 进入分发表。
- 对 payload 做参数解析与范围校验。
- 生成 `ActionResult` 或错误码。

### Step 4：UI 更新
- `ACK` 更新发送状态（成功/失败）。
- `STATUS_PUSH` 更新界面控件。
- 原始日志保留 HEX，用于联调。

---

## 7. 日志规范（建议）

### 7.1 上行日志（设备 -> UI）
- `RX FRAME cmd=0x?? seq=? len=? crc=ok payload=...`
- `RX EVENT STATUS mode=AUTO temp=29.8 pos=70 err=0`

### 7.2 下行日志（UI -> 设备）
- `TX REQ seq=? MODE_SET AUTO`
- `TX REQ seq=? MOVE_TO 70`

### 7.3 错误日志
- `PROTO ERR crc_fail count=?`
- `SEM ERR bad_param cmd=MOVE_TO percent=255`

---

## 8. 最小闭环测试清单（先测这 6 条）

1. TCP 连接建立后发送 `PING`，收到 `ACK(OK)`。
2. 发送 `MODE_SET AUTO`，状态推送显示 mode 变化。
3. 发送 `MOVE_TO 70`，收到 `ACK(OK)` 且状态更新。
4. 发送 `MOVE_TO 255`，收到 `ACK(BAD_PARAM)`。
5. 发送 `CFG_GET`，收到配置快照。
6. 连续发送 20 条命令，验证无崩溃、无错序。

---

## 9. 后续扩展路线（建议顺序）

1. V1：文本命令 + ACK（你现在最容易先跑通）。
2. V2：切换到统一二进制帧（沿用 `55 AA` 协议）。
3. V3：事件推送、重发策略、心跳机制。
4. V4：调度/规则引擎命令化、远程配置持久化。

---

## 10. 本文档和代码的对应关系

- 协议底层：`src/bsp/bsp_uart.h`, `src/bsp/bsp_uart.c`
- 上位机收发入口：`pc_ui/src/mainwindow.cpp`
- 规则层接口：`src/services/rule_engine.h`
- 功能与架构总览：`src/docs/*`

---

## 11. 实施注意事项（避免走弯路）

1. 不要把业务逻辑写在 socket 回调里。
2. 不要让 UI 控件直接决定硬件细节参数。
3. 不要混用“文本协议”和“二进制协议”但没有版本标识。
4. 所有命令必须有可追踪的 `seq` 与 `ACK`。
5. 错误码优先于自由文本，便于自动化测试。

---

## 12. 阶段 2 实施入口（协议落地）

本阶段使用如下协议规范作为唯一实现依据：

- `src/docs/protocol/protocol_v1.md`

建议按以下顺序推进：

1. 先在上位机实现 `Frame Encode/Decode`（只做协议，不做业务）。
2. 再在 MCU 端将现有命令入口映射到 `CMD Map`。
3. 打通 `PING -> ACK` 最小回路。
4. 再打通 `MOVE_TO/MODE_SET/STOP/STATUS_GET`。
5. 最后补 `CFG_GET/CFG_SET` 和事件推送。

交付判定标准：

- UI 端日志能稳定看到 `RX FRAME` 与 `ACK/NACK`；
- 连续 20 条请求无乱序、无丢 ACK；
- 参数非法场景稳定返回 `NACK(BAD_PARAM)`。
