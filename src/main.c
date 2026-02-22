#include <REG52.H>
#include "drivers/lcd1602.h"
#include "drivers/at24c02.h"

/**
 * @brief 简单毫秒延时。
 * @note
 * 1) 用于给 LCD 刷新和 EEPROM 写周期预留时间。
 * 2) 这是阻塞延时，仅用于当前裸机联调示例。
 */
static void DelayMs(unsigned int ms)
{
    unsigned int i, j;
    while (ms--)
    {
        for (i = 0; i < 110; i++)
        {
            for (j = 0; j < 5; j++)
            {
                ;
            }
        }
    }
}

/**
 * @brief 将 AT24C02 返回码转换成 LCD 可显示的短文本。
 * @param err AT24C02 驱动返回码。
 * @return 指向 16 字符以内的只读字符串。
 */
static char* At24ErrToText(unsigned char err)
{
    if (err == AT24C02_OK)          return "OK              ";
    if (err == AT24C02_ERR_PARAM)   return "ERR:PARAM       ";
    if (err == AT24C02_ERR_ACK)     return "ERR:ACK         ";
    if (err == AT24C02_ERR_TIMEOUT) return "ERR:TIMEOUT     ";
    if (err == AT24C02_ERR_RANGE)   return "ERR:RANGE       ";
    return "ERR:UNKNOWN     ";
}

void main(void)
{
    /* 计划写入 EEPROM 的测试字符串，不含结束符写入。 */
    unsigned char tx_data[6] = "LMCCZ";
    /* 从 EEPROM 读回的数据缓存，多留 1 字节用于字符串结束符。 */
    unsigned char rx_data[6];
    /* 记录 AT24C02 API 返回值，便于定位是写失败还是读失败。 */
    unsigned char ret;

    LCD_Init();
    LCD_ShowString(1, 1, "AT24C02 TEST    ");
    LCD_ShowString(2, 1, "INIT...         ");

    /* 当前 AT24C02 初始化是空实现，保留调用便于后续扩展。 */
    AT24C02_Init();

    /* Step1: 从地址 0 开始写入 5 个字节：L M C C Z。 */
    // ret = AT24C02_WriteBuffer(0, tx_data, 5);
    // if (ret != AT24C02_OK)
    // {
    //     LCD_ShowString(1, 1, "WRITE FAIL      ");
    //     LCD_ShowString(2, 1, At24ErrToText(ret));
    //     while (1)
    //     {
    //         DelayMs(200);
    //     }
    // }

    /* Step2: 从地址 0 开始读回 5 个字节用于比对显示。 */
    ret = AT24C02_ReadBuffer(0, rx_data, 5);
    if (ret != AT24C02_OK)
    {
        LCD_ShowString(1, 1, "READ FAIL       ");
        LCD_ShowString(2, 1, At24ErrToText(ret));
        while (1)
        {
            DelayMs(200);
        }
    }

    /* 补 '\0'，保证 LCD_ShowString 读取到正确结束位置。 */
    rx_data[5] = '\0';

    /* 最终联调显示：第1行固定标识，第2行显示 EEPROM 读回数据。 */
    LCD_ShowString(1, 1, "EEPROM->LCD OK  ");
    LCD_ShowString(2, 1, "                ");
    LCD_ShowString(2, 1, (char*)rx_data);

    while (1)
    {
        DelayMs(200);
    }
}
