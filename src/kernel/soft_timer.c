/*
 * File: kernel/soft_timer.c
 * Brief: 软定时器实现
 */

#include "soft_timer.h"
#include "..\bsp\bsp_timer.h"
#include <REG52.H>

/* ====== 可配置参数 ====== */
#define STMR_MAX       8   /* 支持的软定时器数量 */
#define STMR_EXPQ_CAP  8   /* 到期队列容量 */

/* ====== 软定时器控制块 ====== */
typedef struct {
    uint16_t remain;   /* 剩余 ms */
    uint16_t period;   /* 周期 ms（周期模式使用） */
    uint8_t  running;  /* 0/1 */
    uint8_t  mode;     /* stmr_mode_t */
} stmr_cb_t;

static volatile stmr_cb_t s_tmr[STMR_MAX];

/* 到期队列：仅保存到期 timer ID */
static volatile uint8_t s_expq[STMR_EXPQ_CAP];
static volatile uint8_t s_qh = 0, s_qt = 0, s_qc = 0;
static volatile uint16_t s_drop = 0;

/* 临界区（保护主循环与中断并发访问） */
static uint8_t critical_enter(void)
{
    uint8_t ea = EA;
    EA = 0;
    return ea;
}

static void critical_exit(uint8_t ea_prev)
{
    EA = ea_prev;
}

/* ISR 内入队（ISR 中不再关中断） */
static void expq_push_isr(uint8_t id)
{
    if (s_qc >= STMR_EXPQ_CAP) {
        s_drop++;
        return;
    }

    s_expq[s_qh] = id;
    s_qh++;
    if (s_qh >= STMR_EXPQ_CAP) s_qh = 0;
    s_qc++;
}

void stmr_init(void)
{
    uint8_t i;
    uint8_t ea_prev = critical_enter();

    for (i = 0; i < STMR_MAX; i++) {
        s_tmr[i].remain  = 0;
        s_tmr[i].period  = 0;
        s_tmr[i].running = 0;
        s_tmr[i].mode    = STMR_MODE_ONESHOT;
    }

    s_qh = s_qt = s_qc = 0;
    s_drop = 0;

    critical_exit(ea_prev);
}

uint8_t stmr_bind_timer0_1ms(void)
{
    return (uint8_t)Timer0_1ms_RegisterHook(stmr_isr_1ms);
}

uint8_t stmr_start(uint8_t id, uint16_t period_ms, uint8_t mode)
{
    uint8_t ea_prev;

    if (id >= STMR_MAX) return 0;
    if (period_ms == 0) return 0;
    if (mode != STMR_MODE_ONESHOT && mode != STMR_MODE_PERIODIC) return 0;

    ea_prev = critical_enter();
    s_tmr[id].period  = period_ms;
    s_tmr[id].remain  = period_ms;
    s_tmr[id].mode    = mode;
    s_tmr[id].running = 1;
    critical_exit(ea_prev);

    return 1;
}

void stmr_stop(uint8_t id)
{
    uint8_t ea_prev;

    if (id >= STMR_MAX) return;

    ea_prev = critical_enter();
    s_tmr[id].running = 0;
    s_tmr[id].remain  = 0;
    critical_exit(ea_prev);
}

uint8_t stmr_is_running(uint8_t id)
{
    uint8_t v;
    uint8_t ea_prev;

    if (id >= STMR_MAX) return 0;

    ea_prev = critical_enter();
    v = s_tmr[id].running;
    critical_exit(ea_prev);

    return v;
}

/* 必须每 1ms 调用一次：由硬件定时器 ISR 或 hook 分发调用 */
void stmr_isr_1ms(void)
{
    uint8_t i;

    for (i = 0; i < STMR_MAX; i++)
    {
        if (!s_tmr[i].running) continue;

        if (s_tmr[i].remain > 0) {
            s_tmr[i].remain--;
        }

        if (s_tmr[i].remain == 0)
        {
            expq_push_isr(i);

            if (s_tmr[i].mode == STMR_MODE_PERIODIC) {
                s_tmr[i].remain = s_tmr[i].period;
            } else {
                s_tmr[i].running = 0;
            }
        }
    }
}

uint8_t stmr_fetch_expired(uint8_t* out_id)
{
    uint8_t ea_prev;

    if (out_id == 0) return 0;

    ea_prev = critical_enter();
    if (s_qc == 0) {
        critical_exit(ea_prev);
        return 0;
    }

    *out_id = s_expq[s_qt];
    s_qt++;
    if (s_qt >= STMR_EXPQ_CAP) s_qt = 0;
    s_qc--;

    critical_exit(ea_prev);
    return 1;
}

uint16_t stmr_drop_count(void)
{
    uint16_t v;
    uint8_t ea_prev = critical_enter();
    v = s_drop;
    critical_exit(ea_prev);
    return v;
}

void stmr_clear_drop_count(void)
{
    uint8_t ea_prev = critical_enter();
    s_drop = 0;
    critical_exit(ea_prev);
}

