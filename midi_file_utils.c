
#include "midi.h"
#include "sd_card.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "hw_config.h"

bool _is_initialized = false;
sd_card_t * _sd_ptr;
FIL _current_file;
DIR _root_dir;
uint16_t _file_index;

//Reads the specified number of bytes at the current file ptr and updates the address with that len.
midi_message_t read_status_data(FIL * fptr, track_info * track, bool * new_status)
{
    uint8_t status;
    FSIZE_t curr_ptr = f_tell(fptr);
    //Just advance the file ptr                
    if (f_read(fptr, &status, 1, 0) != FR_OK) return READ_ERROR;   

    if ((status & 0x80))
    {
        // printf("New status: 0x%02x\n", track->status);
        //new status
        *new_status = true;
        track->offset++;
        track->status = status;
    }
    else
    {
        *new_status = false;
        //return the file pointer to where it was
        if (f_lseek(fptr, curr_ptr) != FR_OK) return READ_ERROR;       
    }
    
    return OK;
}

//Reads the specified number of bytes at the current file ptr and updates the address with that len.
midi_message_t read_current_data(FIL * fptr, uint32_t * address, uint8_t * buff, uint8_t len)
{
    if (len == 0) return OK;

    //Just advance the file ptr                
    if (f_read(fptr, buff, len, 0) != FR_OK) return READ_ERROR;   
    //Bump the address by 2 since we read 2
    *address += len;    
    return OK;
}

//Peeks the specified number of bytes at the current file ptr without moving the pointer
midi_message_t peek_current_data(FIL * fptr, uint8_t * buff, uint8_t len)
{
    if (len == 0) return OK;
    FSIZE_t curr_ptr = f_tell(fptr);
    //Just advance the file ptr                
    if (f_read(fptr, buff, len, 0) != FR_OK) return READ_ERROR;   
    //return the file pointer to where it was
    if (f_lseek(fptr, curr_ptr) != FR_OK) return READ_ERROR;
    return OK;
}

/// @brief Set up to read a variable length quantity, up to 32 bits
/// @param data the data array to read from
/// @param offset the byte offset in that array
/// @param iptr a pointer to a 32bit unsigned int value for the VQL
/// @return the number of bytes read
uint8_t get_VLQ(uint8_t * data, uint32_t offset, uint32_t * iptr)
{
    uint8_t byte;
    uint32_t value = 0;  
    uint8_t count = 0;

    do
    {
        value <<= 7;;
        byte =  data[offset+count];
        value += byte & 0x7F;
        count++;
    //If the MSB is set then we are still processing a VLQ    
    } while ((byte & 0x80));
    
    *iptr = value;  
    return count;  
}


void load_word(uint8_t * data, uint32_t offset, uint16_t * value)
{
    *value = big_endian_to_word(data + offset);    
}

void load_midi_type(midi_info * midi)
{
    load_word(midi->midi_data, 8, &midi->type);
}

void load_track_count(midi_info * midi)
{
    load_word(midi->midi_data, 10, &midi->track_cnt);
}

void load_time_div(midi_info * midi)
{
    load_word(midi->midi_data, 12, &midi->time_div);
}

midi_result_t load_midi_file(FIL * fptr, midi_info * midi)
{
    FSIZE_t size = f_size(fptr);
    
//TODO: Check max file size

    printf("Loading midi file. Size: %llu\n", size);
    
    //First make sure we get rid of any previous file
    if (midi->midi_data)
    {
        free(midi->midi_data);
    }

    midi->midi_data = (uint8_t *)malloc(size);

    if (f_lseek(fptr, 0) != OK) return READ_ERROR;
    if (f_read(fptr, midi->midi_data, size, 0) != OK) return READ_ERROR;

    printf("Midi file loaded\n");

    return OK;
}

void init_track(track_info * track, uint32_t offset, uint32_t chunk_size)
{
    track->offset = offset; //address should be first actual data byte after size
    track->size = chunk_size;
    track->read_delta = true;
    track->delta = 0;
    track->status = 0;
    track->eot_reached = false;
    track->elapsed = 0;    
}

void load_track_chunk_info(midi_info * midi) 
{   
    uint8_t buff[4];
    uint32_t chunk_size;
    UINT bytes_read = 0;
    uint32_t offset = 14 + 4; //Start of first chunk + sig

    //Make sure to free up any previous tracks
    if (midi->tracks)
    {
        free(midi->tracks);
    }

    midi->tracks = (track_info *)malloc(sizeof(track_info) * midi->track_cnt);

    for(uint8_t i = 0; i < midi->track_cnt; i++)
    {

        chunk_size = big_endian_to_int(midi->midi_data + offset); //Chunk size
        offset += 4; //move to start of data
        init_track(midi->tracks + i, offset, chunk_size);
        
        offset += chunk_size + 4; //skip next sig
        //TODO: make sure this accounts for type 0 correctly.
        if (midi->type == 0)
        {
            // printf("\tType 0\n");
            return;
        }
    }    
}


midi_result_t midi_get_file_info(FIL * fptr, midi_info * midi)
{   
  
    if (load_midi_file(fptr, midi) != OK) return READ_ERROR;
    load_midi_type(midi);
    load_time_div(midi);
    load_track_count(midi);
    load_track_chunk_info(midi);

    return OK;
}


midi_result_t midi_open_sd()
{
    if (_sd_ptr)
    {
        f_unmount(_sd_ptr->pcName);
    }

    _sd_ptr = sd_get_by_num(0);
    FRESULT fr = f_mount(&_sd_ptr->fatfs, _sd_ptr->pcName, 1);

    if (FR_OK != fr) 
    {
        return READ_ERROR;
    }

    return OK;
}

midi_result_t midi_mount_root(DIR * root)
{
    FRESULT fr = f_opendir(root, "/");

    if (FR_OK != fr) 
    {
        return READ_ERROR;
    }

    return OK;
}

//Move forward from the first midi found until we get to the 0-based index of the file at that index
midi_result_t get_file_at_index(uint16_t index, midi_info * midi)
{
    FIL fptr;
    FILINFO f_info;    
    midi->index = index;
    //Always start at the beginning
    FRESULT res = f_findfirst(&_root_dir, &f_info, "", "*.mid*");

    if (res != FR_OK) return READ_ERROR;

    //We can't request anything lest than index 0, so if we can't even find the first file, then none exists.
    if (f_info.fname[0] == 0) return NO_MIDI;


    while (index-- > 0)
    {

        res = f_findnext(&_root_dir, &f_info);

        if (res != FR_OK) return READ_ERROR;

        //If this is true, then we were given an index beyond the number of midis that exist
        if (f_info.fname[0] == 0) return NO_MIDI;
        
    }

    strcpy(midi->name, f_info.fname); 
    printf("Mounting file: %s\n", f_info.fname);
    if (f_open(&fptr, f_info.fname, FA_READ) != OK) return READ_ERROR;  
    //If we reach here, we found the midi file at the index requested;
    printf("Loading midi file\n");
    if (midi_get_file_info(&fptr, midi) != FR_OK) return READ_ERROR;
    //Make sure to close the file
    return f_close(&fptr);
}

//Start and the beginning and move through all the midis till no more are found.
//Use the last good index
midi_result_t get_last_file_index(uint16_t * iptr)
{
    uint16_t index = 0;
    FILINFO f_info;
    //Always start at the beginning
    FRESULT res = f_findfirst(&_root_dir, &f_info, "", "*.mid*");

    if (res != FR_OK) return READ_ERROR;

    if (f_info.fname[0] == 0) return NO_MIDI;

    while(1)
    {
        res = f_findnext(&_root_dir, &f_info);

        if (res != FR_OK) return READ_ERROR;

        //We found the last file, so use the data from the previous
        if (f_info.fname[0] == 0)
        {
            *iptr = index;
            break;
        }

        index++;
    }

    return OK;
}

midi_result_t midi_next(midi_info * midi)
{
    
    _file_index++;
    midi_result_t res = get_file_at_index(_file_index, midi);
    if (_file_index > 0 && res == NO_MIDI)
    {
        //If we got here, we reached the end and need to wrap around
        _file_index = 0;
        res = get_file_at_index(_file_index, midi);
    }
    
    return res;
}

midi_result_t midi_previous(midi_info * midi)
{
    
    _file_index--;
    midi_result_t res = get_file_at_index(_file_index, midi);

    if (res == NO_MIDI)
    {
        res = get_last_file_index(&_file_index);

        if (res == OK)
        {
            res = get_file_at_index(_file_index, midi);
        }
    }

    return res;
}

midi_result_t midi_init(midi_info * midi)
{
    _is_initialized = false;
    midi->index = 0;
    midi->midi_data = 0;
    midi->time_div = 0;
    midi->track_cnt = 0;
    midi->tracks = 0;
    midi->type = 0;
    midi->us_per_tick = DEFAULT_US_PER_TICK;

    if (midi_open_sd() != OK) return READ_ERROR;
    if (midi_mount_root(&_root_dir) != OK) return READ_ERROR;
    
    //Set to max so when we go forward for the first time, it wraps to 0.
    _file_index = UINT16_MAX;
    // _message_cb = message_cb;
    // _msg_stream_cb = msg_stream_cb;

    _is_initialized = true;

    return OK;

}

bool is_initialized()
{
    return _is_initialized;
}