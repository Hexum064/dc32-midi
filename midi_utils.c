#include "midi.h"



uint32_t big_endian_to_int(uint8_t buff[4])
{
    return (uint32_t)(buff[0] << 24) + (uint32_t)(buff[1] << 16) + (uint32_t)(buff[2] << 8) + (uint32_t)(buff[3]);
}

uint16_t big_endian_to_word(uint8_t buff[2])
{
    return (uint32_t)(buff[0] << 8) + (uint32_t)(buff[1]);
}

