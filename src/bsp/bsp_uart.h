#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include <REG52.H>

/*
 * UART link layer:
 * - ISR driven RX/TX ring buffers.
 * - Optional protocol parser/packer for unified V1 frame.
 */

#define UART_RX_BUFFER_SIZE        32
#define UART_TX_BUFFER_SIZE        32
#define UART_BAUD_RATE             4800

/* V1 frame: [55][AA][VER][CMD][SEQ][LEN][PAYLOAD...][CHK] */
#define UART_FRAME_SOF1            0x55
#define UART_FRAME_SOF2            0xAA
#define UART_FRAME_VER             0x01
#define UART_FRAME_MAX_PAYLOAD     32
#define UART_FRAME_QUEUE_SIZE      4

typedef struct
{
    unsigned char ver;
    unsigned char cmd;
    unsigned char seq;
    unsigned char len;
    unsigned char payload[UART_FRAME_MAX_PAYLOAD];
    unsigned char checksum;
} UartFrame;

typedef void (*uart_frame_handler_t)(const UartFrame *frame);

void UartInit(void);
void Uart_SendByte(unsigned char byte);
void Uart_SendBuffer(const unsigned char *buf, unsigned char len);
void Uart_SendString(const char *str);

unsigned char Uart_ReadByte(unsigned char *byte);
unsigned char Uart_RxAvailable(void);
unsigned char Uart_TxIdle(void);
unsigned char Uart_RxOverflowed(void);
void Uart_ClearRxOverflow(void);
unsigned char Uart_TxOverflowed(void);
void Uart_ClearTxOverflow(void);

void Uart_ProtocolProcess(void);
unsigned char Uart_ProtocolGetFrame(UartFrame *frame);
unsigned char Uart_ProtocolSendFrame(unsigned char cmd,
                                     unsigned char seq,
                                     const unsigned char *payload,
                                     unsigned char len);
void Uart_RegisterFrameHandler(uart_frame_handler_t handler);

/* legacy alias */
void SendData(unsigned char byte);

#endif
