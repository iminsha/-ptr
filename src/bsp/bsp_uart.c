#include "bsp_uart.h"

/*
 * ============================================================
 * 文件: bsp_uart.c
 * 作用:
 * 1) 提供基于串口中断的异步收发（主循环非阻塞）
 * 2) 提供环形缓冲，隔离 ISR 与业务层
 * 3) 提供基础协议解析器（帧头/长度/命令/校验）
 * 4) 为后续上位机通信预留“拉取式 + 回调式”两套接口
 *
 * 设计原则:
 * - ISR 只做“快路径”操作：搬运字节、推进指针、设置标志
 * - 协议解析放在主循环中做，避免 ISR 过重
 * - 涉及 TX/RX 指针共享数据时，短暂关串口中断 ES 做临界区保护
 * ============================================================
 */

/* ---------------- 编译期参数校验 ---------------- */
#if ((UART_RX_BUFFER_SIZE & (UART_RX_BUFFER_SIZE - 1)) != 0)
#error "UART_RX_BUFFER_SIZE must be power-of-two."
#endif

#if ((UART_TX_BUFFER_SIZE & (UART_TX_BUFFER_SIZE - 1)) != 0)
#error "UART_TX_BUFFER_SIZE must be power-of-two."
#endif

#if ((UART_FRAME_QUEUE_SIZE & (UART_FRAME_QUEUE_SIZE - 1)) != 0)
#error "UART_FRAME_QUEUE_SIZE must be power-of-two."
#endif

/*
 * 使用 2 的幂大小后，(index + 1) & MASK 即可完成回绕，
 * 比取模更轻量，适合 8051 这类资源受限平台。
 */
#define UART_RX_MASK               (UART_RX_BUFFER_SIZE - 1)
#define UART_TX_MASK               (UART_TX_BUFFER_SIZE - 1)
#define UART_FRAME_QUEUE_MASK      (UART_FRAME_QUEUE_SIZE - 1)

/* 帧固定开销 = SOF1 + SOF2 + VER + CMD + SEQ + LEN + CHECKSUM = 7 字节 */
#define UART_FRAME_OVERHEAD        7

/* 11.0592MHz 晶振、Timer1 模式2、SMOD=0 时，4800bps 的重装值 */
#define UART_TH1_RELOAD_4800       0xFA

/* ---------------- 串口收发缓冲区 ---------------- */
/*
 * volatile 的意义:
 * 这些变量会在 ISR 和主循环之间共享，必须禁止编译器缓存优化。
 */
/*
 * 内存分配说明（Keil C51）:
 * - DATA 仅 128B，当前模块对象较多，必须把大对象移出 DATA。
 * - 大对象（RX/TX 缓冲、协议帧）统一放到 xdata。
 * - 小状态变量放到 idata，尽量减少 DATA 段压力。
 */
/* 关键路径缓存放在内部RAM，避免无XRAM硬件上读到0xFF */
static volatile unsigned char idata s_rx_buf[UART_RX_BUFFER_SIZE];
static volatile unsigned char idata s_tx_buf[UART_TX_BUFFER_SIZE];

/* head: 写入位置；tail: 读取位置 */
static volatile unsigned char idata s_rx_head;
static volatile unsigned char idata s_rx_tail;
static volatile unsigned char idata s_tx_head;
static volatile unsigned char idata s_tx_tail;

/* 诊断标志:
 * - rx_overflow: 接收缓冲满，出现丢包
 * - tx_overflow: 发送缓冲满，发送请求被丢弃
 * - tx_hw_busy:  硬件发送器是否处于发送中
 */
static volatile unsigned char idata s_rx_overflow;
static volatile unsigned char idata s_tx_overflow;
static volatile unsigned char idata s_tx_hw_busy;

/* ---------------- 协议解析状态机 ---------------- */
typedef enum
{
    UART_PARSE_WAIT_SOF1 = 0,     /* 等待第1个帧头字节 0x55 */
    UART_PARSE_WAIT_SOF2,         /* 等待第2个帧头字节 0xAA */
    UART_PARSE_WAIT_VER,          /* 等待协议版本 VER */
    UART_PARSE_WAIT_CMD,          /* 等待命令 CMD */
    UART_PARSE_WAIT_SEQ,          /* 等待序号 SEQ */
    UART_PARSE_WAIT_LEN,          /* 等待长度 LEN */
    UART_PARSE_WAIT_PAYLOAD,      /* 接收 payload */
    UART_PARSE_WAIT_CHECKSUM      /* 等待校验字节 */
} uart_parse_state_t;

/* 当前正在解析的帧上下文 */
static uart_parse_state_t idata s_parse_state = UART_PARSE_WAIT_SOF1;
static UartFrame xdata s_parse_frame;
static unsigned char idata s_parse_index;   /* 当前 payload 写入下标 */
static unsigned char idata s_parse_sum;     /* 校验累加和 */

/* 解析完成后的帧队列（供上层拉取） */
static UartFrame xdata s_frame_queue[UART_FRAME_QUEUE_SIZE];
static unsigned char idata s_frame_q_head;
static unsigned char idata s_frame_q_tail;

/* 可选：上层回调（在主循环解析函数中触发，不在 ISR 触发） */
static uart_frame_handler_t idata s_frame_handler = 0;

/* ============================================================
 * 内部工具函数
 * ============================================================
 */

/*
 * 计算 TX 环形缓冲的剩余可用字节数。
 * 注意:
 * - 该函数假设调用方已进入临界区（已关 ES）
 * - 环形缓冲保留 1 字节作为“空/满判定间隔”
 */
static unsigned char uart_tx_free_locked(void)
{
    if (s_tx_head >= s_tx_tail)
    {
        return (unsigned char)(UART_TX_BUFFER_SIZE - (s_tx_head - s_tx_tail) - 1);
    }
    return (unsigned char)(s_tx_tail - s_tx_head - 1);
}

/*
 * 向 TX 环形缓冲压入 1 字节（不直接发硬件）。
 * 返回:
 * - 1: 成功
 * - 0: 缓冲已满（置 tx_overflow 标志）
 * 约束:
 * - 调用方需先关 ES，确保与 ISR 互斥
 */
static unsigned char uart_tx_push_locked(unsigned char byte)
{
    unsigned char idata next = (unsigned char)((s_tx_head + 1) & UART_TX_MASK);
    if (next == s_tx_tail)
    {
        s_tx_overflow = 1;
        return 0;
    }
    s_tx_buf[s_tx_head] = byte;
    s_tx_head = next;
    return 1;
}

/*
 * “踢”发送器:
 * 当硬件空闲且 TX 队列非空时，立即装载一个字节到 SBUF，
 * 后续字节由 TI 中断自动链式发送。
 * 约束:
 * - 调用方需先关 ES
 */
static void uart_kick_tx_locked(void)
{
    if (!s_tx_hw_busy && (s_tx_head != s_tx_tail))
    {
        s_tx_hw_busy = 1;
        SBUF = s_tx_buf[s_tx_tail];
        s_tx_tail = (unsigned char)((s_tx_tail + 1) & UART_TX_MASK);
    }
}

/*
 * 复位协议解析状态机，回到等待 SOF1。
 */
static void uart_reset_parser(void)
{
    s_parse_state = UART_PARSE_WAIT_SOF1;
    s_parse_index = 0;
    s_parse_sum = 0;
    s_parse_frame.ver = 0;
    s_parse_frame.cmd = 0;
    s_parse_frame.seq = 0;
    s_parse_frame.len = 0;
    s_parse_frame.checksum = 0;
}

/*
 * 将完整帧入队，并可选触发上层回调。
 * 队列满时策略: 丢弃“新帧”，保留旧帧不变。
 */
static void uart_queue_frame(const UartFrame *frame)
{
    unsigned char idata next = (unsigned char)((s_frame_q_head + 1) & UART_FRAME_QUEUE_MASK);

    if (next == s_frame_q_tail)
    {
        return;
    }

    s_frame_queue[s_frame_q_head] = *frame;
    s_frame_q_head = next;

    if (s_frame_handler != 0)
    {
        s_frame_handler(frame);
    }
}

/*
 * 喂入 1 字节到协议状态机。
 * 帧格式:
 *   [SOF1][SOF2][VER][CMD][SEQ][LEN][PAYLOAD...][CHECKSUM]
 * 校验:
 *   CHECKSUM = (VER + CMD + SEQ + LEN + 每个PAYLOAD字节) & 0xFF
 */
static void uart_parse_feed(unsigned char byte)
{
    switch (s_parse_state)
    {
    case UART_PARSE_WAIT_SOF1:
        if (byte == UART_FRAME_SOF1)
        {
            s_parse_state = UART_PARSE_WAIT_SOF2;
        }
        break;

    case UART_PARSE_WAIT_SOF2:
        if (byte == UART_FRAME_SOF2)
        {
            s_parse_state = UART_PARSE_WAIT_VER;
        }
        else if (byte == UART_FRAME_SOF1)
        {
            /*
             * 允许连续 0x55 快速重同步:
             * 例如流里出现 55 55 AA ...，第二个 55 仍可作为 SOF1。
             */
            s_parse_state = UART_PARSE_WAIT_SOF2;
        }
        else
        {
            uart_reset_parser();
        }
        break;

    case UART_PARSE_WAIT_VER:
        if (byte == UART_FRAME_VER)
        {
            s_parse_frame.ver = byte;
            s_parse_sum = byte;
            s_parse_state = UART_PARSE_WAIT_CMD;
        }
        else
        {
            uart_reset_parser();
        }
        break;

    case UART_PARSE_WAIT_CMD:
        s_parse_frame.cmd = byte;
        s_parse_sum = (unsigned char)(s_parse_sum + byte);
        s_parse_state = UART_PARSE_WAIT_SEQ;
        break;

    case UART_PARSE_WAIT_SEQ:
        s_parse_frame.seq = byte;
        s_parse_sum = (unsigned char)(s_parse_sum + byte);
        s_parse_state = UART_PARSE_WAIT_LEN;
        break;

    case UART_PARSE_WAIT_LEN:
        if (byte <= UART_FRAME_MAX_PAYLOAD)
        {
            s_parse_frame.len = byte;
            s_parse_index = 0;
            s_parse_sum = (unsigned char)(s_parse_sum + byte);
            if (s_parse_frame.len == 0)
            {
                s_parse_state = UART_PARSE_WAIT_CHECKSUM;
            }
            else
            {
                s_parse_state = UART_PARSE_WAIT_PAYLOAD;
            }
        }
        else
        {
            uart_reset_parser();
        }
        break;

    case UART_PARSE_WAIT_PAYLOAD:
        s_parse_frame.payload[s_parse_index++] = byte;
        s_parse_sum = (unsigned char)(s_parse_sum + byte);
        if (s_parse_index >= s_parse_frame.len)
        {
            s_parse_state = UART_PARSE_WAIT_CHECKSUM;
        }
        break;

    case UART_PARSE_WAIT_CHECKSUM:
        s_parse_frame.checksum = byte;
        if (s_parse_sum == byte)
        {
            uart_queue_frame(&s_parse_frame);
        }
        uart_reset_parser();
        break;

    default:
        uart_reset_parser();
        break;
    }
}

/* ============================================================
 * 对外接口实现（Driver 层）
 * ============================================================
 */

/*
 * 接口: UartInit
 * 说明:
 * - 配置串口为模式1（8位UART）
 * - 配置 Timer1 作为波特率发生器
 * - 初始化 RX/TX 缓冲与协议状态机
 * - 开启串口中断 ES（并默认打开总中断 EA）
 */
void UartInit(void)
{
    /* 模式1 + 允许接收 */
    SCON = 0x50;

    /* Timer1: 模式2，8位自动重装 */
    TMOD &= 0x0F;
    TMOD |= 0x20;

    /* 关闭倍速位 SMOD，使用标准公式 */
    PCON &= 0x7F;

#if (UART_BAUD_RATE == 4800)
    TH1 = UART_TH1_RELOAD_4800;
    TL1 = UART_TH1_RELOAD_4800;
#else
#error "Unsupported UART_BAUD_RATE in current implementation."
#endif

    TR1 = 1;

    /* 清中断标志，避免误触发 */
    RI = 0;
    TI = 0;

    /* 清空运行时状态 */
    s_rx_head = 0;
    s_rx_tail = 0;
    s_tx_head = 0;
    s_tx_tail = 0;
    s_rx_overflow = 0;
    s_tx_overflow = 0;
    s_tx_hw_busy = 0;

    s_frame_q_head = 0;
    s_frame_q_tail = 0;
    uart_reset_parser();

    ES = 1;
    EA = 1;
}

/*
 * 接口: Uart_SendByte
 * 说明:
 * - 非阻塞发送 1 字节
 * - 实际行为: 压入 TX 环形缓冲，由 ISR 发送
 * - 若缓冲满则丢弃并置 tx_overflow 标志
 */
void Uart_SendByte(unsigned char byte)
{
    unsigned char idata es_old;
    es_old = ES;
    ES = 0;
    (void)uart_tx_push_locked(byte);
    uart_kick_tx_locked();
    ES = es_old;
}

/*
 * 接口: Uart_SendBuffer
 * 说明:
 * - 依次调用 Uart_SendByte 发送 buffer
 * - 非阻塞，若中途缓冲满，后续字节可能被丢弃
 */
void Uart_SendBuffer(const unsigned char *buf, unsigned char len)
{
    unsigned char idata i;
    if (buf == 0)
    {
        return;
    }
    for (i = 0; i < len; i++)
    {
        Uart_SendByte(buf[i]);
    }
}

/*
 * 接口: Uart_SendString
 * 说明:
 * - 发送 '\0' 结尾字符串
 * - 不发送末尾 '\0'
 */
void Uart_SendString(const char *str)
{
    if (str == 0)
    {
        return;
    }
    while (*str != '\0')
    {
        Uart_SendByte((unsigned char)(*str));
        str++;
    }
}

/*
 * 接口: Uart_ReadByte
 * 说明:
 * - 从 RX 环形缓冲读取 1 字节
 * 返回:
 * - 1: 成功读取
 * - 0: 缓冲为空或传入空指针
 */
unsigned char Uart_ReadByte(unsigned char *byte)
{
    unsigned char idata es_old;
    if (byte == 0)
    {
        return 0;
    }

    es_old = ES;
    ES = 0;
    if (s_rx_head == s_rx_tail)
    {
        ES = es_old;
        return 0;
    }

    *byte = s_rx_buf[s_rx_tail];
    s_rx_tail = (unsigned char)((s_rx_tail + 1) & UART_RX_MASK);
    ES = es_old;
    return 1;
}

/*
 * 接口: Uart_RxAvailable
 * 说明: 查询 RX 缓冲是否有数据可读
 * 返回: 1 有数据 / 0 无数据
 */
unsigned char Uart_RxAvailable(void)
{
    unsigned char idata es_old;
    unsigned char idata available;

    es_old = ES;
    ES = 0;
    available = (s_rx_head != s_rx_tail) ? 1 : 0;
    ES = es_old;
    return available;
}

/*
 * 接口: Uart_TxIdle
 * 说明: 查询发送路径是否完全空闲
 * 判定条件:
 * - TX 环形缓冲为空
 * - 硬件发送器不忙
 */
unsigned char Uart_TxIdle(void)
{
    unsigned char idata es_old;
    unsigned char idata idle;

    es_old = ES;
    ES = 0;
    idle = ((s_tx_head == s_tx_tail) && (!s_tx_hw_busy)) ? 1 : 0;
    ES = es_old;
    return idle;
}

/*
 * 接口: Uart_RxOverflowed / Uart_ClearRxOverflow
 * 说明: 读取/清除 RX 溢出标志
 */
unsigned char Uart_RxOverflowed(void)
{
    return s_rx_overflow;
}

void Uart_ClearRxOverflow(void)
{
    s_rx_overflow = 0;
}

/*
 * 接口: Uart_TxOverflowed / Uart_ClearTxOverflow
 * 说明: 读取/清除 TX 溢出标志
 */
unsigned char Uart_TxOverflowed(void)
{
    return s_tx_overflow;
}

void Uart_ClearTxOverflow(void)
{
    s_tx_overflow = 0;
}

/* ============================================================
 * 对外接口实现（Protocol 层）
 * ============================================================
 */

/*
 * 接口: Uart_ProtocolProcess
 * 说明:
 * - 建议在主循环高频调用
 * - 将 RX 缓冲中的全部字节喂给状态机
 * - 解析成功后帧会进入 frame queue，并触发可选回调
 */
void Uart_ProtocolProcess(void)
{
    unsigned char idata byte;
    while (Uart_ReadByte(&byte))
    {
        uart_parse_feed(byte);
    }
}

/*
 * 接口: Uart_ProtocolGetFrame
 * 说明:
 * - 从解析完成队列中取出 1 帧
 * 返回:
 * - 1: 成功取到
 * - 0: 队列为空或入参为空
 */
unsigned char Uart_ProtocolGetFrame(UartFrame *frame)
{
    if (frame == 0)
    {
        return 0;
    }

    if (s_frame_q_head == s_frame_q_tail)
    {
        return 0;
    }

    *frame = s_frame_queue[s_frame_q_tail];
    s_frame_q_tail = (unsigned char)((s_frame_q_tail + 1) & UART_FRAME_QUEUE_MASK);
    return 1;
}

/*
 * 接口: Uart_ProtocolSendFrame
 * 说明:
 * - 将 cmd/payload 打包为协议帧并发送
 * - 为避免“半帧入队”，先检查 TX 可用空间是否足够
 * 返回:
 * - 1: 帧已完整进入 TX 队列
 * - 0: 参数非法或空间不足
 */
unsigned char Uart_ProtocolSendFrame(unsigned char cmd,
                                     unsigned char seq,
                                     const unsigned char *payload,
                                     unsigned char len)
{
    unsigned char idata i;
    unsigned char idata checksum;
    unsigned char idata need_bytes;
    unsigned char idata es_old;

    if (len > UART_FRAME_MAX_PAYLOAD)
    {
        return 0;
    }

    if ((len > 0) && (payload == 0))
    {
        return 0;
    }

    checksum = UART_FRAME_VER;
    checksum = (unsigned char)(checksum + cmd);
    checksum = (unsigned char)(checksum + seq);
    checksum = (unsigned char)(checksum + len);
    for (i = 0; i < len; i++)
    {
        checksum = (unsigned char)(checksum + payload[i]);
    }

    need_bytes = (unsigned char)(UART_FRAME_OVERHEAD + len);

    es_old = ES;
    ES = 0;
    if (uart_tx_free_locked() < need_bytes)
    {
        ES = es_old;
        return 0;
    }

    (void)uart_tx_push_locked(UART_FRAME_SOF1);
    (void)uart_tx_push_locked(UART_FRAME_SOF2);
    (void)uart_tx_push_locked(UART_FRAME_VER);
    (void)uart_tx_push_locked(cmd);
    (void)uart_tx_push_locked(seq);
    (void)uart_tx_push_locked(len);
    for (i = 0; i < len; i++)
    {
        (void)uart_tx_push_locked(payload[i]);
    }
    (void)uart_tx_push_locked(checksum);

    uart_kick_tx_locked();
    ES = es_old;
    return 1;
}

/*
 * 接口: Uart_RegisterFrameHandler
 * 说明:
 * - 注册上层帧回调（可选）
 * - 回调触发点: Uart_ProtocolProcess 中解析出有效帧时
 */
void Uart_RegisterFrameHandler(uart_frame_handler_t handler)
{
    s_frame_handler = handler;
}

/*
 * 接口: SendData（兼容接口）
 * 说明:
 * - 保留旧代码调用方式，内部等价于 Uart_SendByte
 */
void SendData(unsigned char byte)
{
    Uart_SendByte(byte);
}

/* ============================================================
 * 串口中断服务函数
 * ============================================================
 */

/*
 * 向量 4: 串口中断
 * 处理顺序:
 * 1) RI=1: 读取收到字节并写入 RX 缓冲
 * 2) TI=1: 发送完成后继续从 TX 缓冲取下一个字节
 */
void Uart_ISR(void) interrupt 4
{
    if (RI)
    {
        unsigned char idata rx_data;
        unsigned char idata next;

        /* 8051推荐顺序：先读SBUF，再清RI，避免极端时序下读错字节 */
        rx_data = SBUF;
        RI = 0;

        next = (unsigned char)((s_rx_head + 1) & UART_RX_MASK);
        if (next == s_rx_tail)
        {
            /* RX 满: 丢弃当前字节，并记录溢出。 */
            s_rx_overflow = 1;
        }
        else
        {
            s_rx_buf[s_rx_head] = rx_data;
            s_rx_head = next;
        }
    }

    if (TI)
    {
        TI = 0;
        if (s_tx_head != s_tx_tail)
        {
            SBUF = s_tx_buf[s_tx_tail];
            s_tx_tail = (unsigned char)((s_tx_tail + 1) & UART_TX_MASK);
            s_tx_hw_busy = 1;
        }
        else
        {
            s_tx_hw_busy = 0;
        }
    }
}
