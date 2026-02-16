/* kernel/tick.c */
#include "tick.h"

/* Keil C51 / 8051 寄存器定义头文件：
 * - STC89C52 常用 REG52.H
 * - AT89S52 也兼容
 * 如果你工程里用的是 reg51.h / REG51.H，请按工程实际替换。
 */
#include <REG52.H>

/* 32-bit 毫秒计数：只能在 tick_isr_1ms() 中自增 */
static volatile uint32_t s_tick_ms = 0;

/* 进入/退出临界区：保护 32-bit 读取 */
static uint8_t critical_enter(void)
{
    uint8_t ea = EA;   /* 保存中断使能状态 */
    EA = 0;            /* 关总中断 */
    return ea;
}

static void critical_exit(uint8_t ea_prev)
{
    EA = ea_prev;      /* 恢复中断使能状态 */
}

void tick_init(void)
{
    uint8_t ea_prev = critical_enter();
    s_tick_ms = 0;
    critical_exit(ea_prev);
}

/* 必须由 1ms 定时器中断调用 */
void tick_isr_1ms(void)
{
    /* 极短：只做计数自增 */
    s_tick_ms++;
}

uint32_t tick_get_ms(void)
{
    uint32_t snap;
    uint8_t ea_prev = critical_enter();
    snap = s_tick_ms;  /* 原子快照 */
    critical_exit(ea_prev);
    return snap;
}

uint32_t tick_elapsed_since(uint32_t start)
{
    uint32_t now = tick_get_ms();
    /* unsigned 减法天然处理回绕 */
    return (uint32_t)(now - start);
}

uint8_t tick_has_elapsed(uint32_t start, uint32_t delta_ms)
{
    return (tick_elapsed_since(start) >= delta_ms) ? 1u : 0u;
}
