#ifndef midi_h
#define midi_h

#include "ff.h"
#include <stdbool.h>

// #define DEBUG

#define MAX_FILE_SIZE 128000LU
#define BUFF_ADD_LEVEL 200
#define DEFAULT_US_PER_TICK (500000/96) //default to 96 bpm 
#define VOICE_COUNT 24
#define KEY_COUNT 10
#define CHANNEL_COUNT 16

static uint8_t  sine_wave[256] = {
  0x80, 0x83, 0x86, 0x89, 0x8C, 0x90, 0x93, 0x96,
  0x99, 0x9C, 0x9F, 0xA2, 0xA5, 0xA8, 0xAB, 0xAE,
  0xB1, 0xB3, 0xB6, 0xB9, 0xBC, 0xBF, 0xC1, 0xC4,
  0xC7, 0xC9, 0xCC, 0xCE, 0xD1, 0xD3, 0xD5, 0xD8,
  0xDA, 0xDC, 0xDE, 0xE0, 0xE2, 0xE4, 0xE6, 0xE8,
  0xEA, 0xEB, 0xED, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4,
  0xF5, 0xF6, 0xF8, 0xF9, 0xFA, 0xFA, 0xFB, 0xFC,
  0xFD, 0xFD, 0xFE, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFE, 0xFE, 0xFD,
  0xFD, 0xFC, 0xFB, 0xFA, 0xFA, 0xF9, 0xF8, 0xF6,
  0xF5, 0xF4, 0xF3, 0xF1, 0xF0, 0xEF, 0xED, 0xEB,
  0xEA, 0xE8, 0xE6, 0xE4, 0xE2, 0xE0, 0xDE, 0xDC,
  0xDA, 0xD8, 0xD5, 0xD3, 0xD1, 0xCE, 0xCC, 0xC9,
  0xC7, 0xC4, 0xC1, 0xBF, 0xBC, 0xB9, 0xB6, 0xB3,
  0xB1, 0xAE, 0xAB, 0xA8, 0xA5, 0xA2, 0x9F, 0x9C,
  0x99, 0x96, 0x93, 0x90, 0x8C, 0x89, 0x86, 0x83,
  0x80, 0x7D, 0x7A, 0x77, 0x74, 0x70, 0x6D, 0x6A,
  0x67, 0x64, 0x61, 0x5E, 0x5B, 0x58, 0x55, 0x52,
  0x4F, 0x4D, 0x4A, 0x47, 0x44, 0x41, 0x3F, 0x3C,
  0x39, 0x37, 0x34, 0x32, 0x2F, 0x2D, 0x2B, 0x28,
  0x26, 0x24, 0x22, 0x20, 0x1E, 0x1C, 0x1A, 0x18,
  0x16, 0x15, 0x13, 0x11, 0x10, 0x0F, 0x0D, 0x0C,
  0x0B, 0x0A, 0x08, 0x07, 0x06, 0x06, 0x05, 0x04,
  0x03, 0x03, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03,
  0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x0A,
  0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x13, 0x15,
  0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22, 0x24,
  0x26, 0x28, 0x2B, 0x2D, 0x2F, 0x32, 0x34, 0x37,
  0x39, 0x3C, 0x3F, 0x41, 0x44, 0x47, 0x4A, 0x4D,
  0x4F, 0x52, 0x55, 0x58, 0x5B, 0x5E, 0x61, 0x64,
  0x67, 0x6A, 0x6D, 0x70, 0x74, 0x77, 0x7A, 0x7D
};

static uint8_t offset_steps[] = 
{
    0,128,86,64,52,43,130,115,85,55,39,22
};

static uint16_t twelfths[] = {
//     16384, 
// 17358,
// 18390,
// 19484,
// 20643,
// 21870,
// 23170,
// 24548,
// 26008,
// 27554,
// 29193,
// 30929
30929,
29193,
27554,
26008,
24548,
23170,
21870,
20643,
19484,
18390,
17358,
16384


};

typedef void (* _message_cb_t)(uint8_t * data, uint8_t length);

typedef struct key_s
{
    uint8_t low_key;
    uint8_t high_key;
    uint16_t key_offset;
} key_info;


typedef struct channel_s
{    
    uint8_t key_count;
    key_info keys[KEY_COUNT];
} channel_info;

typedef struct voice_s
{
    uint8_t sample_id;
    uint16_t sample_len;   
    uint16_t loop_start; 
    uint8_t root;
    uint8_t velocity;
    uint16_t sustain;
    uint16_t decay;
    uint16_t release;
    uint8_t * sample_data;
    //The inverse of the octave. Where the root number is no more than 5 octaves below the highest note
    //Since this is a divider. 0 = highest note
    uint8_t octave_div; 
    uint8_t octave_div_cnt; //A counter to track the divisions
    uint16_t offset_step; //Calculated in 12ths
    uint16_t offset_step_cnt;
    uint32_t offset;
    
} voice_info;

static channel_info channels[CHANNEL_COUNT];
static voice_info voices[VOICE_COUNT];


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