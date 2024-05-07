#ifndef midi_h
#define midi_h

#include "ff.h"
#include <stdbool.h>

// #define DEBUG

#define MAX_FILE_SIZE 128000LU
#define BUFF_ADD_LEVEL 200
#define DEFAULT_US_PER_TICK (500000/96) //default to 96 bpm 

static float pitches[] = {0.03125,0.033108222,0.035076939,0.037162722,0.039372533,0.041713745,0.044194174,0.046822096,0.049606283,0.052556026,0.05568117,0.058992145,0.0625,0.066216443,0.070153878,0.074325445,0.078745066,0.083427491,0.088388348,0.093644192,0.099212566,0.105112052,0.11136234,0.117984289,0.125,0.132432887,0.140307756,0.148650889,0.157490131,0.166854982,0.176776695,0.187288385,0.198425131,0.210224104,0.22272468,0.235968578,0.25,0.264865774,0.280615512,0.297301779,0.314980262,0.333709964,0.353553391,0.374576769,0.396850263,0.420448208,0.445449359,0.471937156,0.5,0.529731547,0.561231024,0.594603558,0.629960525,0.667419927,0.707106781,0.749153538,0.793700526,0.840896415,0.890898718,0.943874313,1,1.059463094,1.122462048,1.189207115,1.25992105,1.334839854,1.414213562,1.498307077,1.587401052,1.681792831,1.781797436,1.887748625,2,2.118926189,2.244924097,2.37841423,2.5198421,2.669679708,2.828427125,2.996614154,3.174802104,3.363585661,3.563594873,3.775497251,4,4.237852377,4.489848193,4.75682846,5.0396842,5.339359417,5.656854249,5.993228308,6.349604208,6.727171322,7.127189745,7.550994501,8,8.475704755,8.979696386,9.51365692,10.0793684,10.67871883,11.3137085,11.98645662,12.69920842,13.45434264,14.25437949,15.101989,16,16.95140951,17.95939277,19.02731384,20.1587368,21.35743767,22.627417,23.97291323,25.39841683,26.90868529,28.50875898,30.20397801,32,33.90281902,35.91878555,38.05462768,40.3174736,42.71487533,45.254834,47.94582646};


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
    key_info keys[10];
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
} voice_info;

static channel_info channels[16];
static voice_info voices[24];


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