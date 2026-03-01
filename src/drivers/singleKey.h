/*
 * File: drivers/singleKey.h
 * Brief: K1~K4 独立按键扫描接口
 */

#ifndef __DRIVERS_SINGLEKEY_H__
#define __DRIVERS_SINGLEKEY_H__
#include <REG52.H>
sbit P3_1 = P3^1;
sbit P3_0 = P3^0;
sbit P3_2 = P3^2;
sbit P3_3 = P3^3;
/**
 * @brief 扫描独立按键并返回键值
 * @return 0=无按键；1~4=对应按键按下事件
 * @note 函数内部包含消抖与等待释放
 */
unsigned char Key(void);
void Delay(unsigned int xms);
#endif /* __DRIVERS_SINGLEKEY_H__ */
