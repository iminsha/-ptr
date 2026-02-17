/*
 * drivers/ds18b20.h
 * DS18B20 temperature sampling API.
 */

#ifndef __DRIVERS_DS18B20_H__
#define __DRIVERS_DS18B20_H__

#include "src\kernel\Data_rename.h"

typedef enum {
    /* 温度读取正常。 */
    DS18B20_OK = 0,
    /* 总线复位后无设备应答。 */
    DS18B20_ERR_NO_DEVICE = 1,
    /* Scratchpad CRC 校验失败。 */
    DS18B20_ERR_CRC = 2
} ds18b20_err_t;

/**
 * @brief 初始化并检测 DS18B20 在线状态。
 * @return DS18B20_OK 或 DS18B20_ERR_NO_DEVICE。
 */
ds18b20_err_t DS18B20_Init(void);

/**
 * @brief 触发一次温度转换（Convert T）。
 * @return DS18B20_OK 或 DS18B20_ERR_NO_DEVICE。
 */
ds18b20_err_t DS18B20_StartConvert(void);

/**
 * @brief 读取温度值（单位 0.1°C，放大 10 倍）。
 * @param out_x10 输出温度指针，例如 25.3°C 对应 253。
 * @return DS18B20_OK / DS18B20_ERR_NO_DEVICE / DS18B20_ERR_CRC。
 */
ds18b20_err_t DS18B20_ReadTempX10(int16_t* out_x10);

/**
 * @brief 阻塞式读取温度：启动转换 + 等待 + 读取。
 * @param out_x10 输出温度指针。
 * @return DS18B20_OK / DS18B20_ERR_NO_DEVICE / DS18B20_ERR_CRC。
 */
ds18b20_err_t DS18B20_GetTempX10_Blocking(int16_t* out_x10);

#endif /* __DRIVERS_DS18B20_H__ */
