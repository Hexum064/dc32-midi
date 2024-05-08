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

// static uint32_t pitches[] = {524288,555464,588493,623487,660561,699840,741455,785544,832255,881744,934175,989724,1048576,1110928,1176987,1246974,1321123,1399681,1482910,1571089,1664511,1763487,1868350,1979448,2097152,2221855,2353973,2493948,2642246,2799362,2965821,3142177,3329021,3526975,3736700,3958896,4194304,4443710,4707947,4987896,5284492,5598724,5931641,6284355,6658042,7053950,7473400,7917791,8388608,8887420,9415894,9975792,10568983,11197448,11863282,12568710,13316084,14107900,14946799,15835582,16777215,17774840,18831787,19951583,21137966,22394895,23726565,25137420,26632169,28215800,29893599,31671165,33554430,35549680,37663574,39903167,42275933,44789790,47453130,50274840,53264337,56431600,59787197,63342329,67108860,71099360,75327148,79806334,84551865,89579581,94906260,100549680,106528675,112863200,119574395,126684658,134217720,142198721,150654297,159612668,169103731,179159162,189812520,201099360,213057350,225726399,239148789,253369316,268435440,284397442,301308594,319225335,338207461,358318324,379625040,402198719,426114700,451452798,478297579,506738633,536870880,568794884,602617188,638450670,676414923,716636647,759250080,804397439};
static float twelfths[] = {1, 
1.0594630943593,
1.1224620483094,
1.1892071150027,
1.2599210498949,
1.3348398541700,
1.4142135623731,
1.4983070768767,
1.5874010519682,
1.6817928305074,
1.7817974362807,
1.8877486253634
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
    float offset_step; //Calculated in 12ths
    float offset;
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