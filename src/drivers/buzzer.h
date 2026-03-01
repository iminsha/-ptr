/**
 * @file    buzzer.h
 * @brief   蜂鸣器非阻塞驱动接口定义
 * @details
 * - 本模块提供蜂鸣器的非阻塞播放能力。
 * - 业务侧通过 Buzzer_Play() 下发模式；
 * - 定时侧通过 Buzzer_Task100us()/Buzzer_Task1ms() 推进状态机。
 */

#ifndef __DRIVERS_BUZZER_H__
#define __DRIVERS_BUZZER_H__

#include <REG52.H>

/* ========================================================================== */
/* 硬件与配置常量                                                             */
/* ========================================================================== */

/**
 * @brief 蜂鸣器控制引脚
 * @note  当前默认映射到 P2.5；如硬件变更可在编译前覆盖此宏。
 */
#ifndef BUZZER_IO
sbit BUZZER_IO = P2^5;
#endif

/**
 * @brief 蜂鸣器有效电平
 * @value 1 高电平导通
 * @value 0 低电平导通
 */
#ifndef BUZZER_ACTIVE_LEVEL
#define BUZZER_ACTIVE_LEVEL 1
#endif

/** @brief 静音模式（停止播放） */
#define BUZZER_PATTERN_NONE      0u
/** @brief 短鸣模式 */
#define BUZZER_PATTERN_SHORT     1u
/** @brief 双鸣模式 */
#define BUZZER_PATTERN_DOUBLE    2u
/** @brief 三鸣模式 */
#define BUZZER_PATTERN_TRIPLE    3u
/** @brief 长鸣模式 */
#define BUZZER_PATTERN_LONG      4u
/** @brief 报警模式 */
#define BUZZER_PATTERN_ALARM     5u

/**
 * @brief 推荐调度节拍（单位：us）
 * @note  为保持 200/300/500us 半周期精度，推荐使用 100us 调度。
 */
#define BUZZER_TASK_TICK_US      100u

/* ========================================================================== */
/* 对外接口                                                                   */
/* ========================================================================== */

/**
 * @brief  初始化蜂鸣器模块
 * @return 无
 * @post   模块进入静音且空闲状态
 */
void Buzzer_Init(void);

/**
 * @brief  播放指定模式（非阻塞）
 * @param  pattern 模式编号，取值见 BUZZER_PATTERN_*。
 * @return 无
 * @note   新模式会覆盖当前播放状态。
 */
void Buzzer_Play(unsigned char pattern);

/**
 * @brief  立即停止播放并静音
 * @return 无
 */
void Buzzer_Stop(void);

/**
 * @brief  获取当前模式编号
 * @return 当前模式（BUZZER_PATTERN_*）
 */
unsigned char Buzzer_GetPattern(void);

/**
 * @brief  查询蜂鸣器是否处于忙状态
 * @retval 1 正在播放
 * @retval 0 空闲
 */
bit Buzzer_IsBusy(void);

/**
 * @brief  100us 周期任务入口（推荐）
 * @return 无
 * @note   建议在硬件定时中断中调用。
 */
void Buzzer_Task100us(void);

/**
 * @brief  1ms 周期任务入口（兼容）
 * @return 无
 * @note   内部会执行 10 次 100us 子步进。
 */
void Buzzer_Task1ms(void);

#endif /* __DRIVERS_BUZZER_H__ */

