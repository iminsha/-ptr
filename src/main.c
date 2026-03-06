#include <REG52.H>
#include "drivers/lcd1602.h"
#include "services/protocol_v1_service.h"
#include "services/action_center.h"
#include "drivers/ds18b20.h"
#include "bsp/bsp_uart.h"
static ProtoV1Decoded xdata g_dec;
/*
 * 函数：DelayMS
 * 作用：粗略毫秒延时（基于11.0592MHz经验值）
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

void Display_Temp_On_LCD(int temp_x10)
{
    unsigned int abs_val;

    if (temp_x10 < 0)
    {
        abs_val = (unsigned int)(-temp_x10);
        LCD_ShowChar(2, 6, '-');
    }
    else
    {
        abs_val = (unsigned int)temp_x10;
        LCD_ShowChar(2, 6, '+');
    }

    if (abs_val > 999) abs_val = 999;

    LCD_ShowChar(2, 7, (char)('0' + (abs_val / 100 % 10)));
    LCD_ShowChar(2, 8, (char)('0' + (abs_val / 10 % 10)));
    LCD_ShowChar(2, 9, '.');
    LCD_ShowChar(2, 10, (char)('0' + (abs_val % 10)));
    LCD_ShowChar(2, 11, 'C');
}

/**
 * @brief 构建并发送温度状态数据帧
 * @param seq 帧序号
 */
void ActionCenter_SendTempFrame(unsigned char seq)
{
    unsigned char xdata payload[5];
    int temp = ActionCenter_GetTempX10();

    payload[0] = ActionCenter_GetMode();
    payload[1] = (unsigned char)(temp & 0xFF);
    payload[2] = (unsigned char)((temp >> 8) & 0xFF);
    payload[3] = ActionCenter_GetPos();
    payload[4] = ActionCenter_GetErr();

    Uart_ProtocolSendFrame(0x80, seq, payload, 5);
}

void main(void)
{
    unsigned int sample_timer_main = 0;

    LCD_Init();
    LCD_ShowString(1, 1, "PROTO MONITOR");
    LCD_ShowString(2, 1, "TEMP:+00.0C");

    UartInit();
    ProtoV1_Init();
    ActionCenter_Init();

    while (1)
    {
        ProtoV1_Poll();
        DelayMS(10);                 /* 粗略 10ms 节拍 */
        sample_timer_main++;
        /* 每10ms调用一次温度状态机 */
        ActionCenter_GetTempure();

        /* LCD刷新：先按1秒一次，更贴合你当前温度采样节奏 */
        if ((sample_timer_main % 100) == 0)
        {
            if (ActionCenter_IsTempValid())
            {
                Display_Temp_On_LCD(ActionCenter_GetTempX10());
            }
            else
            {
                LCD_ShowString(2, 1, "TEMP: ERROR    ");
            }
        }

        /* 发帧：先按2秒一次 */
        if ((sample_timer_main % 200) == 0)
        {
            if (ActionCenter_IsTempValid())
            {
                ActionCenter_SendTempFrame((unsigned char)(sample_timer_main / 200));
            }
        }
        //  if(ProtoV1_GetDecoded(&g_dec))
        // {
        //     ActionCenter_Execute(&g_dec);
        //     // ShowDecoded(&g_dec);
        // }
        /* 防止计数一直变大 */
        if (sample_timer_main >= 60000)
        {
            sample_timer_main = 0;
        }
    }
}