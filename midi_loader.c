#include "midi.h"
#include <stdlib.h>
#include <stdio.h>
#include "pico/stdlib.h"

#define TEMP_TICKS 16000UL

uint8_t _prev_queue_status = 0;

uint32_t _event_bytes_count = 0;
uint16_t _tempo_change_count = 0;
uint16_t _event_count = 0; //looking for about 8780
uint16_t _tick_count = 0;

uint32_t * _temp_ticks;
uint32_t * _track_addresses;


// Merge two subarrays L and M into _temp_ticks
void merge(uint32_t p, uint32_t q, uint32_t r) 
{

    // Create L ← A[p..q] and M ← A[q+1..r]
    uint32_t n1 = q - p + 1;
    uint32_t n2 = r - q;

    uint32_t L[n1], M[n2];

    for (uint32_t i = 0; i < n1; i++)
        L[i] = _temp_ticks[p + i];
    for (uint32_t j = 0; j < n2; j++)
        M[j] = _temp_ticks[q + 1 + j];

    // Maintain current index of sub-arrays and main array
    uint32_t i, j, k;
    i = 0;
    j = 0;
    k = p;

    // Until we reach either end of either L or M, pick larger among
    // elements L and M and place them in the correct position at A[p..r]
    while (i < n1 && j < n2) 
    {
        if (L[i] <= M[j]) {
            _temp_ticks[k] = L[i];
            i++;
        } else {
            _temp_ticks[k] = M[j];
            j++;
        }
        k++;
     }

  // When we run out of elements in either L or M,
  // pick up the remaining elements and put in A[p..r]
    while (i < n1) 
    {
        _temp_ticks[k] = L[i];
        i++;
        k++;
    }

    while (j < n2) 
    {
        _temp_ticks[k] = M[j];
        j++;
        k++;
    }
}

// Divide the array into two subarrays, sort them and merge them
void ticks_merge_sort(uint32_t l, uint32_t r) 
{
    if (l < r) 
    {

            // m is the point where the array is divided into two subarrays
            uint32_t m = l + (r - l) / 2;

            ticks_merge_sort(l, m);
            ticks_merge_sort(m + 1, r);

            // Merge the sorted subarrays
            merge(l, m, r);
    }
}


void dedup_ticks()
{
    uint32_t j = 0;
    for (uint32_t i = 1; i < _tick_count; i++)
    {
        if (_temp_ticks[i] != _temp_ticks[j] )
        {
            j++;
            _temp_ticks[j] = _temp_ticks[i];
        }
        
    }
    _tick_count = j + 1;
}

midi_result_t realloc_temp_ticks()
{    
    //NOTE: though it shouldn't, this could fail here

    if (!realloc(_temp_ticks, _tick_count * sizeof(uint32_t)))
    {
        return READ_ERROR;
    }

    return OK;
}

void create_tick_data_array(midi_info * midi)
{
    midi->tick_data = 0;
    if (_tick_count > 0)
    {
        midi->tick_data = (midi_tick_data *)malloc((_tick_count + 1) * sizeof(midi_tick_data));


        midi->tick_data[0].ticks = 0;

        for (uint16_t i = 0; i < _tick_count; i++)
        {
            midi->tick_data[i+1].ticks = _temp_ticks[i];
        }

        printf("Tick Data Array created at 0x%08x\n", midi->tick_data);
    }
    //Add one for 0 ticks
    _tick_count++; 
}

void create_event_bytes_array(midi_info * midi)
{
    midi->event_bytes = 0;
    if (_event_bytes_count > 0)
    {
        midi->event_bytes = (uint8_t *)malloc(_event_bytes_count);
        printf("Event Bytes Array created at 0x%08x\n", midi->event_bytes);
    }
}

void create_tempo_data_array(midi_info * midi)
{
    midi->tempo_data = 0;
    if (_tempo_change_count > 0)
    {
        midi->tempo_data = (midi_tempo_data *)malloc(_tempo_change_count * sizeof(midi_tempo_data));
        printf("Tempo Data Array created at 0x%08x\n", midi->tempo_data);
    }
}

void create_track_addresses(midi_info * midi)
{
    _track_addresses = (uint32_t *)malloc(midi->track_cnt * sizeof(uint32_t));
    for (uint8_t i = 0; i < midi->track_cnt; i++)
    {
        _track_addresses[i] = midi->tracks[i].address;
    }
}

void reset_track_data(midi_info * midi)
{
    for (uint8_t i = 0; i < midi->track_cnt; i++)
    {
        midi->tracks[i].delta = 0;
        midi->tracks[i].elapsed = 0;
        midi->tracks[i].eot_reached = false;
        midi->tracks[i].read_delta = true;
        midi->tracks[i].status = 0;
        midi->tracks[i].address = _track_addresses[i];
        
    }
}

midi_result_t process_track_counts(midi_info * midi, uint8_t index)
{
    uint8_t data[3];
    uint8_t meta_channel = 0;
    uint8_t meta_event = 0;
    uint8_t len = 0;
    bool new_status = false;
    track_info * track = &(midi->tracks[index]);

    if (f_lseek(midi->file, track->address) != OK) return READ_ERROR;

    //EoT event is the exit
    while(1)
    {

        if (get_VLQ(midi->file, &( track->address), &(track->delta)) != OK) return READ_ERROR;  

        //Going to use this as a flag to indicate a delta change
        track->read_delta = false; 

        if (track->delta > 0)
        {
            track->read_delta = true;
            track->elapsed += track->delta;   
            _temp_ticks[_tick_count++] = track->elapsed;
        }

        if (read_status_data(midi->file, track, &new_status) != OK) return READ_ERROR;  

        if ((track->status & 0xF0) == 0xF0)
        {
            
            switch(track->status)
            {
                //These are Sys common and sys real-time messages
                case SYS_EX:
                //TODO: read and skip data. We don't care about these
                    break;
                case MIDI_START:
                case MIDI_CONT:
                case MIDI_STOP:
                    //Nothing to send here. We will use this as a control here
                    break;
                case SONG_SEL:
                    //Ignored but will need to skip the data
                    if (midi_move_file_pointer(midi->file, 1, &track->address) != OK) return READ_ERROR;

                    break;
                case META_EVENT:
                    
                    //Read the meta event and the len
                    if (read_current_data(midi->file, &(track->address), data, 2) != OK) return READ_ERROR;  
                    //go ahead and read the data too
                    meta_event = data[0];
                    len = data[1];

                    if (midi_move_file_pointer(midi->file, len, &track->address) != OK) return READ_ERROR;
                    //first byte should be the meta event
                    switch (meta_event)
                    {
                        
                        case TEXT:
                        case COPYRIGHT:
                        case TRACK_NAME:
                        case INSTR_NAME:
                        case LYRIC:
                            // printf("Trk: %d, %*s\n", idx_d->index, len, data);
                            //break;
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
                            track->eot_reached = true;
                            return OK;                        
                        case SET_TEMPO:
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
                    if (midi_move_file_pointer(midi->file, 2, &track->address) != OK) return READ_ERROR;
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
                    if (midi_move_file_pointer(midi->file, 1, &track->address) != OK) return READ_ERROR;
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


           
    }
}

midi_result_t process_track_data(midi_info * midi, uint8_t index)
{
    uint8_t data[3];
    uint8_t meta_channel = 0;
    uint8_t meta_event = 0;
    uint32_t tempo = 0;
    uint8_t len = 0;
    bool new_status = false;
    track_info * track = &(midi->tracks[index]);

// printf("Processing track %d. Last elapsed: %u, Addr: 0x%08x\n", index, track->elapsed, track->address);
    if (f_lseek(midi->file, track->address) != OK) return READ_ERROR;

    //EoT event is the exit
    while(1)
    {

        if (track->read_delta)
        {
            if (get_VLQ(midi->file, &( track->address), &(track->delta)) != OK) return READ_ERROR;  
        }
// printf("Track %d delta: %u\n", index, track->delta);

        //Exit if we have a new delta > 0, and also that
        //we actually read a new delta
        if (track->delta > 0 && track->read_delta)
        {
            track->read_delta = false;
            track->elapsed += track->delta;   
            return OK;
        }

        if (read_status_data(midi->file, track, &new_status) != OK) return READ_ERROR;  

        if ((track->status & 0xF0) == 0xF0)
        {
            
            switch(track->status)
            {
                //These are Sys common and sys real-time messages
                case SYS_EX:
                //TODO: read and skip data. We don't care about these
                    break;
                case MIDI_START:
                case MIDI_CONT:
                case MIDI_STOP:
                    //Nothing to send here. We will use this as a control here
                    break;
                case SONG_SEL:
                    //Ignored but will need to skip the data
                    if (midi_move_file_pointer(midi->file, 1, &track->address) != OK) return READ_ERROR;

                    break;
                case META_EVENT:
                    
                    //Read the meta event and the len
                    if (read_current_data(midi->file, &(track->address), data, 2) != OK) return READ_ERROR;  
                    //go ahead and read the data too
                    meta_event = data[0];
                    len = data[1];

                    if (midi_move_file_pointer(midi->file, len, &track->address) != OK) return READ_ERROR;
                    //first byte should be the meta event
                    switch (meta_event)
                    {
                        
                        case TEXT:
                        case COPYRIGHT:
                        case TRACK_NAME:
                        case INSTR_NAME:
                        case LYRIC:
                            // printf("Trk: %d, %*s\n", idx_d->index, len, data);
                            //break;
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
                            track->eot_reached = true;
                            return OK;                        
                        case SET_TEMPO:
                            //Nothing to send but we need to update the timing
                            //function works on 4 bytes so we need to shift everything
                            //since tempo is only 3 bytes
                            data[3] = data[2];
                            data[2] = data[1];
                            data[1] = data[0];
                            data[0] = 0;
printf("Adding tempo change #%d. current ticks: %u\n", _tempo_change_count, track->elapsed);
                            midi->tempo_data[_tempo_change_count++].tempo = big_endian_to_int(data);                                              
                            midi->tempo_data[_tempo_change_count].ticks = track->elapsed;

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
            switch ( track->status & 0xF0 )
            {
                //The following all use the same amount of data and we just need to send that along
                //(2 byte version)
                case NOTE_OFF: 
                case NOTE_ON:                      
                case POLY_KEP_PRESS:
                case PITCH_WHEEL:
                case CTRL_CHANGE:
                    if (read_current_data(midi->file, &( track->address), data, 2) != OK) return READ_ERROR; 
                    if (new_status || !track->read_delta)
                    {
                        midi->event_bytes[_event_bytes_count++] = track->status;
                    }                    
                    midi->event_bytes[_event_bytes_count++] = data[0];
                    midi->event_bytes[_event_bytes_count++] = data[1];

                    break;
                //The following all use the same amount of data and we just need to send that along
                //(1 byte version)                
                case PROG_CHANGE:                            
                case CHAN_PRESS:
                    if (read_current_data(midi->file, &( track->address), data, 1) != OK) return READ_ERROR; 
                    if (new_status || !track->read_delta)
                    {
                        midi->event_bytes[_event_bytes_count++] = track->status;
                    }                    
                    midi->event_bytes[_event_bytes_count++] = data[0];
                
                    break;
                default:
                    //Do nothing
                    break;
            }
        }  

        //Set this at the end so we can use it as an 
        //indicator to include the status byte in the 
        //recorded event data
        track->read_delta = true; 
           
    }
}

uint32_t get_next_earliest_tick(midi_info * midi)
{
    u_int32_t min = 0xFFFFFFFF;

    for (uint8_t i = 0; i < midi->track_cnt; i++)
    {
        if (midi->tracks[i].eot_reached)
        {
            continue;
        }

        if (min > midi->tracks[i].elapsed)
        {
            min = midi->tracks[i].elapsed;
        }
    }

    return min;
}

midi_result_t process_all_tracks(midi_info * midi)
{    
    uint32_t min_ticks = 0;
    uint32_t elapsed_ticks = 0;

    create_track_addresses(midi);
    _tick_count = 0;
    _event_bytes_count = 0;
    _tempo_change_count = 0;    
    _event_count = 0;
    _temp_ticks = (uint32_t *)malloc(TEMP_TICKS * sizeof(uint32_t));

    //First Pass: Get counts

    for (uint8_t i = 0; i < midi->track_cnt; i++)
    {            
        process_track_counts(midi, i);
    }
    
    //Allocate arrays
    midi->tick_count = _tick_count;
    ticks_merge_sort(0, _tick_count - 1);
    dedup_ticks();
    realloc_temp_ticks();
    create_tick_data_array(midi);
    create_event_bytes_array(midi);
    create_tempo_data_array(midi);
    reset_track_data(midi);
    printf("Event Bytes: %u, Event Count: %u, Unique Tick Count: %u, Tempo Changes: %u\n", _event_bytes_count, _event_count, _tick_count, _tempo_change_count);

    //Second pass: load data
// for(uint16_t i = 0; i < _tick_count; i++)
// {
//     printf("Ticks %u: %u\n", i, midi->tick_data[i].ticks);
// }


    //These will now be used as indexes.
    bool all_tracks_eot = false;
    
    _event_bytes_count = 0;
    _tempo_change_count = 0;    
    _tick_count = 0;
    
    while(!all_tracks_eot)
    {
        
        //The current tick event should match the previous min ticks
        //for the first one, this should be 0;
        midi->tick_data[_tick_count].data_index = _event_bytes_count;
// printf("**Event data %d. start: %u, ticks: %u, elapsed: %u**\n", _tick_count, _event_bytes_count, midi->tick_data[_tick_count].ticks, elapsed_ticks);
        all_tracks_eot = true;

        for (uint8_t i = 0; i < midi->track_cnt; i++)
        {   
            if (!midi->tracks[i].eot_reached)
            {
                all_tracks_eot = false;
            }

            //Skip any track at EoT or any track that's not 
            //ready to be processed based on elapsed ticks
            if ((midi->tracks[i].eot_reached
            || midi->tracks[i].elapsed > elapsed_ticks) && _tick_count > 0)
            {
// printf("Track %d skipped, eot: %d, track elapsed: %u, current elapsed %u\n", i, midi->tracks[i].eot_reached, midi->tracks[i].elapsed, elapsed_ticks);
                continue;
            }

            
            process_track_data(midi, i);
            
//  printf("Track %d, new elapsed: %u\n", i, midi->tracks[i].elapsed);

        }

        // elapsed_ticks = get_next_earliest_tick(midi);
        midi->tick_data[_tick_count].length = _event_bytes_count - midi->tick_data[_tick_count].data_index;
        _tick_count++;    
        elapsed_ticks = midi->tick_data[_tick_count].ticks;


        // printf("Elapsed: %u. All EoT: %d\n", elapsed_ticks, all_tracks_eot);
// printf("Ending event data %d. length: %u. ticks: %u, elapsed: %u\n", _tick_count,  midi->tick_data[_tick_count].length, midi->tick_data[_tick_count].ticks, elapsed_ticks); 
        

//         if (elapsed_ticks >= 57599)
//         {
// printf("___EXIT___\n");
//             break;
//         }
    }




    return OK;
}


/*

midi_result_t process_track_counts(midi_info * midi, uint8_t index)
{
    uint8_t data[3];
    uint8_t meta_channel = 0;
    uint8_t meta_event = 0;
    uint8_t len = 0;
    uint32_t tempo = 0;    

    bool new_status = false;
    track_info * track = &(midi->tracks[index]);


    // printf("Processing track %d. Last elapsed: %u, Size: %u, Addr: 0x%08x\n", idx_d->index, idx_d->ticks, track->size, track->address);
    // printf("\tProcessing till %u\n", next_ticks);

    if (f_lseek(midi->file, track->address) != OK) return READ_ERROR;


    //keep processing events until the delta is non-zero
    while(1)
    {
        // if (track->read_delta)
        // {
            if (get_VLQ(midi->file, &( track->address), &(track->delta)) != OK) return READ_ERROR;  
        // }

            //Going to use this as a flag to indicate a delta change
            track->read_delta = false; 

            if (track->delta > 0)
            {
                track->read_delta = true;
                track->elapsed += track->delta;   
                _temp_ticks[_tick_count++] = track->elapsed;
            }

        // if (track->delta > 0 && track->read_delta)
        // {
        //     idx_d->ticks += track->delta;
        //     // printf("Setting track %d elapsed to: %u\n", idx_d->index, idx_d->ticks);
        //     //Make sure we skip reading the delta next time because we already have it
           
        //     if (idx_d->ticks != next_ticks)
        //     {
        //          _unique_ticks_count++;
        //     }

        //     if (idx_d->ticks > next_ticks)
        //     {
        //         track->read_delta = false;
        //         //We want to replay the status on each delta change
        //         // printf("Exiting track processing\n");
        //         return OK;
        //     }

           
        // }
        
        

        if (read_status_data(midi->file, track, &new_status) != OK) return READ_ERROR;  
        // printf("Track: %d, processing status: 0x%02x. Event %u. Ticks: %u\n", idx_d->index, track->status, _event_count, idx_d->ticks);
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
                    if (midi_move_file_pointer(midi->file, 1) != OK) return READ_ERROR;
                    // if (read_current_data(midi->file, &(track->address), data, 1) != OK) return READ_ERROR; 
                    break;
                case META_EVENT:
                    
                    //Read the meta event and the len
                    if (read_current_data(midi->file, &(track->address), data, 2) != OK) return READ_ERROR;  
                    //go ahead and read the data too
                    meta_event = data[0];
                    len = data[1];
                    // printf("\tTrk %d, meta event 0x%02x, len: %d\n", index, meta_event, len);
                    // if (read_current_data(midi->file, &(track->address), data, len) != OK) return READ_ERROR;  
                    if (midi_move_file_pointer(midi->file, len) != OK) return READ_ERROR;
                    //first byte should be the meta event
                    switch (meta_event)
                    {
                        
                        case TEXT:
                        case COPYRIGHT:
                        case TRACK_NAME:
                        case INSTR_NAME:
                        case LYRIC:
                            // printf("Trk: %d, %*s\n", idx_d->index, len, data);
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
                            track->eot_reached = true;
                            return OK;                        
                        case SET_TEMPO:
                            //Nothing to send but we need to update the timing
                            //function works on 4 bytes so we need to shit everything
                            //since tempo is only 3 bytes
//                             data[3] = data[2];
//                             data[2] = data[1];
//                             data[1] = data[0];
//                             data[0] = 0;
//                             tempo = big_endian_to_int(data);
// #ifndef DEBUG
//                             midi->us_per_tick = tempo/midi->time_div;
// #endif
//                             printf("Trk: %d, setting tempo: %u, time div: %u, us/tick: %u\n", idx_d->index, tempo, midi->time_div, midi->us_per_tick);

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
                    // if (read_current_data(midi->file, &( track->address), data, 2) != OK) return READ_ERROR; 
                    if (midi_move_file_pointer(midi->file, 2) != OK) return READ_ERROR;
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
                    // if (read_current_data(midi->file, &( track->address), data, 1) != OK) return READ_ERROR; 
                    if (midi_move_file_pointer(midi->file, 1) != OK) return READ_ERROR;
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


           
    }
}

*/