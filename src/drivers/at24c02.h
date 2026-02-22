/*
 * drivers/at24c02.h
 * 说明：AT24C02 EEPROM 驱动接口定义。
 * 约定：所有 API 返回 unsigned char 错误码，0 表示成功。
 */

#ifndef __DRIVERS_AT24C02_H__
#define __DRIVERS_AT24C02_H__

#include "src\bsp\bsp_i2c_soft.h"

/* ==================== 器件地址（7bit 地址 0x50） ====================
 * AT24C02 在总线上常见地址为 0b1010_A2A1A0。
 * 当 A2/A1/A0 全接地时：
 * - 写地址字节：0xA0
 * - 读地址字节：0xA1
 */
#define AT24C02_DEV_ADDR_W   0xA0
#define AT24C02_DEV_ADDR_R   0xA1

/* ==================== 器件参数 ====================
 * TOTAL_SIZE：总容量 256 字节（地址范围 0x00~0xFF）
 * PAGE_SIZE：页写大小 8 字节（跨页必须拆分）
 */
#define AT24C02_TOTAL_SIZE   256u
#define AT24C02_PAGE_SIZE    8u

/* ==================== 返回码定义 ====================
 * AT24C02_OK
 * - 含义：操作成功。
 *
 * AT24C02_ERR_PARAM
 * - 含义：参数非法。
 * - 常见触发：输出指针为 NULL 且 len != 0。
 *
 * AT24C02_ERR_ACK
 * - 含义：I2C 传输阶段从机未应答（NACK）。
 * - 常见触发：器件未接好、地址错误、总线异常。
 *
 * AT24C02_ERR_TIMEOUT
 * - 含义：写周期完成轮询超时。
 * - 常见触发：器件忙超时或硬件故障。
 *
 * AT24C02_ERR_RANGE
 * - 含义：访问范围越界（mem_addr + len 超过 256）。
 */
#define AT24C02_OK           0u
#define AT24C02_ERR_PARAM    1u
#define AT24C02_ERR_ACK      2u
#define AT24C02_ERR_TIMEOUT  3u
#define AT24C02_ERR_RANGE    4u

/**
 * @brief AT24C02 驱动初始化。
 * @note 当前软 I2C 无额外初始化动作，此函数保留给上层统一调用。
 */
void AT24C02_Init(void);

/**
 * @brief 向 EEPROM 指定地址写入 1 字节。
 * @param mem_addr EEPROM 内部地址（0x00~0xFF）。
 * @param data_ 待写入数据。
 * @retval AT24C02_OK 写入成功。
 * @retval AT24C02_ERR_ACK 发送地址/数据阶段收到 NACK。
 * @retval AT24C02_ERR_TIMEOUT 写周期轮询超时。
 */
unsigned char AT24C02_WriteByte(unsigned char mem_addr, unsigned char data_);

/**
 * @brief 从 EEPROM 指定地址读取 1 字节（随机读）。
 * @param mem_addr EEPROM 内部地址（0x00~0xFF）。
 * @param out_data 输出指针，读取结果写入 *out_data。
 * @retval AT24C02_OK 读取成功。
 * @retval AT24C02_ERR_PARAM out_data 为 NULL。
 * @retval AT24C02_ERR_ACK 发送地址或读命令阶段收到 NACK。
 */
unsigned char AT24C02_ReadByte(unsigned char mem_addr, unsigned char* out_data);

/**
 * @brief 连续写入多个字节（自动分页，避免跨页覆盖）。
 * @param mem_addr 起始地址（0x00~0xFF）。
 * @param data_ 输入数据缓冲区指针。
 * @param len 写入长度（字节）。
 * @retval AT24C02_OK 写入成功。
 * @retval AT24C02_ERR_PARAM data_ 为 NULL 且 len != 0。
 * @retval AT24C02_ERR_RANGE 地址范围越界。
 * @retval AT24C02_ERR_ACK I2C 阶段 NACK。
 * @retval AT24C02_ERR_TIMEOUT 写周期轮询超时。
 */
unsigned char AT24C02_WriteBuffer(unsigned char mem_addr, unsigned char* data_, unsigned char len);

/**
 * @brief 连续读取多个字节（顺序读）。
 * @param mem_addr 起始地址（0x00~0xFF）。
 * @param out_data 输出缓冲区指针。
 * @param len 读取长度（字节）。
 * @retval AT24C02_OK 读取成功。
 * @retval AT24C02_ERR_PARAM out_data 为 NULL 且 len != 0。
 * @retval AT24C02_ERR_RANGE 地址范围越界。
 * @retval AT24C02_ERR_ACK I2C 阶段 NACK。
 */
unsigned char AT24C02_ReadBuffer(unsigned char mem_addr, unsigned char* out_data, unsigned char len);

#endif /* __DRIVERS_AT24C02_H__ */
