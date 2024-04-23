#include "circular_buff.h"

uint16_t _head = 0;
uint16_t _tail = 0;
midi_message _msg_buff[BUFF_SIZE];
uint16_t _count = 0;

bool cb_enque(midi_message * msg, uint16_t len)
{
    if (_count < len)
    {
        return false;
    }

    for (uint16_t i = 0; i < len; i++)
    {
        _head = (_head + 1) % BUFF_SIZE;

        _msg_buff[_head] = msg[i];
        _count++;
    }

    return true;
}
bool cb_deque(midi_message * msg,  uint32_t ticks)
{
    
    
    if (!_count || _msg_buff[_tail].ticks > ticks)
    {
        return false;
    }

    irq_set_enabled(UART0_IRQ, false);  

    *msg = _msg_buff[_tail];
    _tail = (_tail + 1) % BUFF_SIZE;
    _count--;

    irq_set_enabled(UART0_IRQ, true);
    return true;
}

uint16_t cb_count()
{
    return _count;
}