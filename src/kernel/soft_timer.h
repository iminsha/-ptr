/*
 * kernel/soft_timer.h
 * 功能：软定时器管理（启动/停止/到期产生事件）。
 */
#ifndef __KERNEL_SOFT_TIMER_H__
#define __KERNEL_SOFT_TIMER_H__
#pragma once
#include "Data_rename.h"

typedef enum{
    STMR_MODE_ONESHOT = 0,//单次到期
    STMR_MODE_PERIODIC = 1 //周期到期
} stmr_mode_t;


/**
 * @brief 初始化软定时器模块
 */
void stmr_init(void);

/**
 * @brief 在 1ms 中断中调用，推进所有软定时器
 * @warning 必须每 1ms 调用一次（由硬件tick保证）
 */
void stmr_isr_1ms(void);

/**
 * @brief 启动/重启一个软定时器
 * @param id 定时器编号（0..N-1）
 * @param period_ms 周期/单次时长（ms）
 * @param mode 单次 or 周期
 * @return 1=成功，0=参数非法
 */
uint8_t stmr_start(uint8_t id, uint16_t period_ms, uint8_t mode);

/**
 * @brief 停止一个软定时器
 */
void stmr_stop(uint8_t id);

/**
 * @brief 查询是否在运行
 */
uint8_t stmr_is_running(uint8_t id);

/**
 * @brief 取出一个“到期的定时器ID”
 * @param out_id 输出到期的id
 * @return 1=取到，0=无到期
 * @note  主循环反复调用直到取空
 */
uint8_t stmr_fetch_expired(uint8_t* out_id);

/**
 * @brief 获取丢失的到期次数（到期队列满导致）
 */
uint16_t stmr_drop_count(void);

/**
 * @brief 清零丢失计数（测试用）
 */
void stmr_clear_drop_count(void);

#endif /* __KERNEL_SOFT_TIMER_H__ */