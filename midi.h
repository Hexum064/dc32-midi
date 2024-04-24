#ifndef midi_h
#define midi_h

#include "ff.h"
#include <stdbool.h>

// #define DEBUG

#define MAX_FILE_SIZE 128000LU
#define BUFF_ADD_LEVEL 200
#define DEFAULT_US_PER_TICK (500000/96) //default to 96 bpm 

typedef void (* _message_cb_t)(uint8_t * data, uint8_t length);



typedef struct track_s 
{
    uint32_t offset;    
    uint32_t size;
    uint32_t delta;
    uint32_t elapsed;
    uint8_t status;
    bool read_delta;
    bool eot_reached;
} track_info;

typedef struct midi_s
{
    uint8_t name[255];
    uint16_t index;
    uint16_t type;
    uint16_t track_cnt;
    uint16_t time_div;
    uint32_t us_per_tick;
    uint8_t * midi_data;
    track_info * tracks;
} midi_info;



typedef enum midi_result_enum
{
    OK,
    NO_MIDI,
    READ_ERROR,
    TOO_BIG,
    UNSUPPORTED
} midi_result_t;



typedef enum midi_msgs_enum
{
    NOTE_OFF = 0x80,
    NOTE_ON = 0x90,    
    POLY_KEP_PRESS = 0xA0,
    CTRL_CHANGE = 0xB0,
    PROG_CHANGE = 0xC0,
    CHAN_PRESS = 0xD0,
    PITCH_WHEEL = 0xE0,
    SYS_EX = 0xF0,
    SONG_POS_PTR = 0xF2,
    SONG_SEL = 0xF3,
    TUNE_REQ = 0xF6,
    END_SYS_EX = 0xF7,
    TIMING_CLK = 0xF8,
    MIDI_START = 0xFA,
    MIDI_CONT = 0xFB,
    MIDI_STOP = 0xFC,
    ACT_SENSE = 0xFE,
    META_EVENT = 0xFF

} midi_message_t;

typedef enum midi_meta_enum
{
    SEQ_NUM = 0x00,
    TEXT = 0x01,
    COPYRIGHT = 0x02,
    TRACK_NAME = 0x03,
    INSTR_NAME = 0x04,
    LYRIC = 0x05,
    MARKER = 0x06,
    CUE_POINT = 0x07,
    MIDI_CH_PRE = 0x20,
    EOT = 0x2F,
    SET_TEMPO = 0x51,
    SMPTE_OFFSET = 0x54,
    TIME_SIG = 0x58,
    KEY_SIG = 0x59,
    SEQ_SPECIFIC = 0x7F
} midi_meta_t;

uint32_t big_endian_to_int(uint8_t buff[4]);
uint16_t big_endian_to_word(uint8_t buff[2]);
bool end_of_midi(midi_info * midi);

/// @brief processes the tracks to get the events at the next ticks
/// @param midi the midi data
/// @param message_data the message data to transmit
/// @param message_length the length of the message to transmit
/// @param ticks the tick the message should be transmitted on
/// @return true if there are more tracks to proccess or false if all are at EoT
bool process_all_tracks(midi_info * midi, uint8_t * message_data, uint8_t * message_length, uint32_t * ticks);


/// @brief processes the tracks in order of ticks
/// @param midi pointer to the midi info struct
/// @param message_data returned message data to transmit
/// @param message_length length of the message to transmit
/// @param ticks the tick to transmit on
/// @return true if ok or false if at end of all tracks
uint8_t get_VLQ(uint8_t * data, uint32_t offset, uint32_t * iptr);
//Always reads 2 bytes
void load_word(uint8_t * data, uint32_t offset, uint16_t * value);
midi_result_t midi_get_file_info(FIL * fptr, midi_info * midi);

midi_result_t midi_init(midi_info * midi);
midi_result_t midi_next(midi_info * midi);
midi_result_t midi_previous(midi_info * midi);
midi_result_t midi_mount_root(DIR * root);
midi_result_t midi_open_sd();
bool is_initialized();

#endif