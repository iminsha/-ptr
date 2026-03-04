#include <REG52.H>
#include "bsp/bsp_uart.h"
#include "drivers/lcd1602.h"
#include "services/protocol_v1_service.h"

/*
 * main.c keeps only a clean integration test loop:
 * 1) Init UART.
 * 2) Init protocol service.
 * 3) Poll protocol and run periodic tick.
 */

static void DelayMS(unsigned int ms)
{
    unsigned int i;
    unsigned int j;
    for (i = 0; i < ms; ++i)
    {
        for (j = 0; j < 114; ++j)
        {
        }
    }
}

void main(void)
{
    LCD_Init();
    LCD_ShowString(1, 1, "mySmart V1 Ready");
    LCD_ShowString(2, 1, "Wait CMD...");

    UartInit();
    ProtoV1_Init();

    while (1)
    {
        ProtoV1_Poll();
        ProtoV1_Tick10ms();
        DelayMS(10);
    }
}
