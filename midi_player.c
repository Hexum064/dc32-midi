#include "midi.h"
#include <stdlib.h>
#include <stdio.h>
#include "pico/stdlib.h"





void process_track(midi_info * midi, uint8_t track_index, uint8_t * message_data, uint8_t * message_length)
{
    uint8_t data[3];
    uint8_t meta_channel = 0;
    uint8_t meta_event = 0;
    uint8_t len = 0;
    uint32_t tempo = 0;
    uint8_t status = 0;
    bool new_status = false;
    track_info * track = &(midi->tracks[track_index]);



    while (1)
    {

        //So we can skip it if a non-0 delta was read last and just continue at the status byte
        if ( track->read_delta)
        {
            track->offset += get_VLQ(midi->midi_data, track->offset,  &( track->delta));
        }

// printf("\t*Trk %d delta: %u, elapsed: %u\n", track_index, track->delta, track->elapsed);   

        if (track->read_delta && track->delta > 0)
        {
            track->elapsed += track->delta;
            track->read_delta = false;
// printf("\t\t*Elapsed: %u\n", track->elapsed);             
            return;
        }

        

        status = midi->midi_data[track->offset];

        //If the MSB is set, we have a new status
        //(i.e. this is not a running status)
        if ((status & 0x80))
        {
            track->offset++;
            track->status = status;
            new_status = true;
        }
        else
        {
            status = track->status;
            new_status = false;
        }

        //We want to send a new status any time we have
        //a new delta. If read_delta is false, that means
        //it was a new delta the last time through so we
        //should treat this as a new status 
        if (!track->read_delta)
        {
            new_status = true;
        }

        //Then we can reset the read_delta flag
        track->read_delta = true;

// printf("\t*Trk %d status: 0x%02x. Running: %s\n", track_index, status, new_status ? "f" : "t");        

        //Is this a System Common Message
        if ((status & 0xF0) == 0xF0)
        {
            // printf("\t\tsys msg\n");
            switch(status)
            {
                //These are Sys common and sys real-time messages
                case SYS_EX:
                //TODO: read and skip data. We don't care about these
                    break;
                case MIDI_START:
                    //Nothing to send here. We will use this as a control here
                    break;
                case MIDI_CONT:
                    //Nothing to send here. We will use this as a control here
                    break;
                case MIDI_STOP:
                    //Nothing to send here. We will use this as a control here
                    break;
                case SONG_SEL:
                    //Ignored but will need to skip the data (1 byte)
                    track->offset++;
                    break;
                case META_EVENT:
                    
                    //Read the meta event and the len                                            
                    meta_event = midi->midi_data[track->offset++];
                    len = midi->midi_data[track->offset++];
                    //update the offset to skip the data

                    //first byte should be the meta event
                    switch (meta_event)
                    {
                        
                        case TEXT:
                        case COPYRIGHT:
                        case TRACK_NAME:
                        case INSTR_NAME:
                        case LYRIC:                                                        
                        case SEQ_NUM:                        
                        case MARKER:
                        case CUE_POINT:
                        case TIME_SIG:
                        case KEY_SIG:
                        case SEQ_SPECIFIC:
                            //We don't care about this data right now so just update offset
                            track->offset += len;
                            break;
                        case EOT:
                            //No more data to read and nothing needs to be sent
                            //Just need to stop processing here
                            printf("Trk: %d EOT\n", track_index); 
                            track->eot_reached = true;
                            return;                        
                        case SET_TEMPO:
                            //Nothing to send but we need to update the timing
                            //function works on 4 bytes so we need to shit everything
                            //since tempo is only 3 bytes
                            data[0] = 0;
                            data[1] = midi->midi_data[track->offset++];
                            data[2] = midi->midi_data[track->offset++];
                            data[3] = midi->midi_data[track->offset++];

                            tempo = big_endian_to_int(data);
#ifndef DEBUG
                            midi->us_per_tick = tempo/midi->time_div;
#endif
printf("\tTrk: %d, setting tempo: %u, time div: %u, us/tick: %u\n", track_index, tempo, midi->time_div, midi->us_per_tick);
                            
                            break;
                        case SMPTE_OFFSET:
                            track->offset += len;
                            //Nothing to send but we need to update the time sig
                            //TODO: update time signature
                            break;                                             
                        default:
                            track->offset += len;
                            //Catch all. OK to ignore anything here
                            break;
                    }
                    break;
                default:
                    //Any that are undefined will be caught here and can be ignored
                    break;
            }            
        }
        else
        {
            //NOTE: This setup assumes that there are not unrecognized messages
            if (new_status)
            {
                message_data[*message_length] = status;
                (*message_length)++;
            }
            //Always add at least 1 byte of message info
            message_data[*message_length] = midi->midi_data[track->offset++];
            (*message_length)++;

            //These are channel voice messages and we only want to look at the first 4 MSB for the switch            
            switch ( status & 0xF0 )
            {
                //The following all use the same amount of data and we just need to send that along
                //(2 byte version)
                case NOTE_OFF: 
                case NOTE_ON:
                case POLY_KEP_PRESS:
                case PITCH_WHEEL:
                case CTRL_CHANGE:
                    //These messages take 2 bytes of message info, so add one more
                    message_data[*message_length] = midi->midi_data[track->offset++];
                    (*message_length)++;                    
                    break;            
                case PROG_CHANGE:                            
                case CHAN_PRESS:
                    //These don't need any more data
                default:
                    //Do nothing
                    break;
            }
        }
    }

}


uint32_t get_next_min_elapsed(midi_info * midi)
{
    uint32_t min = 0xFFFFFF; //no elapsed time should be greater than 2^24

    for (uint8_t i = 0; i < midi->track_cnt; i++)
    {
        if (midi->tracks[i].eot_reached)
        {
            continue;
        }

        if (midi->tracks[i].elapsed < min)
        {
            min = midi->tracks[i].elapsed;
        }
    }

    return min;
}


bool process_all_tracks(midi_info * midi, uint8_t * message_data, uint8_t * message_length, uint32_t * ticks)
{
    // uint32_t min_elapsed = get_next_min_elapsed(midi);
    bool tracks_left_to_process = false;    

    *message_length = 0;

// printf("Min elapsed: %u\n", min_elapsed);

    for (uint8_t i = 0; i < midi->track_cnt; i++)
    {
        if (!midi->tracks[i].eot_reached)
        {
            tracks_left_to_process = true;
        }

// printf("**Processing track %d\n", i);
        if (midi->tracks[i].elapsed > *ticks
            || midi->tracks[i].eot_reached)
        {
// printf("\tSkip Track %d, elapsed: %u, eot: %s\n", i, midi->tracks[i].elapsed, midi->tracks[i].eot_reached ? "t" : "f");
            continue;
        }

        process_track(midi, i, message_data, message_length);
// printf("\tUpdated Track %d, elapsed: %u\n", i, midi->tracks[i].elapsed);        

    }
    
    *ticks = get_next_min_elapsed(midi);
    return tracks_left_to_process;
}
