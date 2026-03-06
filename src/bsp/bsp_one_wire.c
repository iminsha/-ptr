#include "bsp_one_wire.h"
#include <intrins.h>

static unsigned char ow_enter_critical(void)
{
    unsigned char ea_old = EA;
    EA = 0;
    return ea_old;
}

static void ow_exit_critical(unsigned char ea_old)
{
    EA = ea_old;
}
/* 以下延时常数基于 11.0592MHz 标定，用于满足 1-Wire 时序窗口。 */
static void ow_delay_480us(void)
{
    unsigned char i;
    _nop_();
    i = 218;
    while (--i)
    {
        ;
    }
}

static void ow_delay_240us(void)
{
    unsigned char i;
    i = 108;
    while (--i)
    {
        ;
    }
}

static void ow_delay_60us(void)
{
    unsigned char i;
    i = 25;
    while (--i)
    {
        ;
    }
}

static void ow_delay_30us(void)
{
    unsigned char i;
    i = 11;
    while (--i)
    {
        ;
    }
}

static void ow_delay_15us(void)
{
    unsigned char i;
    i = 4;
    while (--i)
    {
        ;
    }
}

uint8_t ow_reset_presence(void)
{
    uint8_t presence;
    unsigned char ea_old = ow_enter_critical();

    ONEWIRE_DQ = 1;
    _nop_();

    ONEWIRE_DQ = 0;
    ow_delay_480us();

    ONEWIRE_DQ = 1;
    ow_delay_60us();

    presence = (ONEWIRE_DQ == 0) ? 1u : 0u;
    ow_delay_240us();

    ow_exit_critical(ea_old);
    return presence;
}

void ow_write_bit(uint8_t b)
{
    /* 写时隙起始：主机先拉低总线。 */
    ONEWIRE_DQ = 0;
    if (b)
    {
        /* 写 1：保持短低电平。 */
        ow_delay_15us();
    }
    else
    {
        /* 写 0：保持长低电平。 */
        ow_delay_60us();
    }

    /* 释放总线并留恢复时间。 */
    ONEWIRE_DQ = 1;
    ow_delay_30us();
}

uint8_t ow_read_bit(void)
{
    uint8_t b;
    unsigned char ea_old = ow_enter_critical();

    ONEWIRE_DQ = 0;
    ow_delay_15us();

    ONEWIRE_DQ = 1;
    _nop_();          /* 释放后稍等一点 */
    _nop_();

    b = (ONEWIRE_DQ != 0) ? 1u : 0u;
    ow_delay_60us();

    ow_exit_critical(ea_old);
    return b;
}

void ow_write_byte(uint8_t byte)
{
    /* i 为 bit 序号，按 LSB first 发送。 */
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        ow_write_bit(byte & 0x01u);
        byte >>= 1;
    }
}

uint8_t ow_read_byte(void)
{
    /* byte 累积 8 个 bit 的读取结果。 */
    uint8_t i;
    uint8_t byte = 0;

    for (i = 0; i < 8; i++)
    {
        if (ow_read_bit())
        {
            byte |= (1u << i);
        }
    }

    return byte;
}