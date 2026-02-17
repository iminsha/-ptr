/*
 * drivers/ds1302.c
 * DS1302 时钟驱动实现。
 * 说明：读写时序严格沿用当前项目已验证可显示的写法。
 */

#include "ds1302.h"

/* ===== 寄存器地址（按用户给定映射） ===== */
#define DS1302_SEC_W      0x80
#define DS1302_MIN_W      0x82
#define DS1302_HOUR_W     0x84
#define DS1302_DATE_W     0x86
#define DS1302_MONTH_W    0x88
#define DS1302_DAY_W      0x8A
#define DS1302_YEAR_W     0x8C
#define DS1302_WP_W       0x8E

#define DS1302_SEC_R      0x81
#define DS1302_MIN_R      0x83
#define DS1302_HOUR_R     0x85
#define DS1302_DATE_R     0x87
#define DS1302_MONTH_R    0x89
#define DS1302_DAY_R      0x8B
#define DS1302_YEAR_R     0x8D

/**
 * @brief BCD 转十进制。
 * @param data_ BCD 数据。
 * @return 十进制值。
 */
unsigned char DS1302_BcdToDec(unsigned char data_)
{
    return (unsigned char)(((data_ >> 4) * 10) + (data_ & 0x0F));
}

/**
 * @brief 十进制转 BCD。
 * @param data_ 十进制数据。
 * @return BCD 值。
 */
unsigned char DS1302_DecToBcd(unsigned char data_)
{
    return (unsigned char)(((data_ / 10) << 4) | (data_ % 10));
}

/**
 * @brief 写 1 字节到 DS1302 总线（LSB first）。
 * @note 时序按当前项目可用写法：每位直接 SCLK 置 1 再置 0。
 */
static void ds1302_write_byte(unsigned char data_)
{
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        DS1302_IO = data_ & (0x01 << i);
        DS1302_SCLK = 1;
        DS1302_SCLK = 0;
    }
}

/**
 * @brief 读取 1 字节（旧接口命名，保留兼容）。
 * @param command 读命令字（奇地址）。
 * @return 读取到的数据字节。
 */
unsigned char DS_1302_ReadByte(unsigned char command)
{
    unsigned char i;
    unsigned char result = 0x00;

    /* 启动一次传输。 */
    DS1302_RST = 1;

    /* 1) 发送命令字，低位在前。 */
    for (i = 0; i < 8; i++)
    {
        DS1302_IO = command & (0x01 << i);
        DS1302_SCLK = 0;
        DS1302_SCLK = 1;
    }

    /* 2) 读取数据字节，低位在前。 */
    DS1302_IO = 0;
    for (i = 0; i < 8; i++)
    {
        DS1302_SCLK = 1;
        DS1302_SCLK = 0;
        if (DS1302_IO)
        {
            result |= (0x01 << i);
        }
    }

    /* 结束传输。 */
    DS1302_RST = 0;
    DS1302_IO = 0;
    return result;
}

/**
 * @brief 写寄存器（命令 + 数据）。
 */
void DS1302_WriteData(unsigned char command, unsigned char data_)
{
    DS1302_RST = 1;
    ds1302_write_byte(command);
    ds1302_write_byte(data_);
    DS1302_RST = 0;
}

/**
 * @brief 读寄存器（对 DS_1302_ReadByte 的封装）。
 */
unsigned char DS1302_ReadByte(unsigned char command)
{
    return DS_1302_ReadByte(command);
}

/**
 * @brief 设置写保护。
 * @param en 1=开启写保护，0=关闭写保护。
 */
void DS1302_SetWriteProtect(bit en)
{
    DS1302_WriteData(DS1302_WP_W, en ? 0x80 : 0x00);
}

/**
 * @brief 初始化 DS1302。
 * @note 保留当前时序逻辑，附加两项稳定性完善：
 * 1) 关闭写保护；2) 清除 CH 位防止停振。
 */
void DS1302_Init(void)
{
    unsigned char sec_reg;

    DS1302_SCLK = 0;
    DS1302_RST = 0;
    DS1302_IO = 0;

    DS1302_SetWriteProtect(0);

    /* CH=1 时秒振荡器停止，这里强制清零。 */
    sec_reg = DS1302_ReadByte(DS1302_SEC_R);
    if (sec_reg & 0x80)
    {
        sec_reg &= 0x7F;
        DS1302_WriteData(DS1302_SEC_W, sec_reg);
    }
}

/**
 * @brief 读取当前时间（输出十进制）。
 * @param t 输出结构体指针。
 */
void DS1302_ReadTime(DS1302_Time* t)
{
    unsigned char hour_reg;
    unsigned char hour_dec;

    if (t == 0)
    {
        return;
    }

    t->sec = DS1302_BcdToDec(DS1302_ReadByte(DS1302_SEC_R) & 0x7F);
    t->min = DS1302_BcdToDec(DS1302_ReadByte(DS1302_MIN_R));

    /* 小时兼容 12/24 小时制，统一转换为 24 小时制输出。 */
    hour_reg = DS1302_ReadByte(DS1302_HOUR_R);
    if (hour_reg & 0x80)
    {
        hour_dec = DS1302_BcdToDec(hour_reg & 0x1F);
        if (hour_reg & 0x20)
        {
            if (hour_dec < 12)
            {
                hour_dec += 12;
            }
        }
        else if (hour_dec == 12)
        {
            hour_dec = 0;
        }
        t->hour = hour_dec;
    }
    else
    {
        t->hour = DS1302_BcdToDec(hour_reg & 0x3F);
    }

    t->day = DS1302_BcdToDec(DS1302_ReadByte(DS1302_DATE_R));
    t->month = DS1302_BcdToDec(DS1302_ReadByte(DS1302_MONTH_R));
    t->week = DS1302_BcdToDec(DS1302_ReadByte(DS1302_DAY_R));
    t->year = DS1302_BcdToDec(DS1302_ReadByte(DS1302_YEAR_R));
}

/**
 * @brief 写入时间（输入十进制）。
 * @param t 输入结构体指针。
 */
void DS1302_SetTime(const DS1302_Time* t)
{
    if (t == 0)
    {
        return;
    }

    DS1302_SetWriteProtect(0);

    DS1302_WriteData(DS1302_SEC_W, DS1302_DecToBcd(t->sec) & 0x7F);
    DS1302_WriteData(DS1302_MIN_W, DS1302_DecToBcd(t->min));
    DS1302_WriteData(DS1302_HOUR_W, DS1302_DecToBcd(t->hour) & 0x3F);
    DS1302_WriteData(DS1302_DATE_W, DS1302_DecToBcd(t->day));
    DS1302_WriteData(DS1302_MONTH_W, DS1302_DecToBcd(t->month));
    DS1302_WriteData(DS1302_DAY_W, DS1302_DecToBcd(t->week));
    DS1302_WriteData(DS1302_YEAR_W, DS1302_DecToBcd(t->year));
}
