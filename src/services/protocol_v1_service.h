#ifndef __PROTOCOL_V1_SERVICE_H__
#define __PROTOCOL_V1_SERVICE_H__

#include "../bsp/bsp_uart.h"

/*
 * 协议V1业务层
 * - 维护命令字典（CMD -> 参数规则）
 * - 将帧解码为语义结果（命令名、参数、错误码）
 * - 提供HEX页/语义页轮播显示
 * - 通过轮询接口保持main简洁
 */

void ProtoV1_Init(void);
void ProtoV1_Poll(void);
void ProtoV1_Tick10ms(void);

#endif
