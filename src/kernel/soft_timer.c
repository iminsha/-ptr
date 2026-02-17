/*
 * kernel/soft_timer.c
 * 内核软定时器实现文件。
 */

#include "soft_timer.h"
#include "soft_timer.h"
#include <REG52.H>

/* ====== 可配置参数 ====== */
#define STMR_MAX 8          /* 支持的软定时器数量 */
#define STMR_EXPQ_CAP 8     /* 到期队列容量（故意不大，便于测试溢出） */

/* ====== 软定时器控制块 ====== */
typedef struct {
    uint16_t remain;   /* 剩余ms */
    uint16_t period;   /* 周期ms（周期模式使用） */
    uint8_t  running;  /* 0/1 */
    uint8_t  mode;     /* stmr_mode_t */
} stmr_cb_t;

static volatile stmr_cb_t s_tmr[STMR_MAX];

/* 到期队列：只存到期的 id */
static volatile uint8_t s_expq[STMR_EXPQ_CAP];
static volatile uint8_t s_qh = 0, s_qt = 0, s_qc = 0;
static volatile uint16_t s_drop = 0;

/*
s_expq[]：队列存的是“到期的 timer id”

s_qh：head（写入位置）

s_qt：tail（取出位置）

s_qc：count（当前队列里有多少个到期id）

s_drop：如果队列满了，还想塞入到期id，就“丢掉”，并把这个计数加一

*/
/* 临界区 */
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

/*
EA 是 8051 的总中断开关。
队列和定时器控制块会被“主循环”和“中断”同时访问。
为了不出现“读一半被中断打断”导致数据错乱，需要：
主循环读写共享数据前：关中断  读写完成后：恢复原来的EA状态
*/

/* ISR内入队（不再关中断） */
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
/*  清零所以软定时器。
    开机清零 清除全部旧数据。
*/
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

/* 必须每1ms调用一次：在硬件定时器ISR里调用 */
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
            /* 到期：推送到期ID */
            expq_push_isr(i);

            if (s_tmr[i].mode == STMR_MODE_PERIODIC) {
                s_tmr[i].remain = s_tmr[i].period; /* 重新装载 */
            } else {
                s_tmr[i].running = 0;              /* 单次停止 */
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

