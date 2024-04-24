
#include "midi.h"
#include "sd_card.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "hw_config.h"

sd_card_t *sd_ptr;
FIL _current_file;
DIR _root_dir;
uint16_t _file_index;

midi_result_t midi_move_file_pointer(FIL * fptr, uint32_t length, uint32_t * address)
{

    *address = *address + length;
    return f_lseek(fptr, *address);
    
}

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
        track->address++;
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

//Set up to read a variable length quantity, up to 32 bits
midi_result_t get_VLQ(FIL * fptr, uint32_t * address, uint32_t * iptr)
{
    uint8_t data;
    uint32_t value = 0;  

    do
    {
        value <<= 7;
        // printf("\t\tVLQ: reading next byte\n");
        if (read_current_data(fptr, address, &data, 1) != OK) return READ_ERROR;  
        // printf("\t\tVLQ: data: %d, val: %d\n", data, value);
        value += data & 0x7F;
    //If the MSB is set then we are still processing a VLQ    
    } while ((data & 0x80));
    
    *iptr = value;
    return OK;
}


midi_result_t load_word(FIL * fptr, uint32_t offset, uint16_t * value)
{
    uint8_t buff[2];
    if (f_lseek(fptr, offset) != OK) return READ_ERROR;
    if (f_read(fptr, buff, 2, 0) != OK) return READ_ERROR;
    *value = big_endian_to_word(buff);
    return OK;
}

midi_result_t load_midi_type(FIL * fptr, midi_info * midi)
{
    uint16_t val;
    if (load_word(fptr, 8, &val) != OK) return READ_ERROR;
    
    midi->type = val;

    return OK;
}

midi_result_t load_track_count(FIL * fptr, midi_info * midi)
{
  
    uint16_t val;
    if (load_word(fptr, 10, &val) != OK) return READ_ERROR;
    
    midi->track_cnt = val;


    return OK;    

}

midi_result_t load_time_div(FIL * fptr, midi_info * midi)
{
    uint16_t val;
    if (load_word(fptr, 12, &val) != OK) return READ_ERROR;
    
    midi->time_div = val;

    return OK;
}

midi_result_t load_track_chunk_info(FIL * fptr, midi_info * midi) 
{   
    uint8_t buff[4];
    uint32_t val;
    
    UINT bytes_read = 0;
    uint32_t offset = 14 + 4; //Start of first chunk + sig

    if (midi->tracks)
    {
        free(midi->tracks);
    }



    printf("Track count: %d\n", midi->track_cnt);

    midi->tracks = (track_info *)malloc(sizeof(track_info) * midi->track_cnt);


    for(uint8_t i = 0; i < midi->track_cnt; i++)
    {
        if (f_lseek(fptr, offset) != OK) return READ_ERROR;
        if (f_read(fptr, buff, 4, 0) != OK) return READ_ERROR;
        val = big_endian_to_int(buff); //Chunk size
        offset += 4; //move to start of data
        midi->tracks[i].address = offset; //address should be first actual data byte after size
        midi->tracks[i].size = val;
        midi->tracks[i].read_delta = true;
        midi->tracks[i].delta = 0;
        midi->tracks[i].status = 0;
        midi->tracks[i].eot_reached = false;
        midi->tracks[i].elapsed = 0;

        // printf("Track: %d. Start: 0x%02x. Size: %u\n", i, offset, val);

        offset += val + 4; //skip next sig
        //TODO: make sure this accounts for type 0 correctly.
        if (midi->type == 0)
        {
            // printf("\tType 0\n");
            return OK;
        }
    }

    return OK;
}


midi_result_t midi_get_file_info(FIL * fptr, const TCHAR *path, midi_info * midi)
{    
    if (f_open(fptr, path, FA_READ) != OK) return READ_ERROR;

    FSIZE_t fSize = f_size(fptr);

    printf("File size: %llu\n", fSize);

    uint8_t * fBytes = (uint8_t *)malloc(fSize);

    printf("Mem Alloc'd. Reading file...\n");

    if (f_read(fptr, fBytes, fSize, 0) == FR_OK)
    {
        printf("File read. Freeing mem\n");
    }
    else
    {
        printf("File read failed. Freeing mem\n");
    }

    free(fBytes);




    if (load_midi_type(fptr, midi) != OK) return READ_ERROR;
    if (load_time_div(fptr, midi) != OK) return READ_ERROR;
    if (load_track_count(fptr, midi) != OK) return READ_ERROR;
    if (load_track_chunk_info(fptr, midi) != OK) return READ_ERROR;

    strcpy(midi->name, path);
    midi->file = fptr;

    return OK;
}

midi_result_t midi_open_sd()
{
    if (sd_ptr)
    {
        f_unmount(sd_ptr->pcName);
    }

    sd_ptr = sd_get_by_num(0);
    FRESULT fr = f_mount(&sd_ptr->fatfs, sd_ptr->pcName, 1);

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

    //If we reach here, we found the midi file at the index requested;
    if (midi_get_file_info(&_current_file, f_info.fname, midi) != FR_OK) return READ_ERROR;

    return OK;
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

midi_result_t midi_init()
{

    if (midi_open_sd() != OK) return READ_ERROR;
    if (midi_mount_root(&_root_dir) != OK) return READ_ERROR;
    
    //Set to max so when we go forward for the first time, it wraps to 0.
    _file_index = UINT16_MAX;
    // _message_cb = message_cb;
    // _msg_stream_cb = msg_stream_cb;

            
    return OK;

}