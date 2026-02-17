#ifndef __BSP_ONEWIRE_H__
#define __BSP_ONEWIRE_H__

#include <REG52.H>
#include "src\kernel\Data_rename.h"

/* 1-Wire 数据脚，默认使用 P3.7，可按硬件改为其他 IO。 */
#ifndef ONEWIRE_DQ
sbit ONEWIRE_DQ = P3^7;
#endif

/**
 * @brief 发送 1-Wire 复位脉冲并检测 Presence。
 * @return 1: 检测到设备存在；0: 未检测到设备。
 */
uint8_t ow_reset_presence(void);

/**
 * @brief 写入 1 个 bit（LSB 方向由上层 byte 函数控制）。
 * @param b 待写 bit，0 或 1。
 */
void ow_write_bit(uint8_t b);

/**
 * @brief 读取 1 个 bit。
 * @return 读取到的 bit，0 或 1。
 */
uint8_t ow_read_bit(void);

/**
 * @brief 写入 1 个字节（LSB first）。
 * @param byte 待写字节。
 */
void ow_write_byte(uint8_t byte);

/**
 * @brief 读取 1 个字节（LSB first）。
 * @return 读取到的字节。
 */
uint8_t ow_read_byte(void);

#endif /* __BSP_ONEWIRE_H__ */
