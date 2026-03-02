#include <REG52.H>
#include "drivers/lcd1602.h"
#include "bsp/bsp_uart.h"

void main()
{
    UartFrame frame;
    unsigned char i;

    LCD_Init();
    UartInit();

    /*
     * Demo UI:
     * Line1 shows parsed command and payload length.
     * Line2 shows first up to 4 payload bytes in hex.
     */
    LCD_ShowString(1, 1, "CMD:00 LEN:00");
    LCD_ShowString(2, 1, "P0P1P2P3:0000");

    while (1)
    {
        /* Parse all bytes currently buffered by UART ISR. */
        Uart_ProtocolProcess();

        if (Uart_ProtocolGetFrame(&frame))
        {
            LCD_ShowHexNum(1, 5, frame.cmd, 2);
            LCD_ShowHexNum(1, 12, frame.len, 2);

            for (i = 0; i < 4; i++)
            {
                if (i < frame.len)
                {
                    LCD_ShowHexNum(2, (unsigned char)(10 + i * 2), frame.payload[i], 2);
                }
                else
                {
                    LCD_ShowHexNum(2, (unsigned char)(10 + i * 2), 0, 2);
                }
            }

            /*
             * Echo frame back to host for link verification.
             * Upper software can compare send/recv to verify transport.
             */
            (void)Uart_ProtocolSendFrame(frame.cmd, frame.payload, frame.len);
        }
    }
}
