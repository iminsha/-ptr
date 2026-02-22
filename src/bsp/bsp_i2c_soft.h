/*
 * bsp/bsp_i2c_soft.h
 * 功能：软件 I2C 基础读写（给 AT24C02 用，也可复用到其他 I2C 器件）。
 */

#pragma once
#ifndef __BSP_I2C_SOFT_H_
#define __BSP_I2C_SOFT_H_

#include <REG52.H>

/* ===== I2C 引脚配置（按硬件修改）===== */
#ifndef I2C_SCL
sbit I2C_SCL = P2^1;
#endif
#ifndef I2C_SDA
sbit I2C_SDA = P2^0;
#endif
void startI2cSet();
void sendByteData(unsigned char Data);
bit receiveBitACKData();
unsigned char receiveByteData();
void sendBitACKData(bit Data);
void StopI2cSet();

#endif

