/*
 * kernel/tick.h
 * 功能：全局毫秒计数与 Tick 回调入口，供软定时器与节拍驱动。
 */

#pragma once

/*
 * kernel/tick.c
 * 内核 Tick 计时实现文件。
 */

#ifndef __KERNEL_TICK_H__
#define __KERNEL_TICK_H__


typedef unsigned char  uint8_t;
typedef unsigned int   uint16_t;
typedef unsigned long  uint32_t;


/**
 * @brief 初始化 tick 模块（清零计数、可选设置初值）
 * @note  不配置硬件定时器；硬件定时器在 bsp_timer 中配置
 */
void tick_init(void);

/**
 * @brief 1ms Tick ISR 回调入口
 * @note  必须由硬件定时器 1ms 中断调用（例如 Timer0 ISR 中调用）
 * @warning 只能在中断中调用；且必须保持极短
 */
void tick_isr_1ms(void);

/**
 * @brief 获取系统启动以来的毫秒计数（32-bit）
 * @return 当前毫秒数（会回绕）
 * @note  内部保证原子性（临界区读 32-bit）
 */
uint32_t tick_get_ms(void);

/**
 * @brief 计算从 start 到现在经过的毫秒数（处理回绕）
 * @param start 先前保存的 tick_get_ms()
 * @return 经过的毫秒数（0..0xFFFFFFFF）
 */
uint32_t tick_elapsed_since(uint32_t start);

/**
 * @brief 判断是否已经过去了 delta_ms
 * @param start 起始 tick
 * @param delta_ms 超时阈值
 * @return 1=已到期，0=未到期
 */
uint8_t tick_has_elapsed(uint32_t start, uint32_t delta_ms);

#endif /* __KERNEL_TICK_H__ */

