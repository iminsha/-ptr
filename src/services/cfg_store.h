/*
 * services/cfg_store.h
 * 功能：配置加载 / 保存 / 默认恢复
 * 特点：包含 magic、version、CRC 校验机制
 */

/*
============================================================
1) 模块职责
------------------------------------------------------------
- 加载配置：
    从 24C02 读取 Config 数据，校验合法性
    （magic / version / CRC）

- 保存配置：
    将 RAM 中的配置写入 24C02
    （支持页写，可选写后校验）

- 默认恢复：
    当校验失败时恢复默认值，并返回错误原因码

- 版本兼容：
    当结构体升级导致 version 不匹配时，
    自动判定为不兼容，恢复默认配置
============================================================

2) 数据流说明
------------------------------------------------------------
上电流程：
    调用 Cfg_Load(&g_cfg)
        ├─ 成功 -> g_cfg 可直接使用
        └─ 失败 -> Cfg_SetDefault(&g_cfg)
                   （可选）Cfg_Save(&g_cfg)
                   LCD 提示 “DEFAULT”

运行中：
    按键或 UI 修改 g_cfg（仅修改 RAM）

用户确认保存：
    调用 Cfg_Save(&g_cfg) 写入 EEPROM

下次上电：
    从 EEPROM 读取并恢复完整配置
============================================================

3) 建议的配置字段（第一版保持精简）
------------------------------------------------------------
- magic        固定标识（例如 0xA55A）
- version      结构体版本号
- crc16        数据校验值
- temp_high_x10
- temp_low_x10
- cooldown_s
- mode_default
- schedule_count
- （可选）buzzer_enable
- （预留）步进电机相关参数

重要：
    必须实现 CRC 校验。
    若不做 CRC，在 EEPROM 写入过程中断电或写坏，
    会导致读取到非法数据，引发系统异常行为。
============================================================
*/

#pragma once
#ifndef __SERVICES_CFG_STORE_H__
#define __SERVICES_CFG_STORE_H__

#include <REG52.H>
#include "../drivers/at24c02.h"

/* =========================================================
   cfg_store：配置存储服务层（基于 AT24C02）
   ---------------------------------------------------------
   数据流：

   1) 上电：
        Cfg_Load(&g_cfg)
            - 返回 CFG_OK -> g_cfg 可直接使用
            - 返回错误  -> reason 表示失败原因

   2) 加载失败：
        Cfg_SetDefault(&g_cfg)
        （可选）Cfg_Save(&g_cfg)

   3) 运行中：
        UI / 按键修改 g_cfg

   4) 用户确认：
        Cfg_Save(&g_cfg)
   ========================================================= */

/* 返回值定义 */
#define CFG_OK                 0
#define CFG_ERR_EEPROM         1   /* I2C/EEPROM 通信失败 */
#define CFG_ERR_MAGIC          2   /* magic 不匹配 */
#define CFG_ERR_VERSION        3   /* version 不兼容 */
#define CFG_ERR_CRC            4   /* CRC 校验失败 */

/* 存储布局参数 */
#define CFG_EEPROM_BASE_ADDR   0x00   /* 配置存放起始地址（0~255） */
#define CFG_VERSION            1      /* 当前配置结构版本号 */

/* 配置结构（RAM 中使用，字段为十进制自然值，不使用 BCD） */
typedef struct
{
    /* 温控参数（单位：0.1℃，例如 25.3℃ -> 253） */
    int  temp_high_x10;          /* 高温阈值 */
    int  temp_low_x10;           /* 低温阈值 */
    unsigned int cooldown_s;     /* 冷却时间（秒） */

    /* 默认工作模式（0=MANUAL，1=AUTO，可根据项目定义） */
    unsigned char mode_default;

    /* 日程条目数量 */
    unsigned char schedule_count;

    /* 启动计数（用于验证断电保存是否生效） */
    unsigned long boot_count;

} Config;

/* 设置默认配置（仅初始化 RAM，不写入 EEPROM） */
void Cfg_SetDefault(Config* cfg);

/*
 * 从 EEPROM 加载配置并校验
 * 返回 CFG_OK 表示成功，否则返回错误码
 */
unsigned char Cfg_Load(Config* cfg);

/*
 * 保存配置到 EEPROM（包含 CRC）
 * 返回 CFG_OK 表示成功
 */
unsigned char Cfg_Save(const Config* cfg);

#endif