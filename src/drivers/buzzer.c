/**
 * @file    buzzer.c
 * @brief   蜂鸣器非阻塞状态机实现
 * @details
 * 设计说明�? * 1. 采用“模式序�?+ 周期任务驱动”的非阻塞结构�? * 2. 主循环仅负责触发播放命令�? * 3. 实际时序推进�?100us 周期任务完成�? * 4. 保持既有音色参数�?00/300/500us 半周期）不变�? */

#include "buzzer.h"

/* ========================================================================== */
/* 时序参数定义（单位：100us Tick�?                                          */
/* ========================================================================== */

/** 500us 半周期对应的 100us Tick �?*/
#define HALF_500US_TICK100US   5u
/** 300us 半周期对应的 100us Tick �?*/
#define HALF_300US_TICK100US   3u
/** 200us 半周期对应的 100us Tick �?*/
#define HALF_200US_TICK100US   2u

/**
 * 毫秒转换�?100us Tick
 * 1ms = 10 × 100us
 */
#define MS_TO_TICK100US(ms)    ((unsigned int)((ms) * 10u))

/* ========================================================================== */
/* 数据结构定义                                                               */
/* ========================================================================== */

/**
 * @brief 蜂鸣段描述结�? *
 * 一个蜂鸣模式由多个蜂鸣段顺序拼接构成�? * 每个段可以是发声段或静音段�? */
typedef struct
{
    unsigned char on;              /* 1 = 发声段，0 = 静音�?*/
    unsigned int  duration_100us;  /* 段持续时间（单位�?00us�?*/
    unsigned int  half_100us;      /* 发声半周期（单位�?00us），静音段为 0 */
} buzzer_seg_t;

/* ========================================================================== */
/* 固定音效序列定义                                                           */
/* ========================================================================== */

/* 短音�?60ms@500us + 150ms 静音 */
static const buzzer_seg_t code s_seq_short[] =
{
    {1u, MS_TO_TICK100US(160u), HALF_500US_TICK100US},
    {0u, MS_TO_TICK100US(150u), 0u},
    {0u, 0u, 0u}
};

/* 双响�? × (130ms@300us + 120ms 静音) */
static const buzzer_seg_t code s_seq_double[] =
{
    {1u, MS_TO_TICK100US(130u), HALF_300US_TICK100US},
    {0u, MS_TO_TICK100US(120u), 0u},
    {1u, MS_TO_TICK100US(130u), HALF_300US_TICK100US},
    {0u, MS_TO_TICK100US(120u), 0u},
    {0u, 0u, 0u}
};

/* 三响�? × (90ms@200us + 90ms 静音) */
static const buzzer_seg_t code s_seq_triple[] =
{
    {1u, MS_TO_TICK100US(90u), HALF_200US_TICK100US},
    {0u, MS_TO_TICK100US(90u), 0u},
    {1u, MS_TO_TICK100US(90u), HALF_200US_TICK100US},
    {0u, MS_TO_TICK100US(90u), 0u},
    {1u, MS_TO_TICK100US(90u), HALF_200US_TICK100US},
    {0u, MS_TO_TICK100US(90u), 0u},
    {0u, 0u, 0u}
};

/* 长音�?50ms@500us + 200ms 静音 */
static const buzzer_seg_t code s_seq_long[] =
{
    {1u, MS_TO_TICK100US(650u), HALF_500US_TICK100US},
    {0u, MS_TO_TICK100US(200u), 0u},
    {0u, 0u, 0u}
};

/* 报警音：交替高低频共 8 �?+ 200ms 静音 */
static const buzzer_seg_t code s_seq_alarm[] =
{
    {1u, MS_TO_TICK100US(120u), HALF_200US_TICK100US},
    {1u, MS_TO_TICK100US(120u), HALF_500US_TICK100US},
    {1u, MS_TO_TICK100US(120u), HALF_200US_TICK100US},
    {1u, MS_TO_TICK100US(120u), HALF_500US_TICK100US},
    {1u, MS_TO_TICK100US(120u), HALF_200US_TICK100US},
    {1u, MS_TO_TICK100US(120u), HALF_500US_TICK100US},
    {1u, MS_TO_TICK100US(120u), HALF_200US_TICK100US},
    {1u, MS_TO_TICK100US(120u), HALF_500US_TICK100US},
    {0u, MS_TO_TICK100US(200u), 0u},
    {0u, 0u, 0u}
};

/* ========================================================================== */
/* 模块内部运行状态变�?                                                      */
/* ========================================================================== */

static unsigned char idata s_pattern = BUZZER_PATTERN_NONE; /* 当前播放模式 */
static bit s_busy = 0;                                 /* 1=播放中，0=空闲 */

static const buzzer_seg_t code* idata s_seq = 0;             /* 当前段序�?*/
static unsigned char idata s_seq_idx = 0;                    /* 当前段索�?*/

static bit s_seg_on = 0;                               /* 当前段类�?*/
static unsigned int idata s_seg_left_100us = 0;              /* 当前段剩余时�?*/

static unsigned int idata s_wave_half_100us = 0;             /* 半周�?*/
static unsigned int idata s_wave_left_100us = 0;             /* 半周期剩余计�?*/
static bit s_wave_level = 0;                           /* 当前输出电平 */

/* ========================================================================== */
/* 临界区控�?                                                                */
/* ========================================================================== */

static bit buzzer_critical_enter(void)
{
    bit ea_prev = EA;
    EA = 0;
    return ea_prev;
}

static void buzzer_critical_exit(bit ea_prev)
{
    EA = ea_prev;
}

/* ========================================================================== */
/* 硬件控制函数                                                               */
/* ========================================================================== */

/* 设置蜂鸣器输出电�?*/
static void buzzer_hw_set(bit on)
{
    if (on)
        BUZZER_IO = (BUZZER_ACTIVE_LEVEL ? 1 : 0);
    else
        BUZZER_IO = (BUZZER_ACTIVE_LEVEL ? 0 : 1);
}

/* 进入静音状态并清除方波参数 */
static void buzzer_enter_silence(void)
{
    s_wave_half_100us = 0u;
    s_wave_left_100us = 0u;
    s_wave_level = 0;
    buzzer_hw_set(0);
}

/* ========================================================================== */
/* 运行核心逻辑                                                               */
/* ========================================================================== */

static const buzzer_seg_t code* buzzer_get_seq(unsigned char pattern)
{
    if (pattern == BUZZER_PATTERN_SHORT)  return s_seq_short;
    if (pattern == BUZZER_PATTERN_DOUBLE) return s_seq_double;
    if (pattern == BUZZER_PATTERN_TRIPLE) return s_seq_triple;
    if (pattern == BUZZER_PATTERN_LONG)   return s_seq_long;
    if (pattern == BUZZER_PATTERN_ALARM)  return s_seq_alarm;
    return 0;
}

static void buzzer_load_segment(void)
{
    const buzzer_seg_t code* seg;

    if (s_seq == 0)
    {
        Buzzer_Stop();
        return;
    }

    seg = &s_seq[s_seq_idx];

    if (seg->duration_100us == 0u)
    {
        Buzzer_Stop();
        return;
    }

    s_seg_on = seg->on ? 1 : 0;
    s_seg_left_100us = seg->duration_100us;

    if (s_seg_on)
    {
        s_wave_half_100us = seg->half_100us;

        if (s_wave_half_100us == 0u)
        {
            Buzzer_Stop();
            return;
        }

        s_wave_level = 1;
        s_wave_left_100us = s_wave_half_100us;
        buzzer_hw_set(1);
    }
    else
    {
        buzzer_enter_silence();
    }
}

/* ========================================================================== */
/* 对外接口                                                                   */
/* ========================================================================== */

void Buzzer_Init(void)
{
    bit ea_prev = buzzer_critical_enter();

    s_pattern = BUZZER_PATTERN_NONE;
    s_busy = 0;
    s_seq = 0;
    s_seq_idx = 0;
    s_seg_on = 0;
    s_seg_left_100us = 0;

    buzzer_enter_silence();

    buzzer_critical_exit(ea_prev);
}

void Buzzer_Play(unsigned char pattern)
{
    const buzzer_seg_t code* seq = buzzer_get_seq(pattern);
    bit ea_prev;

    if (pattern == BUZZER_PATTERN_NONE || seq == 0)
    {
        Buzzer_Stop();
        return;
    }

    ea_prev = buzzer_critical_enter();

    s_pattern = pattern;
    s_busy = 1;
    s_seq = seq;
    s_seq_idx = 0;

    buzzer_load_segment();

    buzzer_critical_exit(ea_prev);
}

void Buzzer_Stop(void)
{
    bit ea_prev = buzzer_critical_enter();

    s_pattern = BUZZER_PATTERN_NONE;
    s_busy = 0;
    s_seq = 0;
    s_seq_idx = 0;
    s_seg_on = 0;
    s_seg_left_100us = 0;

    buzzer_enter_silence();

    buzzer_critical_exit(ea_prev);
}

unsigned char Buzzer_GetPattern(void)
{
    return s_pattern;
}

bit Buzzer_IsBusy(void)
{
    return s_busy;
}

/* 100us 周期任务（建议在定时器中断中调用�?*/
void Buzzer_Task100us(void)
{
    if (!s_busy)
        return;

    if (s_seg_on)
    {
        if (s_wave_left_100us > 0)
            s_wave_left_100us--;

        if (s_wave_left_100us == 0)
        {
            s_wave_level = s_wave_level ? 0 : 1;
            buzzer_hw_set(s_wave_level);
            s_wave_left_100us = s_wave_half_100us;
        }
    }

    if (s_seg_left_100us > 0)
        s_seg_left_100us--;

    if (s_seg_left_100us == 0)
    {
        s_seq_idx++;
        buzzer_load_segment();
    }
}

/* 1ms 周期任务（内部执�?10 �?100us 子步进） */
void Buzzer_Task1ms(void)
{
    unsigned char i;

    for (i = 0; i < 10; i++)
    {
        Buzzer_Task100us();
    }
}
