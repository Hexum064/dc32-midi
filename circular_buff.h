
#ifndef CIRCULAR_BUFF_H
#define CIRCULAR_BUFF_H

#include "stdbool.h"
#include <stdint.h>
#include "hardware/irq.h"
#include "midi.h"

#define BUFF_SIZE 255

bool cb_enque(midi_message * msg, uint16_t len);
bool cb_deque(midi_message * msg, uint32_t ticks);
uint16_t cb_count();


#endif