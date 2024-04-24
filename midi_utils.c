#include "midi.h"
#include <stdbool.h>

bool end_of_midi(midi_info * midi)
{    
    for (uint8_t i = 0; i < midi->track_cnt; i++)
    {
        if (!midi->tracks[i].eot_reached)
        {
            return false;
        }
    }

    return true;
}


uint32_t big_endian_to_int(uint8_t buff[4])
{
    return (uint32_t)(buff[0] << 24) + (uint32_t)(buff[1] << 16) + (uint32_t)(buff[2] << 8) + (uint32_t)(buff[3]);
}

//Always reads 2 bytes
uint16_t big_endian_to_word(uint8_t buff[2])
{
    return (uint32_t)(buff[0] << 8) + (uint32_t)(buff[1]);
}

