#ifndef __ACTION_CENTER_H__
#define __ACTION_CENTER_H__

#include "protocol_v1_service.h"

/*
 * 动作中心模块
 * 职责：
 * 1. 接收协议层输出的已解码命令（ProtoV1Decoded）。
 * 2. 执行业务动作（模式切换、位置更新、蜂鸣器控制等）。
 * 3. 负责请求命令的ACK/NACK与状态回包。
 *
 * 边界：
 * - 不负责协议解帧和参数基础校验（由protocol_v1_service负责）。
 * - 不负责LCD显示（由main负责）。
 */

/*
 * 函数：ActionCenter_Init
 * 作用：初始化动作中心状态与外设依赖。
 * 参数：无
 * 返回：无
 */
void ActionCenter_Init(void);

/*
 * 函数：ActionCenter_Execute
 * 作用：执行1条已解码命令并按规则回包。
 * 参数：
 * - dec: 协议层输出的解码结果指针
 * 返回：无
 */
void ActionCenter_Execute(const ProtoV1Decoded *dec);

/*
 * 函数：ActionCenter_Tick10ms
 * 作用：10ms周期任务，推进模块内部时间相关逻辑。
 * 参数：无
 * 返回：无
 */
void ActionCenter_Tick10ms(void);

#endif
