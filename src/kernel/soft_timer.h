/*
 * File: kernel/soft_timer.h
 * Brief: 软定时器管理接口
 */

#ifndef __KERNEL_SOFT_TIMER_H__
#define __KERNEL_SOFT_TIMER_H__

#include "Data_rename.h"

/* 软定时器模式 */
typedef enum {
    STMR_MODE_ONESHOT = 0,  /* 单次到期 */
    STMR_MODE_PERIODIC = 1  /* 周期到期 */
} stmr_mode_t;

/* 初始化软定时器模块（仅初始化内部状态） */
void stmr_init(void);

/* 在 1ms 中断中调用，推进所有软定时器 */
void stmr_isr_1ms(void);

/*
 * 将软定时器 1ms 推进函数挂接到 bsp_timer 的 1ms hook。
 * 返回值：1=挂接成功，0=挂接失败（如 hook 已满）
 */
uint8_t stmr_bind_timer0_1ms(void);

/* 启动/重启一个软定时器 */
uint8_t stmr_start(uint8_t id, uint16_t period_ms, uint8_t mode);

/* 停止一个软定时器 */
void stmr_stop(uint8_t id);

/* 查询定时器是否在运行 */
uint8_t stmr_is_running(uint8_t id);

/* 取出一个到期的定时器 ID */
uint8_t stmr_fetch_expired(uint8_t* out_id);

/* 获取到期队列溢出丢失计数 */
uint16_t stmr_drop_count(void);

/* 清零丢失计数 */
void stmr_clear_drop_count(void);

#endif /* __KERNEL_SOFT_TIMER_H__ */
