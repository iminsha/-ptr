#pragma once
#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include <REG52.H>

/*
 * UART 模块设计说明：
 * - 采用中断驱动的异步接收/发送（RX/TX）。
 * - 使用环形缓冲区（Ring Buffers）隔离中断服务程序（ISR）与应用层上下文。
 * - 内置帧解析器，支持与上位机软件的协议扩展。
 */

/* -------------------- 串口硬件配置 -------------------- */
/* 使用 2 的幂次方作为缓冲区大小，可以通过位掩码（&）快速实现索引回环，效率更高。 */
#define UART_RX_BUFFER_SIZE        64  // 接收缓冲区大小
#define UART_TX_BUFFER_SIZE        64  // 发送缓冲区大小

/* 默认串口设置：适用于 11.0592MHz 晶振。 */
#define UART_BAUD_RATE             4800

/* -------------------- 通讯协议配置 -------------------- */
/* 数据帧格式: [帧头1][帧头2][长度][命令][有效负载...][校验和] */
#define UART_FRAME_SOF1            0x55  // 帧头1 (Start of Frame)
#define UART_FRAME_SOF2            0xAA  // 帧头2
#define UART_FRAME_MAX_PAYLOAD     32    // 单帧最大负载长度
#define UART_FRAME_QUEUE_SIZE      4     // 解析后的帧队列大小

typedef struct
{
    unsigned char cmd;                          // 命令码
    unsigned char len;                          // 有效数据长度
    unsigned char payload[UART_FRAME_MAX_PAYLOAD]; // 数据内容
    unsigned char checksum;                     // 校验和
} UartFrame;

/* 回调函数：在 Uart_ProtocolProcess() 上下文中执行，而非在中断中执行。 */
typedef void (*uart_frame_handler_t)(const UartFrame *frame);

/* -------------------- 驱动层 API (基础收发) -------------------- */
void UartInit(void); // 初始化串口

/* 非阻塞发送：将数据放入发送环形队列。如果队列满了，数据将被丢弃。 */
void Uart_SendByte(unsigned char byte);
void Uart_SendBuffer(const unsigned char *buf, unsigned char len);
void Uart_SendString(const char *str);

/* 接收读取：从缓冲区取出一个字节。返回 1 表示获取成功，返回 0 表示缓冲区为空。 */
unsigned char Uart_ReadByte(unsigned char *byte);
unsigned char Uart_RxAvailable(void); // 检查接收缓冲区是否有数据
unsigned char Uart_TxIdle(void);      // 检查发送缓冲区是否已清空

/* 溢出标志：用于故障诊断和系统可靠性监控。 */
unsigned char Uart_RxOverflowed(void); // 检查接收是否溢出（数据存太慢取太快）
void Uart_ClearRxOverflow(void);
unsigned char Uart_TxOverflowed(void); // 检查发送是否溢出
void Uart_ClearTxOverflow(void);

/* -------------------- 协议层 API (帧处理) -------------------- */
/* 在主循环中周期性调用，用于将接收到的原始字节解析成完整的“帧”。 */
void Uart_ProtocolProcess(void);

/* 从解析队列中取出一帧数据。如果队列中有可用帧，返回 1。 */
unsigned char Uart_ProtocolGetFrame(UartFrame *frame);

/* 封包并发送一个协议帧。如果缓冲区接受了该帧，返回 1。 */
unsigned char Uart_ProtocolSendFrame(unsigned char cmd,
                                     const unsigned char *payload,
                                     unsigned char len);

/* 可选：注册上层回调函数。当解析到完整帧时自动处理。 */
void Uart_RegisterFrameHandler(uart_frame_handler_t handler);

/* -------------------- 兼容性 API -------------------- */
/* 为旧代码提供的兼容别名。 */
void SendData(unsigned char byte);

#endif