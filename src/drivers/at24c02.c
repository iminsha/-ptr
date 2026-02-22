/*
 * drivers/at24c02.c
 * 说明：AT24C02 EEPROM 驱动实现。
 */

#include "at24c02.h"

/* 写周期轮询上限：用于等待 EEPROM 内部写入完成，避免无限阻塞。 */
#define AT24C02_WRITE_POLL_MAX  200u

/**
 * @brief 检查从机 ACK。
 * @retval AT24C02_OK 收到 ACK。
 * @retval AT24C02_ERR_ACK 收到 NACK。
 */
static unsigned char at24c02_check_ack(void)
{
    return (receiveBitACKData() == 0) ? AT24C02_OK : AT24C02_ERR_ACK;
}

/**
 * @brief 轮询写完成（ACK Polling）。
 * @details
 * 写 AT24C02 后，器件会在内部执行写周期；这段时间可能不应答。
 * 本函数通过重复发器件写地址来轮询是否恢复 ACK。
 * @retval AT24C02_OK 写周期完成。
 * @retval AT24C02_ERR_TIMEOUT 超过轮询上限仍未完成。
 */
static unsigned char at24c02_wait_write_done(void)
{
    /* i：轮询计数器，防止异常情况下死循环。 */
    unsigned int i;

    for (i = 0; i < AT24C02_WRITE_POLL_MAX; i++)
    {
        startI2cSet();
        sendByteData(AT24C02_DEV_ADDR_W);
        if (receiveBitACKData() == 0)
        {
            StopI2cSet();
            return AT24C02_OK;
        }
        StopI2cSet();
    }

    return AT24C02_ERR_TIMEOUT;
}

/**
 * @brief 初始化 EEPROM 驱动。
 * @note 当前软 I2C 无独立初始化动作，函数预留用于上层统一初始化流程。
 */
void AT24C02_Init(void)
{
}

/**
 * @brief 写 1 字节。
 * @param mem_addr 内部地址（0x00~0xFF）。
 * @param data_ 待写数据。
 * @retval AT24C02_OK 成功。
 * @retval AT24C02_ERR_ACK I2C 阶段 NACK。
 * @retval AT24C02_ERR_TIMEOUT 写周期等待超时。
 */
unsigned char AT24C02_WriteByte(unsigned char mem_addr, unsigned char data_)
{
    /* ret：记录每一步 I2C 传输的返回状态。 */
    unsigned char ret;

    startI2cSet();

    sendByteData(AT24C02_DEV_ADDR_W);
    ret = at24c02_check_ack();
    if (ret != AT24C02_OK)
    {
        StopI2cSet();
        return ret;
    }

    sendByteData(mem_addr);
    ret = at24c02_check_ack();
    if (ret != AT24C02_OK)
    {
        StopI2cSet();
        return ret;
    }

    sendByteData(data_);
    ret = at24c02_check_ack();
    StopI2cSet();
    if (ret != AT24C02_OK)
    {
        return ret;
    }

    return at24c02_wait_write_done();
}

/**
 * @brief 读 1 字节（随机读）。
 * @param mem_addr 内部地址（0x00~0xFF）。
 * @param out_data 输出地址。
 * @retval AT24C02_OK 成功。
 * @retval AT24C02_ERR_PARAM out_data 为 NULL。
 * @retval AT24C02_ERR_ACK I2C 阶段 NACK。
 */
unsigned char AT24C02_ReadByte(unsigned char mem_addr, unsigned char* out_data)
{
    /* ret：记录每一步 I2C 传输状态。 */
    unsigned char ret;

    if (out_data == 0)
    {
        return AT24C02_ERR_PARAM;
    }

    /* 随机读流程：先写目标地址，再重复起始切换到读模式。 */
    startI2cSet();

    sendByteData(AT24C02_DEV_ADDR_W);
    ret = at24c02_check_ack();
    if (ret != AT24C02_OK)
    {
        StopI2cSet();
        return ret;
    }

    sendByteData(mem_addr);
    ret = at24c02_check_ack();
    if (ret != AT24C02_OK)
    {
        StopI2cSet();
        return ret;
    }

    startI2cSet();
    sendByteData(AT24C02_DEV_ADDR_R);
    ret = at24c02_check_ack();
    if (ret != AT24C02_OK)
    {
        StopI2cSet();
        return ret;
    }

    *out_data = receiveByteData();
    sendBitACKData(1); /* 单字节读取完成后发送 NACK。 */
    StopI2cSet();

    return AT24C02_OK;
}

/**
 * @brief 连续写入（自动按页拆分）。
 * @param mem_addr 起始地址。
 * @param data_ 输入数据缓冲区。
 * @param len 写入长度。
 * @retval AT24C02_OK 成功。
 * @retval AT24C02_ERR_PARAM 参数非法。
 * @retval AT24C02_ERR_RANGE 范围越界。
 * @retval AT24C02_ERR_ACK I2C 阶段 NACK。
 * @retval AT24C02_ERR_TIMEOUT 写周期等待超时。
 */
unsigned char AT24C02_WriteBuffer(unsigned char mem_addr, unsigned char* data_, unsigned char len)
{
    unsigned char ret;
    /* chunk：本次页写长度；page_left：当前页剩余可写字节数。 */
    unsigned char chunk;
    unsigned char page_left;
    /* i：页内写入循环变量；offset：相对 data_ 的偏移。 */
    unsigned char i;
    unsigned char offset = 0;
    /* end_addr：用于越界检查的结束地址（开区间）。 */
    unsigned int end_addr;

    if ((data_ == 0) && (len != 0))
    {
        return AT24C02_ERR_PARAM;
    }

    if (len == 0)
    {
        return AT24C02_OK;
    }

    end_addr = (unsigned int)mem_addr + (unsigned int)len;
    if (end_addr > AT24C02_TOTAL_SIZE)
    {
        return AT24C02_ERR_RANGE;
    }

    while (offset < len)
    {
        page_left = (unsigned char)(AT24C02_PAGE_SIZE - (mem_addr & (AT24C02_PAGE_SIZE - 1u)));
        chunk = (unsigned char)(len - offset);
        if (chunk > page_left)
        {
            chunk = page_left;
        }

        startI2cSet();

        sendByteData(AT24C02_DEV_ADDR_W);
        ret = at24c02_check_ack();
        if (ret != AT24C02_OK)
        {
            StopI2cSet();
            return ret;
        }

        sendByteData(mem_addr);
        ret = at24c02_check_ack();
        if (ret != AT24C02_OK)
        {
            StopI2cSet();
            return ret;
        }

        for (i = 0; i < chunk; i++)
        {
            sendByteData(data_[offset + i]);
            ret = at24c02_check_ack();
            if (ret != AT24C02_OK)
            {
                StopI2cSet();
                return ret;
            }
        }

        StopI2cSet();

        ret = at24c02_wait_write_done();
        if (ret != AT24C02_OK)
        {
            return ret;
        }

        mem_addr = (unsigned char)(mem_addr + chunk);
        offset = (unsigned char)(offset + chunk);
    }

    return AT24C02_OK;
}

/**
 * @brief 连续读取（顺序读）。
 * @param mem_addr 起始地址。
 * @param out_data 输出缓冲区。
 * @param len 读取长度。
 * @retval AT24C02_OK 成功。
 * @retval AT24C02_ERR_PARAM 参数非法。
 * @retval AT24C02_ERR_RANGE 范围越界。
 * @retval AT24C02_ERR_ACK I2C 阶段 NACK。
 */
unsigned char AT24C02_ReadBuffer(unsigned char mem_addr, unsigned char* out_data, unsigned char len)
{
    unsigned char ret;
    /* i：循环索引；end_addr：用于范围校验。 */
    unsigned char i;
    unsigned int end_addr;

    if ((out_data == 0) && (len != 0))
    {
        return AT24C02_ERR_PARAM;
    }

    if (len == 0)
    {
        return AT24C02_OK;
    }

    end_addr = (unsigned int)mem_addr + (unsigned int)len;
    if (end_addr > AT24C02_TOTAL_SIZE)
    {
        return AT24C02_ERR_RANGE;
    }

    startI2cSet();

    sendByteData(AT24C02_DEV_ADDR_W);
    ret = at24c02_check_ack();
    if (ret != AT24C02_OK)
    {
        StopI2cSet();
        return ret;
    }

    sendByteData(mem_addr);
    ret = at24c02_check_ack();
    if (ret != AT24C02_OK)
    {
        StopI2cSet();
        return ret;
    }

    startI2cSet();
    sendByteData(AT24C02_DEV_ADDR_R);
    ret = at24c02_check_ack();
    if (ret != AT24C02_OK)
    {
        StopI2cSet();
        return ret;
    }

    for (i = 0; i < len; i++)
    {
        out_data[i] = receiveByteData();
        /* 最后一个字节发 NACK，其他字节发 ACK。 */
        sendBitACKData((i == (unsigned char)(len - 1u)) ? 1 : 0);
    }

    StopI2cSet();
    return AT24C02_OK;
}
