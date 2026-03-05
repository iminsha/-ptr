#ifndef __PROTOCOL_V1_SERVICE_H__
#define __PROTOCOL_V1_SERVICE_H__

#include "../bsp/bsp_uart.h"

/*
 * 协议解码错误码定义
 */
#define PROTO_V1_ERR_NONE         0   /* 无错误 */
#define PROTO_V1_ERR_CMD_UNKNOWN  1   /* 命令字未知 */
#define PROTO_V1_ERR_LEN_INVALID  2   /* 长度非法 */

/*
 * 命令名称ID定义
 * 说明：
 * - 使用ID而非字符串，避免C51在字符串指针上的存储区兼容问题。
 * - main层可通过switch将ID映射到LCD文本。
 */
#define PROTO_V1_NAME_PING_REQ    1
#define PROTO_V1_NAME_MODE_SET    2
#define PROTO_V1_NAME_MOVE_TO     3
#define PROTO_V1_NAME_STOP_REQ    4
#define PROTO_V1_NAME_BEEP_REQ    5
#define PROTO_V1_NAME_CFG_GET     6
#define PROTO_V1_NAME_CFG_SET     7
#define PROTO_V1_NAME_STAT_GET    8
#define PROTO_V1_NAME_PING_RSP    9
#define PROTO_V1_NAME_STAT_RSP    10
#define PROTO_V1_NAME_CFG_RSP     11
#define PROTO_V1_NAME_ACK         12
#define PROTO_V1_NAME_NACK        13
#define PROTO_V1_NAME_STAT_EVT    14
#define PROTO_V1_NAME_ERR_EVT     15
#define PROTO_V1_NAME_UNKNOWN     255

/*
 * 协议解码结果
 * 字段说明：
 * - valid: 1表示解码成功；0表示失败（仍保留基础字段用于诊断）
 * - err:   对应PROTO_V1_ERR_xxx
 * - cmd:   原始命令字
 * - name_id: 命令语义ID（用于UI显示）
 * - seq/len: 帧头中的SEQ、LEN
 * - p0/p1/p2: 前3个payload字节（常用参数快速读取）
 */
typedef struct
{
    unsigned char valid;
    unsigned char err;
    unsigned char cmd;
    unsigned char name_id;
    unsigned char seq;
    unsigned char len;
    unsigned char p0;
    unsigned char p1;
    unsigned char p2;
} ProtoV1Decoded;

/*
 * 函数：ProtoV1_Init
 * 作用：初始化协议解码服务内部状态与结果队列。
 * 参数：无
 * 返回：无
 */
void ProtoV1_Init(void);

/*
 * 函数：ProtoV1_Poll
 * 作用：
 * - 驱动底层串口协议状态机进行解帧；
 * - 将完整帧翻译为语义结果并入队。
 * 参数：无
 * 返回：无
 */
void ProtoV1_Poll(void);

/*
 * 函数：ProtoV1_GetDecoded
 * 作用：从解码结果队列中取出1条结果。
 * 参数：
 * - out: 输出指针，不能为空。
 * 返回值：
 * - 1: 成功取到1条结果；
 * - 0: 队列为空或参数无效。
 */
unsigned char ProtoV1_GetDecoded(ProtoV1Decoded *out);

/*
 * 函数：ProtoV1_Tick10ms
 * 作用：10ms周期钩子，预留给后续超时统计/节流显示。
 * 参数：无
 * 返回：无
 */
void ProtoV1_Tick10ms(void);

#endif
