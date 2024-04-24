#ifndef MIDI_UART_H
#define MIDI_UART_H

#include "hardware/uart.h"

#define MIDI_OUT_BAUD 31250

void serial_init();

void start_transmit(uint8_t * data, uint16_t len);

bool is_transmitting();

#endif