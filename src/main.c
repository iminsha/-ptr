#include <REG52.H>
#include "drivers/lcd1602.h"
#include "drivers/ds1302.h"

/**
 * @brief 简单毫秒延时（用于 LCD 刷新节流）。
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
 * @brief 在 LCD 上按两位十进制显示数字（不足补 0）。
 */
static void LCD_Show2(unsigned char line, unsigned char col, unsigned char v)
{
    LCD_ShowNum(line, col, v, 2);
}

void main(void)
{
    DS1302_Time t;

    LCD_Init();
    LCD_ShowString(1, 1, "DS1302 TEST     ");
    LCD_ShowString(2, 1, "INIT...         ");

    /* 初始化 RTC：关闭写保护并处理 CH 位。 */
    DS1302_Init();

    /* 测试写入时间：2026-02-17 16:41:00 */
    t.year = 26;
    t.month = 2;
    t.day = 17;
    t.week = 2;
    t.hour = 17;
    t.min = 02;
    t.sec = 0;
    DS1302_SetTime(&t);

    while (1)
    {
        /* 周期读取 RTC 并刷新到 LCD。 */
        DS1302_ReadTime(&t);

        /* 第1行：20YY-MM-DD */
        LCD_ShowString(1, 1, "20");
        LCD_Show2(1, 3, t.year);
        LCD_ShowChar(1, 5, '-');
        LCD_Show2(1, 6, t.month);
        LCD_ShowChar(1, 8, '-');
        LCD_Show2(1, 9, t.day);
        LCD_ShowString(1, 11, "     ");

        /* 第2行：HH:MM:SS */
        LCD_Show2(2, 1, t.hour);
        LCD_ShowChar(2, 3, ':');
        LCD_Show2(2, 4, t.min);
        LCD_ShowChar(2, 6, ':');
        LCD_Show2(2, 7, t.sec);
        LCD_ShowString(2, 9, "       ");

        DelayMs(50);
    }
}
