/*
 * bsp/bsp_timer.c
 * 板级硬件定时器底层实现文件�? */

#include "bsp_timer.h"

/* =========================================================
   【为什么设计成“hook列表”而不是在ISR里写死调用tick/soft_timer？�?   ---------------------------------------------------------
   - 你当前阶段可能只�?tick；后面又要加 buzzer、keypad、soft_timer�?   - 如果ISR写死调用顺序，你每加一个模块都要改ISR文件，维护很糟糕
   - 用hook注册机制：模块自己注册“我需要每1ms被调用”，Timer统一分发
   数据流如下：
     模块Init() -> Timer0_1ms_RegisterHook(模块的Task1ms)
     Timer0_ISR �?ms触发 -> 依次调用所有已注册hook
   ========================================================= */

/* 允许注册的hook数量：够用就行（51 RAM 紧张�?*/
#define T0_MAX_HOOKS  6

static t0_hook_t idata s_hooks[T0_MAX_HOOKS];
static unsigned char idata s_hook_cnt = 0;

void Timer0_1ms_ClearHooks(void)
{
    unsigned char i;
    EA = 0;
    s_hook_cnt = 0;
    for (i = 0; i < T0_MAX_HOOKS; i++)
        s_hooks[i] = 0;
    EA = 1;
}

bit Timer0_1ms_RegisterHook(t0_hook_t hook)
{
    if (hook == 0) return 0;

    EA = 0;
    if (s_hook_cnt >= T0_MAX_HOOKS)
    {
        EA = 1;
        return 0; /* hook满了，注册失�?*/
    }
    s_hooks[s_hook_cnt++] = hook;
    EA = 1;
    return 1;
}

void Timer0_1ms_Init(void)
{
    /* Timer0模式1�?6位） */
    TMOD &= 0xF0;
    TMOD |= 0x01;

    /* 预装重装�?*/
    TH0 = T0_1MS_RELOAD_TH;
    TL0 = T0_1MS_RELOAD_TL;

    /* 开启Timer0中断、启动Timer0 */
    ET0 = 1;
    TR0 = 1;
    // EA=1;
}

/* =========================================================
   【Timer0 中断服务函数�?   - 必须非常短：只做重装 + hook分发
   - hook里也必须非常短：严禁delay、严禁复杂循�?   ========================================================= */
void Timer0_ISR(void) interrupt 1
{
    unsigned char i;

    /* 重装：保证下一�?ms准时 */
    TH0 = T0_1MS_RELOAD_TH;
    TL0 = T0_1MS_RELOAD_TL;

    /* 依次调用已注册hook */
    for (i = 0; i < s_hook_cnt; i++)
    {
        /* 防御：避免空指针 */
        if (s_hooks[i])
            s_hooks[i]();
    }
}

