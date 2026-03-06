/*
 * drivers/ds18b20.c
 * DS18B20 temperature sensor driver implementation.
 */

#include "ds18b20.h"
#include "src\bsp\bsp_one_wire.h"

#define DS18B20_CMD_SKIP_ROM          0xCC
#define DS18B20_CMD_CONVERT_T         0x44
#define DS18B20_CMD_READ_SCRATCHPAD   0xBE

/* 简单毫秒延时，用于等待温度转换完成。 */
static void ds18b20_delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; i++)
    {
        for (j = 0; j < 120; j++)
        {
            ;
        }
    }
}

ds18b20_err_t DS18B20_Init(void)
{
    return ow_reset_presence() ? DS18B20_OK : DS18B20_ERR_NO_DEVICE;
}

ds18b20_err_t DS18B20_StartConvert(void)
{
    if (!ow_reset_presence())
    {
        return DS18B20_ERR_NO_DEVICE;
    }

    ow_write_byte(DS18B20_CMD_SKIP_ROM);
    ow_write_byte(DS18B20_CMD_CONVERT_T);
    return DS18B20_OK;
}

ds18b20_err_t DS18B20_ReadTempX10(int16_t* out_x10)
{
    /* scratchpad[0..8]：温度LSB/MSB...CRC。 */
    uint8_t scratchpad[9];
    /* i/j：循环计数；crc：CRC8 累加值。 */
    uint8_t i;
    uint8_t j;
    uint8_t crc = 0;
    uint8_t all_zero = 1u;
    uint8_t all_ff = 1u;
    /* raw：DS18B20 原始 1/16°C 补码；x10：换算后的 0.1°C。 */
    int16_t raw;
    int16_t x10;

    if (out_x10 == 0)
    {
        return DS18B20_ERR_CRC;
    }

    if (!ow_reset_presence())
    {
        return DS18B20_ERR_NO_DEVICE;
    }

    ow_write_byte(DS18B20_CMD_SKIP_ROM);
    ow_write_byte(DS18B20_CMD_READ_SCRATCHPAD);

    for (i = 0; i < 9; i++)
    {
        scratchpad[i] = ow_read_byte();
        if (scratchpad[i] != 0x00u)
        {
            all_zero = 0u;
        }
        if (scratchpad[i] != 0xFFu)
        {
            all_ff = 0u;
        }
    }

    /*
     * 总线异常保护：
     * - 全 0x00 常见于数据线被拉低/无效读取；
     * - 全 0xFF 常见于数据线悬空/上拉读取。
     * 这两种情况如果直接参与CRC可能“误通过”，会表现为假温度（如 0.0C）。
     */
    if ((all_zero != 0u) || (all_ff != 0u))
    {
        return DS18B20_ERR_NO_DEVICE;
    }

    /* 依据 Dallas CRC8 多项式 0x31(反转表示 0x8C) 计算校验。 */
    for (i = 0; i < 8; i++)
    {
        crc ^= scratchpad[i];
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x01)
            {
                crc = (crc >> 1) ^ 0x8C;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    if (crc != scratchpad[8])
    {
        return DS18B20_ERR_CRC;
    }

    raw = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);
    x10 = (int16_t)((raw * 10) / 16);
    *out_x10 = x10;

    return DS18B20_OK;
}

ds18b20_err_t DS18B20_GetTempX10_Blocking(int16_t* out_x10)
{
    ds18b20_err_t err;

    err = DS18B20_StartConvert();
    if (err != DS18B20_OK)
    {
        return err;
    }

    ds18b20_delay_ms(760);
    return DS18B20_ReadTempX10(out_x10);
}
