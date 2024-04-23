#include "midi.h"
#include <stdlib.h>
#include <stdio.h>
#include "pico/stdlib.h"


uint8_t _prev_queue_status = 0;

uint16_t _unique_ticks_count = 0;
uint16_t _event_bytes_count = 0;
uint16_t _tempo_change_count = 0;
uint32_t _last_min_ticks = 0;
uint16_t _event_count = 0; //looking for about 8780

typedef struct
{
    uint32_t ticks;
    uint8_t index;
} indexed_delta;


//indexed_delta * _deltas = 0;
indexed_delta _deltas[8];


void clear_deltas(uint8_t track_count)
{
    for (uint8_t i = 0; i < track_count; i++)
    {
        _deltas[i].ticks = 0;
        _deltas[i].index = i;
    }
}


void create_deltas(uint8_t track_count)
{
    uint32_t size = track_count * sizeof(indexed_delta);
    printf("Allocating indexed_delta size: %u\n", size);
    //_deltas = (indexed_delta *)malloc(size);
    printf("Allocated. Clearing\n");
    clear_deltas(track_count);
}

void destroy_deltas()
{
    if (_deltas)
    {
        // free(_deltas);
    }
}

void sort_indexes_by_ticks(uint8_t track_count)
{
    indexed_delta temp;

    for (uint8_t i = 0; i < track_count; i++)
    {
        for (uint8_t j = 0; j < track_count - i - 1; j++)
        {
            if (_deltas[j].ticks > _deltas[j + 1].ticks)
            {
                temp = _deltas[j];
                _deltas[j] = _deltas[j+1];
                _deltas[j+1] = temp;
            }
        }
    }
}

void update_unique_tick_count(uint8_t current_index, uint8_t track_count)
{
    for (uint8_t i = 0; i < track_count; i++)
    {
        if (i == current_index)
        {
            continue;
        }
        
        //we found a match so no need to update count because the ticks are not unique
        //so just exit
        if (_deltas[i].ticks == _last_min_ticks)
        {
            return;
        }
    }

    //If we reached here, we have a new tick count so update the unique tick count
    _unique_ticks_count++;
}

bool tracks_left_to_process(midi_info * midi)
{
    for (uint8_t i = 0; i < midi->track_cnt; i++)
    {
        // printf("checking track %d for EoT\n", i);
        if (!midi->tracks[i].eot_reached)
        {
            return true;
        }
    }

    return false;
}

midi_result_t process_track(midi_info * midi, indexed_delta * idx_d)
{
    uint8_t data[255];
    uint8_t meta_channel = 0;
    uint8_t meta_event = 0;
    uint8_t len = 0;
    uint32_t tempo = 0;    

    bool new_status = false;
    track_info * track = &(midi->tracks[idx_d->index]);


        // printf("Processing track %d. Last elapsed: %u, Size: %u, Addr: 0x%08x\n", idx_d->index, idx_d->ticks, track->size, track->address);

    if (f_lseek(midi->file, track->address) != OK) return READ_ERROR;


    //keep processing events until the delta is non-zero
    while(1)
    {
        if (track->read_delta)
        {
            if (get_VLQ(midi->file, &( track->address), &(track->delta)) != OK) return READ_ERROR;  
        }

        if (track->delta > 0 && track->read_delta)
        {
            idx_d->ticks += track->delta;
            // printf("Setting track %d elapsed to: %u\n", idx_d->index, idx_d->ticks);
            //Make sure we skip reading the delta next time because we already have it
            track->read_delta = false;
            //We want to replay the status on each delta change
           
            return OK;
        }
        
        

        if (read_status_data(midi->file, track, &new_status) != OK) return READ_ERROR;  
        // printf("Track: %d, processing status: 0x%02x. Event %u\n", idx_d->index, track->status, _event_count);
        if ((track->status & 0xF0) == 0xF0)
        {
            
            switch(track->status)
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
                    //Ignored but will need to skip the data
                    if (read_current_data(midi->file, &(track->address), data, 1) != OK) return READ_ERROR; 
                    break;
                case META_EVENT:
                    
                    //Read the meta event and the len
                    if (read_current_data(midi->file, &(track->address), data, 2) != OK) return READ_ERROR;  
                    //go ahead and read the data too
                    meta_event = data[0];
                    len = data[1];
                    // printf("\tTrk %d, meta event 0x%02x, len: %d\n", index, meta_event, len);
                    if (read_current_data(midi->file, &(track->address), data, len) != OK) return READ_ERROR;  
                    //first byte should be the meta event
                    switch (meta_event)
                    {
                        
                        case TEXT:
                        case COPYRIGHT:
                        case TRACK_NAME:
                        case INSTR_NAME:
                        case LYRIC:
                            printf("Trk: %d, %*s\n", idx_d->index, len, data);
                            break;
                        case SEQ_NUM:                        
                        case MARKER:
                        case CUE_POINT:
                        case TIME_SIG:
                        case KEY_SIG:
                        case SEQ_SPECIFIC:
                            //We don't care about this data right now so just skip                        
                            //Nothing to send
                            break;
                        case EOT:
                            //No more data to read and nothing needs to be sent
                            //Just need to stop processing here
                            printf("Trk: %d EOT\n", idx_d->index); 
                            track->eot_reached = true;
                            return OK;                        
                        case SET_TEMPO:
                            //Nothing to send but we need to update the timing
                            //function works on 4 bytes so we need to shit everything
                            //since tempo is only 3 bytes
                            data[3] = data[2];
                            data[2] = data[1];
                            data[1] = data[0];
                            data[0] = 0;
                            tempo = big_endian_to_int(data);
#ifndef DEBUG
                            midi->us_per_tick = tempo/midi->time_div;
#endif
                            printf("Trk: %d, setting tempo: %u, time div: %u, us/tick: %u\n", idx_d->index, tempo, midi->time_div, midi->us_per_tick);

                            _tempo_change_count++;
                            
                            break;
                        case SMPTE_OFFSET:
                            //Nothing to send but we need to update the time sig
                            //TODO: update time signature
                            break;                                             
                        default:
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
            //These are channel voice messages and we only want to look at the first 4 MSB for the switch
            // printf("\t\tchannel msg\n");
            switch ( track->status & 0xF0 )
            {
                //The following all use the same amount of data and we just need to send that along
                //(2 byte version)
                case NOTE_OFF: 
                case NOTE_ON:
                      _event_count++;
                case POLY_KEP_PRESS:
                case PITCH_WHEEL:
                case CTRL_CHANGE:
                    if (read_current_data(midi->file, &( track->address), data, 2) != OK) return READ_ERROR; 
                    _event_bytes_count += 2;
                    if (new_status || !track->read_delta)
                    {
                        _event_bytes_count++;
                    }
                  
                    break;
                //The following all use the same amount of data and we just need to send that along
                //(1 byte version)                
                case PROG_CHANGE:                            
                case CHAN_PRESS:
                    if (read_current_data(midi->file, &( track->address), data, 1) != OK) return READ_ERROR; 
                    _event_bytes_count += 1;
                    if (new_status || !track->read_delta)
                    {
                        _event_bytes_count++;
                    }
                
                    break;
                default:
                    //Do nothing
                    break;
            }
        }  

        //Waiting till the end here to reset this flag because it is
        //being used to check for a delta change
        track->read_delta = true;      
    }
}


midi_result_t process_all_tracks(midi_info * midi)
{
    _unique_ticks_count = 0; 
    _event_bytes_count = 0;
    _tempo_change_count = 0;
    _last_min_ticks = 0;
    _event_count = 0;
    destroy_deltas();
    create_deltas(midi->track_cnt);
    //First Pass: Get counts
    while(tracks_left_to_process(midi))
    {
        // printf("Starting track pass. Event Count: %u\n", _event_count);
// sleep_ms(100);
        for (uint8_t i = 0; i < midi->track_cnt; i++)
        {
            //Skip to the next if the track is at EoT or it's
            //current tick count is not less/equal to the last
            if (midi->tracks[_deltas[i].index].eot_reached
                || _deltas[i].ticks > _last_min_ticks)
            {
                // if (midi->tracks[_deltas[i].index].eot_reached)
                // {
                //     printf("Skipping track %d. EoT\n", _deltas[i].index);    
                // }
                // else
                // {
                //     printf("Skipping track %d. Ticks: %u, Last min ticks: %u\n", _deltas[i].index, _deltas[i].ticks, _last_min_ticks);
                // }
                continue;
            }

            
            process_track(midi, _deltas + i);

            //The returned value may still be less than the next
            //but it should always be greater than the last
            //so it can always be updated
            _last_min_ticks = _deltas[i].ticks;

            //We want to update the count if the 
            if (!(midi->tracks[_deltas[i].index].eot_reached))
            {
                update_unique_tick_count(i, midi->track_cnt);
            }

        }

        sort_indexes_by_ticks(midi->track_cnt);
    }
    
    //Allocate arrays

    //Second pass: load data

    printf("Event Bytes: %u, Unique Tick Count: %u, Tempo Changes: %u, Ticks: %u\n", _event_bytes_count, _unique_ticks_count, _tempo_change_count, _last_min_ticks);

    midi->event_bytes = (uint8_t *) malloc(_event_bytes_count);

    return OK;
}

/*

midi_result_t process_track_old(uint8_t index, midi_info * midi)
{

    
    uint8_t data[255];
    uint8_t meta_channel = 0;
    uint8_t meta_event = 0;
    uint8_t len = 0;
    uint32_t tempo = 0;
    //  printf("--Pocessing Track %d @ 0x%08x. Read delta: %d\n", index, midi->tracks[index].address, midi->tracks[index].read_delta);
    if (f_lseek(&_current_file, midi->tracks[index].address) != OK) return READ_ERROR;

    // while(1)
    {
        //So we can skip it if a non-0 delta was read last and just continue at the status byte
        // if ( midi->tracks[index].read_delta)
        // {
        if (get_VLQ(&_current_file, &( midi->tracks[index].address), &( midi->tracks[index].delta)) != OK) return READ_ERROR;  
        // }

        // printf("\t*Trk %d checking for 0 delta\n", index);
        //If we reached a non-0 delta, we will wait and then process the data.        
        // if ( midi->tracks[index].delta > 0)
        // {
        //     printf("\t*Trk: %d, Found a delta > 0: %d\n", index, midi->tracks[index].delta);
        //     //Make sure we skip reading the delta next time because we already have it
        //     midi->tracks[index].read_delta = false;
        //     return OK;
        // }

        //Reset the delta flag 
        // midi->tracks[index].read_delta = true;        
        // printf("\t*Trk %d getting status\n", index);
        if (read_status_data(&_current_file, &(midi->tracks[index])) != OK) return READ_ERROR;  
       
        // printf("\t*Trk %d status: 0x%02x\n", index,  midi->tracks[index].status);

        if ((midi->tracks[index].status & 0xF0) == 0xF0)
        {
            // printf("\t\tsys msg\n");
            switch(midi->tracks[index].status)
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
                    //Ignored but will need to skip the data
                    if (read_current_data(&_current_file, &( midi->tracks[index].address), data, 1) != OK) return READ_ERROR; 
                    break;
                case META_EVENT:
                    
                    //Read the meta event and the len
                    if (read_current_data(&_current_file, &( midi->tracks[index].address), data, 2) != OK) return READ_ERROR;  
                    //go ahead and read the data too
                    meta_event = data[0];
                    len = data[1];
                    // printf("\tTrk %d, meta event 0x%02x, len: %d\n", index, meta_event, len);
                    if (read_current_data(&_current_file, &( midi->tracks[index].address), data, len) != OK) return READ_ERROR;  
                    //first byte should be the meta event
                    switch (meta_event)
                    {
                        
                        case TEXT:
                        case COPYRIGHT:
                        case TRACK_NAME:
                        case INSTR_NAME:
                        case LYRIC:
                            printf("Trk: %d, %*s\n", index, len, data);
                            break;
                        case SEQ_NUM:                        
                        case MARKER:
                        case CUE_POINT:
                        case TIME_SIG:
                        case KEY_SIG:
                        case SEQ_SPECIFIC:
                            //We don't care about this data right now so just skip                        
                            //Nothing to send
                            break;
                        case EOT:
                            //No more data to read and nothing needs to be sent
                            //Just need to stop processing here
                            printf("Trk: %d EOT\n", index); 
                            midi->tracks[index].eot_reached = true;
                            return OK;                        
                        case SET_TEMPO:
                            //Nothing to send but we need to update the timing
                            //function works on 4 bytes so we need to shit everything
                            //since tempo is only 3 bytes
                            data[3] = data[2];
                            data[2] = data[1];
                            data[1] = data[0];
                            data[0] = 0;
                            tempo = big_endian_to_int(data);
#ifndef DEBUG
                            midi->us_per_tick = tempo/midi->time_div;
#endif
                            printf("Trk: %d, setting tempo: %u, time div: %u, us/tick: %u\n", index, tempo, midi->time_div, midi->us_per_tick);
                            
                            break;
                        case SMPTE_OFFSET:
                            //Nothing to send but we need to update the time sig
                            //TODO: update time signature
                            break;                                             
                        default:
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
            //These are channel voice messages and we only want to look at the first 4 MSB for the switch
            // printf("\t\tchannel msg\n");
            switch ( midi->tracks[index].status & 0xF0 )
            {
                //The following all use the same amount of data and we just need to send that along
                //(2 byte version)
                case NOTE_OFF: 
                case NOTE_ON:
                case POLY_KEP_PRESS:
                case PITCH_WHEEL:
                case CTRL_CHANGE:
                    if (read_current_data(&_current_file, &( midi->tracks[index].address), data, 2) != OK) return READ_ERROR; 
                    // _message_cb( midi->tracks[index].status, data, 2, index);   
                    add_to_queue(midi->tracks[index].status, data[0], data[1], midi->tracks[index].delta);
                    break;
                //The following all use the same amount of data and we just need to send that along
                //(1 byte version)                
                case PROG_CHANGE:                            
                case CHAN_PRESS:
                    if (read_current_data(&_current_file, &( midi->tracks[index].address), data, 1) != OK) return READ_ERROR; 
                    // _message_cb( midi->tracks[index].status, data, 1, index);   
                    add_to_queue(midi->tracks[index].status, data[0], 0xFF, midi->tracks[index].delta);
                    break;
                default:
                    //Do nothing
                    break;
            }
        }
       



    } 
    
    // printf("Trk %d, Broke out early\n");
    // return READ_ERROR;
    return OK;
}

*/

/*


midi_result_t process_all_tracks_old(midi_info * midi, uint32_t tick)
{
    _current_ticks = tick;
 printf("Prcessing tracks at tick %u\n", _current_ticks);
    while(cb_count() < BUFF_ADD_LEVEL)
    {
        // if (cb_count() >= BUFF_ADD_LEVEL)
        // {
        //     return OK;
        // }

       

        for (uint8_t i = 0; i < midi->track_cnt; i++)
        {
            //Skip any tracks that have reached EoT
            if (midi->tracks[i].eot_reached)
            {
                continue;
            }

            //Will process all of the delta 0 messages and the first non-0 delta will be stored
            //(Just the delta)
            // if (midi->tracks[i].delta == 0)
            // {
                
                if (process_track_old(i, midi) != OK) return READ_ERROR;
            // }
            // else
            // {
            //     midi->tracks[i].delta--;
            // }

        }
    }

    // if (_stream_index)
    // {
    //     _msg_stream_cb(_msg_stream, _stream_index);
    // }
    // _stream_index = 0;
    // printf("All tracks processed\n");
    return OK;
}



// midi_result_t midi_init(void (* msg_stream_cb)(uint8_t * stream, uint8_t length))

*/