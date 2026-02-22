#include "bsp_i2c_soft.h"

/**
 * @brief  产生 I2C 起始条件（START）
 *
 * I2C 起始条件定义：
 * 在 SCL 为高电平期间，SDA 由高电平跳变为低电平
 *
 * 调用场景：
 * - 一次 I2C 通信的开始
 * - 在发送从机地址之前
 */
void startI2cSet()
{
    // 预防在复合帧处理中 SDA 被拉低，先释放 SDA
    I2C_SDA = 1;

    // 保证 SCL 为高电平，满足起始条件前提
    I2C_SCL = 1;

    // SDA 在 SCL 高电平期间由高变低 → START
    I2C_SDA = 0;

    // 保持 SDA 低电平，准备后续数据传输
    I2C_SDA = 0;
}

/**
 * @brief  产生 I2C 终止条件（STOP）
 *
 * I2C 终止条件定义：
 * 在 SCL 为高电平期间，SDA 由低电平跳变为高电平
 *
 * 调用场景：
 * - 一次 I2C 通信的结束
 * - 释放总线
 */
void StopI2cSet()
{
    // 确保 SDA 先为低电平
    I2C_SDA = 0;

    // 拉高 SCL，准备产生 STOP
    I2C_SCL = 1;

    // SDA 在 SCL 高电平期间由低变高 → STOP
    I2C_SDA = 1;
}

/**
 * @brief  通过 I2C 总线发送 1 字节数据（MSB 先行）
 *
 * @param  Data 需要发送的 8 位数据
 *
 * I2C 数据传输规则：
 * - 数据在 SCL 低电平期间改变
 * - 在 SCL 高电平期间被从机采样
 * - 高位先发送（MSB first）
 */
void sendByteData(unsigned char Data)
{
    unsigned char i;

    for(i = 0; i < 8; i++)
    {
        // 拉低 SCL，准备改变数据
        I2C_SCL = 0;

        // 依次发送 Data 的每一位（从 bit7 到 bit0）
        I2C_SDA = Data & (0x80 >> i);

        // 拉高 SCL，从机在此时刻采样 SDA
        I2C_SCL = 1;
    }

    // 发送完成后，SCL 拉低，准备 ACK
    I2C_SCL = 0;
}

/**
 * @brief  通过 I2C 总线接收 1 字节数据（MSB 先行）
 *
 * @retval 接收到的 8 位数据
 *
 * I2C 数据接收规则：
 * - 主机释放 SDA，由从机驱动 SDA
 * - 数据在 SCL 高电平期间有效
 */
unsigned char receiveByteData()
{
    unsigned char ReciveData = 0x00;
    unsigned char i;

    // 释放 SDA，由从机驱动数据线
    I2C_SDA = 1;

    for(i = 0; i < 8; i++)
    {
        // 拉高 SCL，从机输出当前位
        I2C_SCL = 1;

        // 在 SCL 高电平期间读取 SDA
        if(I2C_SDA)
        {
            ReciveData |= (0x80 >> i);
        }

        // 拉低 SCL，准备下一位
        I2C_SCL = 0;
    }

    return ReciveData;
}

/**
 * @brief  接收从机返回的 ACK/NACK 位
 *
 * @retval 0：ACK（从机应答）
 *         1：NACK（从机无应答）
 *
 * I2C ACK 规则：
 * - 第 9 个时钟周期
 * - 主机释放 SDA
 * - 从机拉低 SDA 表示 ACK
 */
bit receiveBitACKData()
{
    bit AckData = 0;

    // 释放 SDA，由从机控制
    I2C_SDA = 1;

    // 拉高 SCL，读取 ACK 位
    I2C_SCL = 1;
    AckData = I2C_SDA;

    // 拉低 SCL，结束 ACK 读取
    I2C_SCL = 0;

    return AckData;
}

/**
 * @brief  主机发送 ACK 或 NACK 给从机
 *
 * @param  Data
 *         0：发送 ACK（继续通信）
 *         1：发送 NACK（结束通信）
 *
 * 使用场景：
 * - 主机接收数据后
 * - 告诉从机是否继续发送
 */
void sendBitACKData(bit Data)
{
    // 主机驱动 SDA
    I2C_SDA = Data;

    // 拉高 SCL，从机在此时刻读取 ACK/NACK
    I2C_SCL = 1;

    // 拉低 SCL，完成 ACK 时序
    I2C_SCL = 0;
}
