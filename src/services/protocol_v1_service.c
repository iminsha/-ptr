#include "protocol_v1_service.h"
#include "../bsp/bsp_uart.h"

/* 协议命令定义（仅用于语义映射） */
#define CMD_PING_REQ        0x01
#define CMD_MODE_SET_REQ    0x02
#define CMD_MOVE_TO_REQ     0x03
#define CMD_STOP_REQ        0x04
#define CMD_BEEP_REQ        0x05
#define CMD_CFG_GET_REQ     0x06
#define CMD_CFG_SET_REQ     0x07
#define CMD_STATUS_GET_REQ  0x08
#define CMD_PING_RSP        0x41
#define CMD_STATUS_RSP      0x48
#define CMD_CFG_RSP         0x49
#define CMD_ACK             0x50
#define CMD_NACK            0x51
#define CMD_STATUS_EVT      0x80
#define CMD_ERROR_EVT       0x81

/* 参数校验规则 */
#define RULE_NONE           0
#define RULE_MODE_0_2       1
#define RULE_U8_0_100       2
#define RULE_U8_1_5         3
#define RULE_FIXED_LEN_3    4

/* 解码队列：仅保留1条，最大限度节省IDATA */
#define PROTO_DEC_QUEUE_SIZE 1
#define PROTO_DEC_QUEUE_MASK (PROTO_DEC_QUEUE_SIZE - 1)

/* 解析状态机 */
#define PS_WAIT_SOF1    0
#define PS_WAIT_SOF2    1
#define PS_WAIT_VER     2
#define PS_WAIT_CMD     3
#define PS_WAIT_SEQ     4
#define PS_WAIT_LEN     5
#define PS_WAIT_PAYLOAD 6
#define PS_WAIT_CHK     7

typedef struct
{
    unsigned char cmd;
    unsigned char name_id;
    unsigned char min_len;
    unsigned char max_len;
    unsigned char rule;
} CmdMapItem;

/* 命令映射表：仅用于解析层语义翻译 */
static CmdMapItem code g_cmd_map[] =
{
    { CMD_PING_REQ,       PROTO_V1_NAME_PING_REQ, 0, 0, RULE_NONE },
    { CMD_MODE_SET_REQ,   PROTO_V1_NAME_MODE_SET, 1, 1, RULE_MODE_0_2 },
    { CMD_MOVE_TO_REQ,    PROTO_V1_NAME_MOVE_TO,  1, 1, RULE_U8_0_100 },
    { CMD_STOP_REQ,       PROTO_V1_NAME_STOP_REQ, 0, 0, RULE_NONE },
    { CMD_BEEP_REQ,       PROTO_V1_NAME_BEEP_REQ, 1, 1, RULE_U8_1_5 },
    { CMD_CFG_GET_REQ,    PROTO_V1_NAME_CFG_GET,  0, 0, RULE_NONE },
    { CMD_CFG_SET_REQ,    PROTO_V1_NAME_CFG_SET,  3, 3, RULE_FIXED_LEN_3 },
    { CMD_STATUS_GET_REQ, PROTO_V1_NAME_STAT_GET, 0, 0, RULE_NONE },
    { CMD_PING_RSP,       PROTO_V1_NAME_PING_RSP, 4, 4, RULE_NONE },
    { CMD_STATUS_RSP,     PROTO_V1_NAME_STAT_RSP, 5, 5, RULE_NONE },
    { CMD_CFG_RSP,        PROTO_V1_NAME_CFG_RSP,  0, 32, RULE_NONE },
    { CMD_ACK,            PROTO_V1_NAME_ACK,      1, 1, RULE_NONE },
    { CMD_NACK,           PROTO_V1_NAME_NACK,     1, 1, RULE_NONE },
    { CMD_STATUS_EVT,     PROTO_V1_NAME_STAT_EVT, 5, 5, RULE_NONE },
    { CMD_ERROR_EVT,      PROTO_V1_NAME_ERR_EVT,  2, 2, RULE_NONE }
};

/* 解析器运行时变量（全部用内部RAM，避免xdata问题） */
static unsigned char idata g_ps = PS_WAIT_SOF1;
static unsigned char idata g_cmd = 0;
static unsigned char idata g_seq = 0;
static unsigned char idata g_len = 0;
static unsigned char idata g_idx = 0;
static unsigned char idata g_sum = 0;
static unsigned char idata g_p0 = 0;
static unsigned char idata g_p1 = 0;
static unsigned char idata g_p2 = 0;

/* 解码结果队列 */
static ProtoV1Decoded data g_dec_queue[PROTO_DEC_QUEUE_SIZE];
static unsigned char data g_dec_head = 0;
static unsigned char data g_dec_tail = 0;
static ProtoV1Decoded data g_dec_work;

/*
 * 函数：reset_parser
 * 作用：复位帧解析状态机。
 */
static void reset_parser(void)
{
    g_ps = PS_WAIT_SOF1;
    g_cmd = 0;
    g_seq = 0;
    g_len = 0;
    g_idx = 0;
    g_sum = 0;
    g_p0 = 0;
    g_p1 = 0;
    g_p2 = 0;
}

/*
 * 函数：find_cmd_map_index
 * 作用：查找命令映射表下标。
 */
static unsigned char find_cmd_map_index(unsigned char cmd, unsigned char *found)
{
    unsigned char i;
    unsigned char cnt;
    cnt = (unsigned char)(sizeof(g_cmd_map) / sizeof(g_cmd_map[0]));
    for (i = 0; i < cnt; ++i)
    {
        if (g_cmd_map[i].cmd == cmd)
        {
            *found = 1;
            return i;
        }
    }
    *found = 0;
    return 0;
}

/*
 * 函数：push_decoded
 * 作用：解码结果入队。
 * 返回：1成功，0队列满
 */
static unsigned char push_decoded(const ProtoV1Decoded *item)
{
    unsigned char next;
    if (PROTO_DEC_QUEUE_SIZE == 1)
    {
        /* 单槽队列：新结果覆盖旧结果 */
        g_dec_queue[0] = *item;
        g_dec_head = 1;
        g_dec_tail = 0;
        return 1;
    }

    next = (unsigned char)((g_dec_head + 1) & PROTO_DEC_QUEUE_MASK);
    if (next == g_dec_tail)
    {
        return 0;
    }
    g_dec_queue[g_dec_head] = *item;
    g_dec_head = next;
    return 1;
}

/*
 * 函数：decode_current_frame
 * 作用：把当前解析完成的帧字段翻译成语义结果。
 */
static void decode_current_frame(void)
{
    unsigned char idx;
    unsigned char found;
    unsigned char rule;

    g_dec_work.valid = 0;
    g_dec_work.err = PROTO_V1_ERR_NONE;
    g_dec_work.cmd = g_cmd;
    g_dec_work.name_id = PROTO_V1_NAME_UNKNOWN;
    g_dec_work.seq = g_seq;
    g_dec_work.len = g_len;
    g_dec_work.p0 = g_p0;
    g_dec_work.p1 = g_p1;
    g_dec_work.p2 = g_p2;

    idx = find_cmd_map_index(g_cmd, &found);
    if (!found)
    {
        g_dec_work.err = PROTO_V1_ERR_CMD_UNKNOWN;
        (void)push_decoded(&g_dec_work);
        return;
    }

    g_dec_work.name_id = g_cmd_map[idx].name_id;
    if (g_len < g_cmd_map[idx].min_len || g_len > g_cmd_map[idx].max_len)
    {
        g_dec_work.err = PROTO_V1_ERR_LEN_INVALID;
        (void)push_decoded(&g_dec_work);
        return;
    }

    rule = g_cmd_map[idx].rule;
    if (rule == RULE_MODE_0_2 && g_dec_work.p0 > 2)
    {
        g_dec_work.err = PROTO_V1_ERR_LEN_INVALID;
        (void)push_decoded(&g_dec_work);
        return;
    }
    if (rule == RULE_U8_0_100 && g_dec_work.p0 > 100)
    {
        g_dec_work.err = PROTO_V1_ERR_LEN_INVALID;
        (void)push_decoded(&g_dec_work);
        return;
    }
    if (rule == RULE_U8_1_5 && (g_dec_work.p0 < 1 || g_dec_work.p0 > 5))
    {
        g_dec_work.err = PROTO_V1_ERR_LEN_INVALID;
        (void)push_decoded(&g_dec_work);
        return;
    }

    g_dec_work.valid = 1;
    (void)push_decoded(&g_dec_work);
}

/*
 * 函数：feed_byte
 * 作用：喂入1字节到协议帧状态机。
 */
static void feed_byte(unsigned char b)
{
    switch (g_ps)
    {
    case PS_WAIT_SOF1:
        if (b == UART_FRAME_SOF1) g_ps = PS_WAIT_SOF2;
        break;

    case PS_WAIT_SOF2:
        if (b == UART_FRAME_SOF2) g_ps = PS_WAIT_VER;
        else if (b != UART_FRAME_SOF1) reset_parser();
        break;

    case PS_WAIT_VER:
        if (b == UART_FRAME_VER)
        {
            g_sum = b;
            g_ps = PS_WAIT_CMD;
        }
        else
        {
            reset_parser();
        }
        break;

    case PS_WAIT_CMD:
        g_cmd = b;
        g_sum = (unsigned char)(g_sum + b);
        g_ps = PS_WAIT_SEQ;
        break;

    case PS_WAIT_SEQ:
        g_seq = b;
        g_sum = (unsigned char)(g_sum + b);
        g_ps = PS_WAIT_LEN;
        break;

    case PS_WAIT_LEN:
        if (b <= 32)
        {
            g_len = b;
            g_sum = (unsigned char)(g_sum + b);
            g_idx = 0;
            g_ps = (g_len == 0) ? PS_WAIT_CHK : PS_WAIT_PAYLOAD;
        }
        else
        {
            reset_parser();
        }
        break;

    case PS_WAIT_PAYLOAD:
        if (g_idx < 32)
        {
            if (g_idx == 0) g_p0 = b;
            else if (g_idx == 1) g_p1 = b;
            else if (g_idx == 2) g_p2 = b;
            g_idx++;
            g_sum = (unsigned char)(g_sum + b);
            if (g_idx >= g_len)
            {
                g_ps = PS_WAIT_CHK;
            }
        }
        else
        {
            reset_parser();
        }
        break;

    case PS_WAIT_CHK:
        if (g_sum == b)
        {
            decode_current_frame();
        }
        reset_parser();
        break;

    default:
        reset_parser();
        break;
    }
}

void ProtoV1_Init(void)
{
    g_dec_head = 0;
    g_dec_tail = 0;
    reset_parser();
}

void ProtoV1_Poll(void)
{
    unsigned char b;
    while (Uart_ReadByte(&b))
    {
        feed_byte(b);
    }
}

unsigned char ProtoV1_GetDecoded(ProtoV1Decoded *out)
{
    if (out == 0)
    {
        return 0;
    }
    if (PROTO_DEC_QUEUE_SIZE == 1)
    {
        if (g_dec_head == 0)
        {
            return 0;
        }
        *out = g_dec_queue[0];
        g_dec_head = 0;
        return 1;
    }
    if (g_dec_head == g_dec_tail)
    {
        return 0;
    }
    *out = g_dec_queue[g_dec_tail];
    g_dec_tail = (unsigned char)((g_dec_tail + 1) & PROTO_DEC_QUEUE_MASK);
    return 1;
}

void ProtoV1_Tick10ms(void)
{
    /* 解析层当前无周期任务 */
}
