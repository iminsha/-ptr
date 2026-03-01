#include <REG52.H>
#include "drivers/lcd1602.h"
#include "services/cfg_store.h"

static void DelayMs(unsigned int ms)
{
    unsigned int i, j;
    while (ms--)
    {
        for (i = 0; i < 110; i++)
            for (j = 0; j < 5; j++) { ; }
    }
}

static void show_reason(unsigned char rc)
{
    if (rc == CFG_OK)        LCD_ShowString(1, 1, "CFG LOAD OK     ");
    else if (rc == CFG_ERR_MAGIC)   LCD_ShowString(1, 1, "CFG MAGIC ERR   ");
    else if (rc == CFG_ERR_VERSION) LCD_ShowString(1, 1, "CFG VER ERR     ");
    else if (rc == CFG_ERR_CRC)     LCD_ShowString(1, 1, "CFG CRC ERR     ");
    else                              LCD_ShowString(1, 1, "CFG EEPROM ERR  ");
}

void main(void)
{
    Config idata cfg;
    unsigned char r;

    LCD_Init();
    AT24C02_Init();

    LCD_ShowString(1, 1, "CFG STORE TEST  ");
    LCD_ShowString(2, 1, "BOOT...         ");
    DelayMs(500);

    r = Cfg_Load(&cfg);
    show_reason(r);

    /* 如果加载失败，写入默认配置（让系统自愈） */
    if (r != CFG_OK)
    {
        Cfg_SetDefault(&cfg);
        Cfg_Save(&cfg);
        LCD_ShowString(2, 1, "DEFAULT SAVED   ");
        DelayMs(800);
    }

    /* 每次开机 boot_count++ 并保存，验证断电保持 */
    cfg.boot_count++;
    Cfg_Save(&cfg);

    /* 显示 boot_count 与阈值 */
    LCD_ShowString(2, 1, "B:");
    LCD_ShowNum(2, 3, (unsigned int)(cfg.boot_count % 100000), 5);

    LCD_ShowString(2, 9, "H:");
    LCD_ShowNum(2, 11, (unsigned int)(cfg.temp_high_x10), 4);

    while (1)
    {
        /* 保持显示 */
        DelayMs(500);
    }
}

