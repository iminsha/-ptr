#include "protocol_v1_service.h"
#include "../drivers/lcd1602.h"

/* 命令定义（V1） */
#define CMD_PING_REQ        0x01
#define CMD_MODE_SET_REQ    0x02
#define CMD_MOVE_TO_REQ     0x03
#define CMD_STOP_REQ        0x04
#define CMD_BEEP_REQ        0x05
#define CMD_CFG_GET_REQ     0x06
#define CMD_CFG_SET_REQ     0x07
#define CMD_STATUS_GET_REQ  0x08
/* 响应/事件命令（用于兼容回环或链路反向数据） */
#define CMD_PING_RSP        0x41
#define CMD_STATUS_RSP      0x48
#define CMD_CFG_RSP         0x49
#define CMD_ACK             0x50
#define CMD_NACK            0x51
#define CMD_STATUS_EVT      0x80
#define CMD_ERROR_EVT       0x81

/* 参数规则 */
#define RULE_NONE           0
#define RULE_MODE_0_2       1
#define RULE_U8_0_100       2
#define RULE_U8_1_5         3
#define RULE_CFG_SET_V1     4

/* 解码错误码 */
#define DEC_OK              0
#define DEC_ERR_VER         1
#define DEC_ERR_CMD         2
#define DEC_ERR_LEN         3
#define DEC_ERR_PARAM       4

typedef struct
{
    unsigned char cmd;
    unsigned char min_len;
    unsigned char max_len;
    unsigned char rule;
} CmdDef;

typedef struct
{
    unsigned char ok;
    unsigned char err;
    unsigned char cmd;
    unsigned char p0;
    unsigned char p1;
    unsigned char p2;
} DecodeResult;

/* 命令字典表：后续扩展命令仅需增加这里 */
static const CmdDef code g_cmd_defs[] =
{
    { CMD_PING_REQ,       0, 0, RULE_NONE      },
    { CMD_MODE_SET_REQ,   1, 1, RULE_MODE_0_2  },
    { CMD_MOVE_TO_REQ,    1, 1, RULE_U8_0_100  },
    { CMD_STOP_REQ,       0, 0, RULE_NONE      },
    { CMD_BEEP_REQ,       1, 1, RULE_U8_1_5    },
    { CMD_CFG_GET_REQ,    0, 0, RULE_NONE      },
    { CMD_CFG_SET_REQ,    3, 3, RULE_CFG_SET_V1},
    { CMD_STATUS_GET_REQ, 0, 0, RULE_NONE      },
    { CMD_PING_RSP,       4, 4, RULE_NONE      },
    { CMD_STATUS_RSP,     5, 5, RULE_NONE      },
    { CMD_CFG_RSP,        0, 16, RULE_NONE     },
    { CMD_ACK,            1, 1, RULE_NONE      },
    { CMD_NACK,           1, 1, RULE_NONE      },
    { CMD_STATUS_EVT,     5, 5, RULE_NONE      },
    { CMD_ERROR_EVT,      2, 2, RULE_NONE      }
};

/* 保存最近一帧，用于页面轮播 */
static UartFrame xdata g_last_frame;
static UartFrame xdata g_rx_frame;
static DecodeResult xdata g_last_dec;
static unsigned char idata g_has_frame = 0;

static char to_hex(unsigned char v)
{
    v &= 0x0F;
    if (v < 10)
    {
        return (char)('0' + v);
    }
    return (char)('A' + (v - 10));
}

static void lcd_clear(void)
{
    unsigned char col;
    for (col = 1; col <= 16; ++col)
    {
        LCD_ShowChar(1, col, ' ');
        LCD_ShowChar(2, col, ' ');
    }
}

static const CmdDef *find_cmd_def(unsigned char cmd)
{
    unsigned char i;
    unsigned char cnt;
    cnt = (unsigned char)(sizeof(g_cmd_defs) / sizeof(g_cmd_defs[0]));
    for (i = 0; i < cnt; ++i)
    {
        if (g_cmd_defs[i].cmd == cmd)
        {
            return &g_cmd_defs[i];
        }
    }
    return 0;
}

/* 按字典和规则进行解码，得到“命令语义” */
static void decode_frame(const UartFrame *frame, DecodeResult *out)
{
    const CmdDef *def;

    out->ok = 0;
    out->err = DEC_OK;
    out->cmd = frame->cmd;
    out->p0 = 0;
    out->p1 = 0;
    out->p2 = 0;

    /* 版本校验在串口协议层已完成，这里不再因版本字段拦截，避免内存抖动误报。 */

    def = find_cmd_def(frame->cmd);
    if (def == 0)
    {
        out->err = DEC_ERR_CMD;
        return;
    }

    if (frame->len < def->min_len || frame->len > def->max_len)
    {
        out->err = DEC_ERR_LEN;
        return;
    }

    if (def->rule == RULE_NONE)
    {
        out->ok = 1;
        return;
    }

    if (def->rule == RULE_MODE_0_2)
    {
        if (frame->payload[0] > 2)
        {
            out->err = DEC_ERR_PARAM;
            return;
        }
        out->p0 = frame->payload[0];
        out->ok = 1;
        return;
    }

    if (def->rule == RULE_U8_0_100)
    {
        if (frame->payload[0] > 100)
        {
            out->err = DEC_ERR_PARAM;
            return;
        }
        out->p0 = frame->payload[0];
        out->ok = 1;
        return;
    }

    if (def->rule == RULE_U8_1_5)
    {
        if (frame->payload[0] < 1 || frame->payload[0] > 5)
        {
            out->err = DEC_ERR_PARAM;
            return;
        }
        out->p0 = frame->payload[0];
        out->ok = 1;
        return;
    }

    if (def->rule == RULE_CFG_SET_V1)
    {
        out->p0 = frame->payload[0];
        out->p1 = frame->payload[1];
        out->p2 = frame->payload[2];
        out->ok = 1;
        return;
    }

    out->err = DEC_ERR_PARAM;
}

/* 显示语义页：第一行命令名，第二行参数或错误 */
static void render_sem_page(const DecodeResult *dec)
{
    char cmd_hex[3];

    cmd_hex[0] = to_hex((unsigned char)(dec->cmd >> 4));
    cmd_hex[1] = to_hex(dec->cmd);
    cmd_hex[2] = '\0';

    lcd_clear();

    if (!dec->ok)
    {
        LCD_ShowString(1, 1, "DECODE ERROR");
        if (dec->err == DEC_ERR_VER) LCD_ShowString(2, 1, "ERR:BAD_VER");
        else if (dec->err == DEC_ERR_CMD) LCD_ShowString(2, 1, "ERR:BAD_CMD");
        else if (dec->err == DEC_ERR_LEN) LCD_ShowString(2, 1, "ERR:BAD_LEN");
        else if (dec->err == DEC_ERR_PARAM) LCD_ShowString(2, 1, "ERR:BAD_PARAM");
        else LCD_ShowString(2, 1, "ERR:UNKNOWN");
        return;
    }

    if (dec->cmd == CMD_PING_REQ)
    {
        LCD_ShowString(1, 1, "CMD:PING_REQ");
        LCD_ShowString(2, 1, "NO PARAM");
        return;
    }
    if (dec->cmd == CMD_MODE_SET_REQ)
    {
        LCD_ShowString(1, 1, "CMD:MODE_SET");
        if (dec->p0 == 0) LCD_ShowString(2, 1, "MODE:AUTO");
        else if (dec->p0 == 1) LCD_ShowString(2, 1, "MODE:MANUAL");
        else LCD_ShowString(2, 1, "MODE:SAFE");
        return;
    }
    if (dec->cmd == CMD_MOVE_TO_REQ)
    {
        LCD_ShowString(1, 1, "CMD:MOVE_TO");
        LCD_ShowString(2, 1, "POS:000%");
        LCD_ShowChar(2, 5, (char)('0' + (dec->p0 / 100)));
        LCD_ShowChar(2, 6, (char)('0' + ((dec->p0 / 10) % 10)));
        LCD_ShowChar(2, 7, (char)('0' + (dec->p0 % 10)));
        return;
    }
    if (dec->cmd == CMD_STOP_REQ)
    {
        LCD_ShowString(1, 1, "CMD:STOP_REQ");
        LCD_ShowString(2, 1, "NO PARAM");
        return;
    }
    if (dec->cmd == CMD_BEEP_REQ)
    {
        LCD_ShowString(1, 1, "CMD:BEEP_REQ");
        LCD_ShowString(2, 1, "COUNT:0");
        LCD_ShowChar(2, 7, (char)('0' + dec->p0));
        return;
    }
    if (dec->cmd == CMD_CFG_GET_REQ)
    {
        LCD_ShowString(1, 1, "CMD:CFG_GET");
        LCD_ShowString(2, 1, "NO PARAM");
        return;
    }
    if (dec->cmd == CMD_CFG_SET_REQ)
    {
        LCD_ShowString(1, 1, "CMD:CFG_SET");
        LCD_ShowString(2, 1, "K/V RAW");
        return;
    }
    if (dec->cmd == CMD_STATUS_GET_REQ)
    {
        LCD_ShowString(1, 1, "CMD:STAT_GET");
        LCD_ShowString(2, 1, "NO PARAM");
        return;
    }
    if (dec->cmd == CMD_PING_RSP)
    {
        LCD_ShowString(1, 1, "CMD:PING_RSP");
        LCD_ShowString(2, 1, "RSP FRAME");
        return;
    }
    if (dec->cmd == CMD_STATUS_RSP)
    {
        LCD_ShowString(1, 1, "CMD:STAT_RSP");
        LCD_ShowString(2, 1, "RSP FRAME");
        return;
    }
    if (dec->cmd == CMD_CFG_RSP)
    {
        LCD_ShowString(1, 1, "CMD:CFG_RSP");
        LCD_ShowString(2, 1, "RSP FRAME");
        return;
    }
    if (dec->cmd == CMD_ACK)
    {
        LCD_ShowString(1, 1, "CMD:ACK");
        LCD_ShowString(2, 1, "RSP FRAME");
        return;
    }
    if (dec->cmd == CMD_NACK)
    {
        LCD_ShowString(1, 1, "CMD:NACK");
        LCD_ShowString(2, 1, "RSP FRAME");
        return;
    }
    if (dec->cmd == CMD_STATUS_EVT)
    {
        LCD_ShowString(1, 1, "CMD:STAT_EVT");
        LCD_ShowString(2, 1, "EVENT FRAME");
        return;
    }
    if (dec->cmd == CMD_ERROR_EVT)
    {
        LCD_ShowString(1, 1, "CMD:ERR_EVT");
        LCD_ShowString(2, 1, "EVENT FRAME");
        return;
    }

    LCD_ShowString(1, 1, "CMD:UNKNOWN");
    LCD_ShowString(2, 1, "CMD=0x");
    LCD_ShowString(2, 7, cmd_hex);
}

void ProtoV1_Init(void)
{
    g_has_frame = 0;
    lcd_clear();
}

void ProtoV1_Poll(void)
{
    Uart_ProtocolProcess();
    while (Uart_ProtocolGetFrame(&g_rx_frame))
    {
        g_last_frame = g_rx_frame;
        decode_frame(&g_last_frame, &g_last_dec);
        g_has_frame = 1;
        render_sem_page(&g_last_dec);
    }
}

void ProtoV1_Tick10ms(void)
{
    /* 当前版本仅显示语义，不做页面轮播 */
}
