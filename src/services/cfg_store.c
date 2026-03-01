/*
 * services/cfg_store.c
 * 配置存储服务实现文件
 */

#include "cfg_store.h"

/* =========================================================
   【存储格式设计（固定布局，便于升级）】
   ---------------------------------------------------------
   EEPROM 中存储一个“字节流”结构：

   Offset  Size  内容
   0       2     MAGIC (0xA55A)
   2       1     VERSION (CFG_VERSION)
   3       1     LEN (payload长度，用于扩展)
   4       N     PAYLOAD（按字段顺序序列化）
   4+N     2     CRC16（对 [0 .. 4+N-1] 计算CRC，
                       包含 magic/version/len/payload）

   ---------------------------------------------------------
   设计优点：

   - 版本升级可控：
       version 不匹配直接判定为不兼容

   - payload 长度可扩展：
       后续可向后追加字段

   - CRC 覆盖全部关键内容：
       可检测写坏、干扰或掉电异常

   ========================================================= */

#define CFG_MAGIC_H   0xA5
#define CFG_MAGIC_L   0x5A

/* V1 payload 固定顺序 */
#define PAYLOAD_LEN_V1   (2+2+2+1+1+4)

/*
字段说明：
    temp_high_x10   : int 2字节（Keil C51 int 通常为16位）
    temp_low_x10    : int 2字节
    cooldown_s      : unsigned int 2字节
    mode_default    : 1字节
    schedule_count  : 1字节
    boot_count      : unsigned long 4字节（Keil C51 long 通常为32位）
*/

/* 总存储长度 = 2(magic) + 1(ver) + 1(len) + payload + 2(crc) */
#define CFG_TOTAL_LEN_V1  (2+1+1+PAYLOAD_LEN_V1+2)

/* ================= CRC16（Modbus 风格）=================
   多项式：0xA001
   初值：  0xFFFF
   注意：
   CRC 算法可以不同，但读写必须一致。
========================================================== */
static unsigned int crc16_modbus(const unsigned char* bytes, unsigned char len)
{
    unsigned int crc = 0xFFFF;
    unsigned char i, j;

    for (i = 0; i < len; i++)
    {
        crc ^= bytes[i];
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else             crc >>= 1;
        }
    }
    return crc;
}

/* ================= 序列??反序列化（固定布局??================ */
static void pack_u16(unsigned char* p, unsigned int v)
{
    /* little-endian */
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}
static unsigned int unpack_u16(const unsigned char* p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static void pack_s16(unsigned char* p, int v)
{
    /* int视为16??*/
    unsigned int uv = (unsigned int)v;
    pack_u16(p, uv);
}
static int unpack_s16(const unsigned char* p)
{
    unsigned int uv = unpack_u16(p);
    return (int)uv;
}

static void pack_u32(unsigned char* p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}
static unsigned long unpack_u32(const unsigned char* p)
{
    unsigned long v = 0;
    v |= (unsigned long)p[0];
    v |= ((unsigned long)p[1] << 8);
    v |= ((unsigned long)p[2] << 16);
    v |= ((unsigned long)p[3] << 24);
    return v;
}

/* ??Config 打包??payload（V1??*/
static void cfg_pack_payload_v1(const Config* cfg, unsigned char* payload)
{
    unsigned char* p = payload;

    pack_s16(p, cfg->temp_high_x10);  p += 2;
    pack_s16(p, cfg->temp_low_x10);   p += 2;
    pack_u16(p, cfg->cooldown_s);     p += 2;

    *p++ = cfg->mode_default;
    *p++ = cfg->schedule_count;

    pack_u32(p, cfg->boot_count);     p += 4;
}

/* ??payload 解包??Config（V1??*/
static void cfg_unpack_payload_v1(Config* cfg, const unsigned char* payload)
{
    const unsigned char* p = payload;

    cfg->temp_high_x10 = unpack_s16(p); p += 2;
    cfg->temp_low_x10  = unpack_s16(p); p += 2;
    cfg->cooldown_s    = unpack_u16(p); p += 2;

    cfg->mode_default  = *p++;
    cfg->schedule_count= *p++;

    cfg->boot_count    = unpack_u32(p); p += 4;
}

void Cfg_SetDefault(Config* cfg)
{
    if (cfg == 0) return;

    /* 默认值根据你的项目需求改 */
    cfg->temp_high_x10  = 300;  /* 30.0??*/
    cfg->temp_low_x10   = 290;  /* 29.0??*/
    cfg->cooldown_s     = 30;   /* 30秒冷??*/
    cfg->mode_default   = 1;    /* 默认AUTO */
    cfg->schedule_count = 0;    /* 暂无日程 */
    cfg->boot_count     = 0;
}

/* 读出配置字节流并校验 */
unsigned char Cfg_Load(Config* cfg)
{
    unsigned char idata buf[CFG_TOTAL_LEN_V1];
    unsigned char idata ret;
    unsigned char idata payload_len;
    unsigned int idata crc_calc, crc_stored;

    if (cfg == 0) return CFG_ERR_EEPROM;

    /* 读整??*/
    ret = AT24C02_ReadBuffer(CFG_EEPROM_BASE_ADDR, buf, (unsigned char)CFG_TOTAL_LEN_V1);
    if (ret != AT24C02_OK) return CFG_ERR_EEPROM;

    /* magic校验 */
    if (buf[0] != CFG_MAGIC_H || buf[1] != CFG_MAGIC_L) return CFG_ERR_MAGIC;

    /* version校验 */
    if (buf[2] != CFG_VERSION) return CFG_ERR_VERSION;

    /* len校验（防止读到乱值导致越界） */
    payload_len = buf[3];
    if (payload_len != PAYLOAD_LEN_V1) return CFG_ERR_VERSION;

    /* CRC校验：计??[0 .. 4+payload_len-1] */
    crc_calc = crc16_modbus(buf, (unsigned char)(4 + payload_len));
    crc_stored = unpack_u16(&buf[4 + payload_len]);

    if (crc_calc != crc_stored) return CFG_ERR_CRC;

    /* 解包payload到cfg */
    cfg_unpack_payload_v1(cfg, &buf[4]);

    return CFG_OK;
}

/* 保存配置：打包?-> 计算CRC -> 写入 -> (可??再读回验??*/
unsigned char Cfg_Save(const Config* cfg)
{
    unsigned char idata buf[CFG_TOTAL_LEN_V1];
    unsigned char idata ret;
    unsigned int idata crc;

    if (cfg == 0) return CFG_ERR_EEPROM;

    /* 头部 */
    buf[0] = CFG_MAGIC_H;
    buf[1] = CFG_MAGIC_L;
    buf[2] = CFG_VERSION;
    buf[3] = PAYLOAD_LEN_V1;

    /* payload */
    cfg_pack_payload_v1(cfg, &buf[4]);

    /* CRC */
    crc = crc16_modbus(buf, (unsigned char)(4 + PAYLOAD_LEN_V1));
    pack_u16(&buf[4 + PAYLOAD_LEN_V1], crc);

    /* 写入整块（内部驱动会处理页写与ACK轮询??*/
    ret = AT24C02_WriteBuffer(CFG_EEPROM_BASE_ADDR, buf, (unsigned char)CFG_TOTAL_LEN_V1);
    if (ret != AT24C02_OK) return CFG_ERR_EEPROM;

    return CFG_OK;
}


