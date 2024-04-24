#include <stdlib.h>
#include <stdio.h>
#include "midi_uart.h"
#include "pico/stdlib.h"

uint16_t msg_len = 0;
uint16_t msg_index = 0;
uint8_t * msg_buff;
bool sending_msg = false;


static void uart0_isr() 
{
//   while (uart_is_readable(uart0)) {
//     printf("0x%02x\t", uart_getc(uart0));
//   }
  
    if (uart_is_writable(uart0))
    {
        if (msg_index == msg_len)
        {
            uart_set_irq_enables(uart0, true, false);
            sending_msg = false;
            printf("Done sending\n");
        }
        else
        {
            uart_putc_raw(uart0, msg_buff[msg_index++]);
        }
    }

}

void serial_init()
{
    uart_init(uart0, MIDI_OUT_BAUD);
    gpio_set_function( 0, GPIO_FUNC_UART );
    gpio_set_function( 1, GPIO_FUNC_UART );
    uart_set_fifo_enabled(uart0, false);
    irq_set_exclusive_handler(UART0_IRQ, uart0_isr);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);
    // uart_set_irq_enables(PICO_DEFAULT_UART_INSTANCE, true, false);
}

void start_transmit(uint8_t * data, uint16_t len)
{
    msg_index = 0;
    msg_len = len;
    msg_buff = data;
    sending_msg = true;
    
    for(uint16_t i = 0; i < len; i++)
    {
        printf("0x%02x ", data[i]);
    }
    printf("\n");

    uart_set_irq_enables(uart0, true, true);
    irq_set_pending(UART0_IRQ);
}

bool is_transmitting()
{
    return sending_msg;
}