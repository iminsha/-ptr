#include <REG52.H>
#include "drivers/lcd1602.h"
#include "services/protocol_v1_service.h"
#include "services/action_center.h"

static ProtoV1Decoded xdata g_dec;

/*
 * 函数：DelayMS
 * 作用：粗略毫秒延时（基于11.0592MHz经验值）
 * 参数：
 * - ms: 延时毫秒数
 * 返回：无
 */
static void DelayMS(unsigned int ms)
{
    unsigned int i;
    unsigned int j;
    for (i = 0; i < ms; ++i)
    {
        for (j = 0; j < 114; ++j)
        {
        }
    }
}

/*
 * 函数：HexNibble
 * 作用：将4位数值转换为HEX字符。
 * 参数：
 * - v: 低4位有效
 * 返回：
 * - '0'~'9' 或 'A'~'F'
 */
static char HexNibble(unsigned char v)
{
    v &= 0x0F;
    if (v < 10)
    {
        return (char)('0' + v);
    }
    return (char)('A' + (v - 10));
}

/*
 * 函数：U8ToHex
 * 作用：将1字节转换为2位HEX文本。
 * 参数：
 * - v: 输入字节
 * - out2: 输出缓冲区，至少2字节
 * 返回：无
 */
static void U8ToHex(unsigned char v, char *out2)
{
    out2[0] = HexNibble((unsigned char)(v >> 4));
    out2[1] = HexNibble(v);
}

/*
 * 函数：LcdClear
 * 作用：清空LCD1602两行16列字符。
 * 参数：无
 * 返回：无
 */
static void LcdClear(void)
{
    unsigned char c;
    for (c = 1; c <= 16; ++c)
    {
        LCD_ShowChar(1, c, ' ');
        LCD_ShowChar(2, c, ' ');
    }
}

/*
 * 函数：ShowNameById
 * 作用：根据协议层输出的name_id显示命令名称。
 * 参数：
 * - name_id: 命令名称ID（PROTO_V1_NAME_xxx）
 * 返回：无
 */
static void ShowNameById(unsigned char name_id)
{
    if (name_id == PROTO_V1_NAME_PING_REQ) LCD_ShowString(1, 1, "PING_REQ");
    else if (name_id == PROTO_V1_NAME_MODE_SET) LCD_ShowString(1, 1, "MODE_SET");
    else if (name_id == PROTO_V1_NAME_MOVE_TO) LCD_ShowString(1, 1, "MOVE_TO");
    else if (name_id == PROTO_V1_NAME_STOP_REQ) LCD_ShowString(1, 1, "STOP_REQ");
    else if (name_id == PROTO_V1_NAME_BEEP_REQ) LCD_ShowString(1, 1, "BEEP_REQ");
    else if (name_id == PROTO_V1_NAME_CFG_GET) LCD_ShowString(1, 1, "CFG_GET");
    else if (name_id == PROTO_V1_NAME_CFG_SET) LCD_ShowString(1, 1, "CFG_SET");
    else if (name_id == PROTO_V1_NAME_STAT_GET) LCD_ShowString(1, 1, "STAT_GET");
    else if (name_id == PROTO_V1_NAME_PING_RSP) LCD_ShowString(1, 1, "PING_RSP");
    else if (name_id == PROTO_V1_NAME_STAT_RSP) LCD_ShowString(1, 1, "STAT_RSP");
    else if (name_id == PROTO_V1_NAME_CFG_RSP) LCD_ShowString(1, 1, "CFG_RSP");
    else if (name_id == PROTO_V1_NAME_ACK) LCD_ShowString(1, 1, "ACK");
    else if (name_id == PROTO_V1_NAME_NACK) LCD_ShowString(1, 1, "NACK");
    else if (name_id == PROTO_V1_NAME_STAT_EVT) LCD_ShowString(1, 1, "STAT_EVT");
    else if (name_id == PROTO_V1_NAME_ERR_EVT) LCD_ShowString(1, 1, "ERR_EVT");
    else LCD_ShowString(1, 1, "UNKNOWN");
}

/*
 * 函数：ShowDecoded
 * 作用：将协议层输出的解码结果显示到LCD。
 * 参数：
 * - dec: 解码结果指针
 * 返回：无
 */
static void ShowDecoded(const ProtoV1Decoded *dec)
{
    char hex2[3];

    LcdClear();
    ShowNameById(dec->name_id);

    if (dec->valid == 0)
    {
        if (dec->err == PROTO_V1_ERR_CMD_UNKNOWN)
        {
            U8ToHex(dec->cmd, hex2);
            hex2[2] = '\0';
            LCD_ShowString(2, 1, "UNK CMD 0x");
            LCD_ShowString(2, 10, hex2);
        }
        else
        {
            LCD_ShowString(2, 1, "ERR:LEN/RULE");
        }
        return;
    }

    /* 默认显示SEQ和LEN */
    LCD_ShowString(2, 1, "S00 L00");
    U8ToHex(dec->seq, hex2);
    LCD_ShowChar(2, 2, hex2[0]);
    LCD_ShowChar(2, 3, hex2[1]);
    U8ToHex(dec->len, hex2);
    LCD_ShowChar(2, 6, hex2[0]);
    LCD_ShowChar(2, 7, hex2[1]);

    /* 常用命令参数显示 */
    if (dec->name_id == PROTO_V1_NAME_MOVE_TO && dec->len >= 1)
    {
        LCD_ShowString(2, 1, "P:000%");
        LCD_ShowChar(2, 3, (char)('0' + (dec->p0 / 100)));
        LCD_ShowChar(2, 4, (char)('0' + ((dec->p0 / 10) % 10)));
        LCD_ShowChar(2, 5, (char)('0' + (dec->p0 % 10)));
    }
    else if (dec->name_id == PROTO_V1_NAME_MODE_SET && dec->len >= 1)
    {
        LCD_ShowString(2, 1, "MODE=0x00");
        U8ToHex(dec->p0, hex2);
        LCD_ShowChar(2, 8, hex2[0]);
        LCD_ShowChar(2, 9, hex2[1]);
    }
    else if ((dec->name_id == PROTO_V1_NAME_ACK || dec->name_id == PROTO_V1_NAME_NACK) && dec->len >= 1)
    {
        LCD_ShowString(2, 1, "CODE=0x00");
        U8ToHex(dec->p0, hex2);
        LCD_ShowChar(2, 8, hex2[0]);
        LCD_ShowChar(2, 9, hex2[1]);
    }
}

void main(void)
{
    LCD_Init();
    LCD_ShowString(1, 1, "PROTO MONITOR");
    LCD_ShowString(2, 1, "WAIT FRAME...");

    UartInit();
    ProtoV1_Init();
    ActionCenter_Init();

    while (1)
    {
        ProtoV1_Poll();
        while (ProtoV1_GetDecoded(&g_dec))
        {
            ActionCenter_Execute(&g_dec);
            ShowDecoded(&g_dec);
        }
        ProtoV1_Tick10ms();
        ActionCenter_Tick10ms();
        DelayMS(10);
    }
}
