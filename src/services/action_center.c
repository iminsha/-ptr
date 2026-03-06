#include "action_center.h"
#include "../bsp/bsp_uart.h"
#include "../drivers/buzzer.h"
#include "../drivers/ds18b20.h"
/* 请求命令 */
#define CMD_PING_REQ        0x01
#define CMD_MODE_SET_REQ    0x02
#define CMD_MOVE_TO_REQ     0x03
#define CMD_STOP_REQ        0x04
#define CMD_BEEP_REQ        0x05
#define CMD_CFG_GET_REQ     0x06
#define CMD_CFG_SET_REQ     0x07
#define CMD_STATUS_GET_REQ  0x08

/* 响应/事件命令 */
#define CMD_PING_RSP        0x41
#define CMD_STATUS_RSP      0x48
#define CMD_CFG_RSP         0x49
#define CMD_ACK             0x50
#define CMD_NACK            0x51
#define CMD_STATUS_EVT      0x80

/* ACK/NACK code */
#define ACK_CODE_OK         0x00
#define ACK_CODE_BAD_CMD    0x01
#define ACK_CODE_BAD_PARAM  0x02
#define ACK_CODE_FORBIDDEN  0x04

/* 设备状态镜像 */
static unsigned char idata g_mode = 1;          /* 0=AUTO 1=MANUAL 2=SAFE */
static unsigned char idata g_pos = 0;           /* 0~100 */
static unsigned char idata g_err = 0;           /* 错误码 */
static int xdata g_temp10 = 250;                /* 温度*10 */
static unsigned long xdata g_uptime_10ms = 0;   /* 10ms节拍计数 */

/* 配置镜像 */
static unsigned char idata g_cfg_default_mode = 1;
static int xdata g_cfg_th = 300;
static int xdata g_cfg_tl = 280;
static unsigned int xdata g_cfg_cd = 30;

/*
 * 函数：send_ack
 * 作用：发送ACK或NACK。
 * 参数：
 * - seq: 请求序号
 * - ok:  1发送ACK，0发送NACK
 * - code: 错误码/成功码
 * 返回：无
 */
static void send_ack(unsigned char seq, bit ok, unsigned char ack_code)
{
    unsigned char p[1];
    p[0] = ack_code;
    if (ok)
    {
        (void)Uart_ProtocolSendFrame(CMD_ACK, seq, p, 1);
    }
    else
    {
        (void)Uart_ProtocolSendFrame(CMD_NACK, seq, p, 1);
    }
}

/*
 * 函数：send_status
 * 作用：发送状态响应/事件。
 * 参数：
 * - cmd: CMD_STATUS_RSP 或 CMD_STATUS_EVT
 * - seq: 关联序号
 * 返回：无
 */
static void send_status(unsigned char cmd, unsigned char seq)
{
    unsigned char p[5];
    p[0] = g_mode;
    p[1] = (unsigned char)(g_temp10 & 0xFF);
    p[2] = (unsigned char)((g_temp10 >> 8) & 0xFF);
    p[3] = g_pos;
    p[4] = g_err;
    (void)Uart_ProtocolSendFrame(cmd, seq, p, 5);
}

/*
 * 函数：send_ping_rsp
 * 作用：发送PING响应（携带秒级运行时间）。
 * 参数：
 * - seq: 请求序号
 * 返回：无
 */
static void send_ping_rsp(unsigned char seq)
{
    unsigned long sec;
    unsigned char p[4];
    sec = g_uptime_10ms / 100UL;
    p[0] = (unsigned char)(sec & 0xFF);
    p[1] = (unsigned char)((sec >> 8) & 0xFF);
    p[2] = (unsigned char)((sec >> 16) & 0xFF);
    p[3] = (unsigned char)((sec >> 24) & 0xFF);
    (void)Uart_ProtocolSendFrame(CMD_PING_RSP, seq, p, 4);
}

/*
 * 函数：send_cfg_rsp
 * 作用：发送配置响应（简化版）。
 * 参数：
 * - seq: 请求序号
 * 返回：无
 */
static void send_cfg_rsp(unsigned char seq)
{
    unsigned char p[7];
    p[0] = (unsigned char)(g_cfg_th & 0xFF);
    p[1] = (unsigned char)((g_cfg_th >> 8) & 0xFF);
    p[2] = (unsigned char)(g_cfg_tl & 0xFF);
    p[3] = (unsigned char)((g_cfg_tl >> 8) & 0xFF);
    p[4] = (unsigned char)(g_cfg_cd & 0xFF);
    p[5] = (unsigned char)((g_cfg_cd >> 8) & 0xFF);
    p[6] = g_cfg_default_mode;
    (void)Uart_ProtocolSendFrame(CMD_CFG_RSP, seq, p, 7);
}

void ActionCenter_Init(void)
{
    g_mode = 1;
    g_pos = 0;
    g_err = 0;
    g_temp10 = 250;
    g_uptime_10ms = 0;

    g_cfg_default_mode = 1;
    g_cfg_th = 300;
    g_cfg_tl = 280;
    g_cfg_cd = 30;

    Buzzer_Init();
}

void ActionCenter_Execute(const ProtoV1Decoded *dec)
{
    unsigned char cfg_key;
    int cfg_val;

    if (dec == 0)
    {
        return;
    }

    /* 仅处理请求命令（0x00~0x3F），响应/事件命令不再二次处理 */
    if (dec->cmd > 0x3F)
    {
        return;
    }

    /* 解码失败时由动作中心统一回NACK */
    if (!dec->valid)
    {
        if (dec->err == PROTO_V1_ERR_CMD_UNKNOWN)
        {
            send_ack(dec->seq, 0, ACK_CODE_BAD_CMD);
        }
        else
        {
            send_ack(dec->seq, 0, ACK_CODE_BAD_PARAM);
        }
        return;
    }

    if (dec->cmd == CMD_PING_REQ)
    {
        send_ack(dec->seq, 1, ACK_CODE_OK);
        send_ping_rsp(dec->seq);
        return;
    }

    if (dec->cmd == CMD_MODE_SET_REQ)
    {
        g_mode = dec->p0;
        send_ack(dec->seq, 1, ACK_CODE_OK);
        send_status(CMD_STATUS_EVT, dec->seq);
        return;
    }

    if (dec->cmd == CMD_MOVE_TO_REQ)
    {
        if (g_mode == 2)
        {
            send_ack(dec->seq, 0, ACK_CODE_FORBIDDEN);
            return;
        }
        g_pos = dec->p0;
        send_ack(dec->seq, 1, ACK_CODE_OK);
        send_status(CMD_STATUS_EVT, dec->seq);
        return;
    }

    if (dec->cmd == CMD_STOP_REQ)
    {
        g_pos = 0;
        Buzzer_Stop();
        send_ack(dec->seq, 1, ACK_CODE_OK);
        send_status(CMD_STATUS_EVT, dec->seq);
        return;
    }

    if (dec->cmd == CMD_BEEP_REQ)
    {
        if (dec->p0 == 1) Buzzer_Play(BUZZER_PATTERN_SHORT);
        else if (dec->p0 == 2) Buzzer_Play(BUZZER_PATTERN_DOUBLE);
        else if (dec->p0 == 3) Buzzer_Play(BUZZER_PATTERN_TRIPLE);
        else if (dec->p0 == 4) Buzzer_Play(BUZZER_PATTERN_LONG);
        else Buzzer_Play(BUZZER_PATTERN_ALARM);
        send_ack(dec->seq, 1, ACK_CODE_OK);
        return;
    }

    if (dec->cmd == CMD_CFG_GET_REQ)
    {
        send_ack(dec->seq, 1, ACK_CODE_OK);
        send_cfg_rsp(dec->seq);
        return;
    }

    if (dec->cmd == CMD_CFG_SET_REQ)
    {
        cfg_key = dec->p0;
        cfg_val = (int)((unsigned int)dec->p1 | ((unsigned int)dec->p2 << 8));

        if (cfg_key == 1) g_cfg_th = cfg_val;
        else if (cfg_key == 2) g_cfg_tl = cfg_val;
        else if (cfg_key == 3) g_cfg_cd = (unsigned int)cfg_val;
        else if (cfg_key == 4) g_cfg_default_mode = (unsigned char)cfg_val;
        else
        {
            send_ack(dec->seq, 0, ACK_CODE_BAD_PARAM);
            return;
        }
        send_ack(dec->seq, 1, ACK_CODE_OK);
        return;
    }

    if (dec->cmd == CMD_STATUS_GET_REQ)
    {
        send_ack(dec->seq, 1, ACK_CODE_OK);
        send_status(CMD_STATUS_RSP, dec->seq);
        return;
    }

    send_ack(dec->seq, 0, ACK_CODE_BAD_CMD);
}

static int g_last_stable_temp = 0; // 缓存正确温度
static unsigned char g_temp_valid = 0;
static unsigned int idata sample_timer = 0;
void ActionCenter_GetTempure(void)
{
    int16_t temp_raw;

    sample_timer++;

    // 每 1 秒（100 * 10ms）执行一次完整的采样状态机
    if (sample_timer == 1) {
        // 第一步：触发转换
        DS18B20_StartConvert();
    }
    else if (sample_timer == 81) { 
        // 第二步：800ms 后读取数据，此时传感器一定转换完了
        if (DS18B20_ReadTempX10(&temp_raw) == DS18B20_OK) {
            g_last_stable_temp = (int)temp_raw;
            g_temp10 = (int)temp_raw;
            g_temp_valid = 1;
        } else {
            g_temp_valid = 0;
        }
    }
    
    if (sample_timer >= 100) sample_timer = 0;
}

// 供 LCD 直接调用的接口：直接返回缓存的最新正确温度
int ActionCenter_GetTempX10(void) { return g_last_stable_temp; }
unsigned char ActionCenter_IsTempValid(void) { return g_temp_valid; }
unsigned char ActionCenter_GetMode(void) { return g_mode; }
unsigned char ActionCenter_GetPos(void)  { return g_pos; }
unsigned char ActionCenter_GetErr(void)  { return g_err; }
