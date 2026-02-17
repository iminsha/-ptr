/*
 * drivers/ds1302.h
 * DS1302 时钟驱动对外接口。
 */

#ifndef __DRIVERS_DS1302_H__
#define __DRIVERS_DS1302_H__

#include <REG52.H>

/* ===== 引脚定义（按硬件接线修改） ===== */
#ifndef DS1302_RST
sbit DS1302_RST = P3^5;   /* 片选/使能，置 1 开始一次传输 */
#endif

#ifndef DS1302_SCLK
sbit DS1302_SCLK = P3^6;  /* 时钟信号 */
#endif

#ifndef DS1302_IO
sbit DS1302_IO = P3^4;    /* 单线数据口 */
#endif

/* ===== 时间结构（全部使用十进制） ===== */
typedef struct
{
    unsigned char year;   /* 年：00~99 */
    unsigned char month;  /* 月：01~12 */
    unsigned char day;    /* 日：01~31 */
    unsigned char week;   /* 周：01~07 */
    unsigned char hour;   /* 时：00~23 */
    unsigned char min;    /* 分：00~59 */
    unsigned char sec;    /* 秒：00~59 */
} DS1302_Time;

/* ===== 底层接口（与旧项目风格兼容） ===== */
void DS1302_WriteData(unsigned char command, unsigned char data_);
unsigned char DS1302_ReadByte(unsigned char command);
unsigned char DS_1302_ReadByte(unsigned char Command);
unsigned char DS1302_BcdToDec(unsigned char data_);
unsigned char DS1302_DecToBcd(unsigned char data_);

/* ===== 高层接口 ===== */
void DS1302_Init(void);
void DS1302_SetWriteProtect(bit en);
void DS1302_ReadTime(DS1302_Time* t);
void DS1302_SetTime(const DS1302_Time* t);

#endif /* __DRIVERS_DS1302_H__ */
