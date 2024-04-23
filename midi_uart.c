#include "midi_uart.h"
#include "pico/stdlib.h"

uint8_t msg_len = 0;
uint8_t msg_index = 0;
uint8_t msg_buff[255];
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

void start_transmit(uint8_t * data, uint8_t len)
{
    msg_index = 0;
    msg_len = len;
    sending_msg = true;
    uart_set_irq_enables(uart0, true, true);
    irq_set_pending(UART0_IRQ);
}

bool is_transmitting()
{
    return sending_msg;
}