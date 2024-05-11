
#include <stdio.h>

#include "f_util.h"
#include "ff.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "rtc.h"

#include "string.h"
#include <stdlib.h>


#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "midi.h"
#include "midi_uart.h"

// #include "../../../src/rp2_common/hardware_i2c/include/hardware/i2c.h"


#define ALARM_NUM 0
#define ALARM_IRQ TIMER_IRQ_0

#define ALARM_2_NUM 1
#define ALARM_2_IRQ TIMER_IRQ_1

// #define I2C_BAUD 400000
// #define AUDIO_I2C_ADDR 0x60
// #define AUDIO_I2C i2c1
// #define GPIO_SDA1 2
// #define GPIO_SCL1 3

midi_info midi;
bool process_midi = false;
uint32_t _ticks = 0;
uint32_t _next_tick = 0;

uint16_t sample_len = 0;
uint16_t orig_len = 0;
uint32_t sample_start = 0;
float sample_offset = 0;

extern uint8_t samples_start[];
extern uint8_t map_start[];
extern uint8_t map_end;
uint32_t offset = 1;
uint8_t id;
uint8_t samples;
uint8_t * orig_sample;
uint8_t * pitched_sample;
bool playing_sample = false;

float step = 0;

#define US_FOR_11K 2 //91


// Alarm interrupt handler
static volatile bool alarm_fired;
static void alarm_in_us(uint32_t delay_us) ;
static void alarm_2_in_us(uint32_t delay_us);

static void alarm_irq(void) {
    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    
    _ticks++;
    alarm_in_us(midi.us_per_tick);
}

static void alarm_in_us(uint32_t delay_us) {
    // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
    // Set irq handler for alarm irq
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
    // Enable the alarm irq
    irq_set_enabled(ALARM_IRQ, true);
    // Enable interrupt in block and at processor

    // Alarm is only 32 bits so if trying to delay more
    // than that need to be careful and keep track of the upper
    // bits
    //uint64_t target = timer_hw->timerawl + delay_us;

    // Write the lower 32 bits of the target time to the alarm which
    // will arm it

    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + delay_us;
}

// uint8_t i2c_data[] = {0x40, 0x00, 0x00};
static void alarm_2_irq(void) {
    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_2_NUM);
    alarm_2_in_us(US_FOR_11K);

    if (!voices[0].active)
    {
        return;
    }

    voices[0].octave_div_cnt--;
    if (voices[0].octave_div_cnt == 0)
    {
        uint64_t start_time;
        uint64_t end_time;

        
        // start_time = time_us_64();
        uint16_t d = *(voices[0].sample_data + (uint16_t)(voices[0].offset>>14)) << 8;       

        gpio_put_masked(0xFF00, d);

         voices[0].offset += voices[0].offset_step;

        //Reached the end of the sample so reset offset to loop start
        if (voices[0].offset >= (uint32_t)((voices[0].sample_len - 1) << 14))
        {
            //printf("reset offset to loop start: 0x%08x\n", voices[0].loop_start);
            voices[0].offset = voices[0].loop_start << 14;
        }
        voices[0].octave_div_cnt = voices[0].octave_div;
        // end_time = time_us_64();
        // printf("%llu\n", end_time-start_time);
    }


    // if (playing_sample)
    // {
        
    // }
    // else 
    // {
    //     printf("Finished with sample in core (%d)\n", get_core_num());
    // }
}

static void alarm_2_in_us(uint32_t delay_us) {
    // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_2_NUM);
    // Set irq handler for alarm irq
    irq_set_exclusive_handler(ALARM_2_IRQ, alarm_2_irq);
    // Enable the alarm irq
    irq_set_enabled(ALARM_2_IRQ, true);
    // Enable interrupt in block and at processor

    // Alarm is only 32 bits so if trying to delay more
    // than that need to be careful and keep track of the upper
    // bits
    //uint64_t target = timer_hw->timerawl + delay_us;

    // Write the lower 32 bits of the target time to the alarm which
    // will arm it

    timer_hw->alarm[ALARM_2_NUM] = timer_hw->timerawl + delay_us;
}

void dac_io_init()
{
    gpio_init_mask(0xFF00);
    gpio_set_dir_masked(0xFF00, 0xFF00);
    gpio_set_drive_strength(8, GPIO_DRIVE_STRENGTH_8MA );
    gpio_set_drive_strength(9, GPIO_DRIVE_STRENGTH_8MA );
    gpio_set_drive_strength(10, GPIO_DRIVE_STRENGTH_8MA );
    gpio_set_drive_strength(11, GPIO_DRIVE_STRENGTH_8MA );
    gpio_set_drive_strength(12, GPIO_DRIVE_STRENGTH_8MA );
    gpio_set_drive_strength(13, GPIO_DRIVE_STRENGTH_8MA );
    gpio_set_drive_strength(14, GPIO_DRIVE_STRENGTH_8MA );
    gpio_set_drive_strength(15, GPIO_DRIVE_STRENGTH_8MA );
    
}

void copy_from_flash(uint8_t * dest, uint32_t offset, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        *(dest++) = samples_start[offset++];
    }
}

void load_channel(uint8_t channel, uint8_t patch)
{
    //Offset is the start of the key data. Key data starts after the patch count and offsets for each patch
    uint16_t index = (patch * 2) + 1;
    uint16_t offset = (uint16_t)map_start[index] + (uint16_t)(map_start[index + 1] << 8);
    //printf("Index 0x%04x, Data: 0x%04x\n", index, offset);
    offset += (map_start[0] * 2) + 2; //2 instead of one to skip the patch id too
    uint8_t cnt = map_start[offset++];
    // printf("count: %d, offset: 0x%04x\n", cnt, offset);
    for (uint8_t i = 0; i < cnt; i++)
    {
        channels[channel].key_count = cnt;
        channels[channel].keys[i].key_offset = offset;
        channels[channel].keys[i].low_key = map_start[offset++];
        channels[channel].keys[i].high_key = map_start[offset];
        offset += 18;
    }
}

uint8_t find_key(uint8_t channel, uint8_t key)
{
    
    for (uint8_t i = 0; i < channels[channel].key_count; i++)
    {
        if (key >= channels[channel].keys[i].low_key && key <= channels[channel].keys[i].high_key)
        {
            return i;
        }
    };
    return 0;
}



//TODO: How do I index the voices
uint8_t some_index = 0;
//TODO: How do I optimize this?
void load_sample(uint8_t channel, uint8_t key, uint8_t velocity)
{
            //0: loKey: 1
            //1: hikey: 1
            //2: root: 1
            //3: offset: 4
            //7: sampleLen: 2
            //9: loopStart: 2
            //11: loopEnd: 2 
            //13: decay: 2
            //15: sustain: 2
            //17: release: 2
    uint8_t key_index = find_key(channel, key);
    key_info keyInfo = channels[channel].keys[key_index];    
    voices[some_index].root = map_start[keyInfo.key_offset+2];

    voices[some_index].sample_len = (uint16_t)map_start[keyInfo.key_offset+7] + (uint16_t)(map_start[keyInfo.key_offset+8] << 8);
    voices[some_index].loop_start = ((uint16_t)map_start[keyInfo.key_offset+9] + (uint16_t)(map_start[keyInfo.key_offset+10] << 8)) - 0;

    uint32_t sample_data_offset = 0;
    uint8_t * offset_ptr = &sample_data_offset;
    *(offset_ptr++) = map_start[keyInfo.key_offset+3];
    *(offset_ptr++) = map_start[keyInfo.key_offset+4];
    *(offset_ptr++) = map_start[keyInfo.key_offset+5];
    *(offset_ptr++) = map_start[keyInfo.key_offset+6];
    voices[some_index].sample_data = samples_start + sample_data_offset;

    voices[some_index].decay = (uint16_t)map_start[keyInfo.key_offset+13] + (uint16_t)(map_start[keyInfo.key_offset+14] << 8);
    voices[some_index].sustain = (uint16_t)map_start[keyInfo.key_offset+15] + (uint16_t)(map_start[keyInfo.key_offset+16] << 8);
    voices[some_index].release = (uint16_t)map_start[keyInfo.key_offset+17] + (uint16_t)(map_start[keyInfo.key_offset+18] << 8);

    voices[some_index].velocity = velocity;
    
    uint8_t root = voices[some_index].root;
    uint8_t diff = 71 + (root-key);
    uint8_t step_mod = (diff-12) % 12;
    
    uint8_t exponent = (int8_t)(diff / 12);  
    uint8_t oct_div = 1 << exponent;

    
    voices[some_index].octave_div = oct_div;
    voices[some_index].octave_div_cnt = voices[some_index].octave_div;
    voices[some_index].offset_step = twelfths[step_mod];
    voices[some_index].offset_step_cnt = 0;
    voices[some_index].offset = 0;

    //printf("Key %d. offset: 0x%08x loop start: 0x%08x, len: %lu\n", key, voices[some_index].sample_data, voices[some_index].loop_start, voices[some_index].sample_len );
}

void init_voices()
{
    for (uint8_t i = 0; i < VOICE_COUNT; i++)
    {
        voices[i].sample_data = (uint8_t *)malloc(1);
    }
}

void main2()
{
    printf("Entering core 1 (%d)\n", get_core_num());
    alarm_2_in_us(US_FOR_11K); //pretend the alarm is always active
    printf("playing samples\n");
    uint8_t patch = 102; //114; //90

    uint64_t start_time;
    uint64_t end_time;
    uint64_t delta_time;

    start_time = time_us_64();
    load_channel(0, patch);
    end_time = time_us_64();

    delta_time = end_time - start_time;


    printf("---Channel %d -> patch %d---\n", 0, patch);
    printf("\tkey count: %d\n", channels[0].key_count);
    printf("\tLoad time: %llu\n", delta_time);

    uint8_t key = 64;
    uint32_t us = 3000000;
    uint32_t us_start = time_us_32();
    load_sample(0, key, 128);
    voices[0].active = true;

    while(time_us_32() < us_start + us)
    {}

    voices[0].active = false;


    key = 60;
    us_start = time_us_32();
    load_sample(0, key, 128);
    voices[0].active = true;

    while(time_us_32() < us_start + us)
    {}

    voices[0].active = false;       


    key = 56;
    us_start = time_us_32();
    load_sample(0, key, 128);
    voices[0].active = true;

    while(time_us_32() < us_start + us)
    {}

    voices[0].active = false;       

    // for (int8_t i = 56 ; i < 84; i = i + 1 )
    // {

    //     start_time = time_us_64();
    //     load_sample(0, i, 128);
    //     end_time = time_us_64();

    //     delta_time = end_time - start_time;        
    //     printf("Key: %d load time: %llu. Offset: 0x%08x, oct div: %d, step: %lu.\n", i, delta_time, voices[0].sample_data, voices[0].octave_div, voices[0].offset_step);
    //     playing_sample = true;
    //     alarm_2_in_us(US_FOR_11K);
        
    //     while(playing_sample){}
    // }


}


int main() {
    int i = 0;
    
    set_sys_clock_khz(133000, true);

    dac_io_init();

    stdio_init_all();

    serial_init();


sleep_ms(2000);

    printf("Starting...\n");

    puts("Hello, world!");
    printf("---Patches: %d---\n", map_start[0]);
    offset = 2 * map_start[0] + 1;

            //loKey: 1
            //hikey: 1
            //root: 1
            //offset: 4
            //sampleLen: 2
            //loopStart: 2
            //loopEnd: 2 
            //decay: 2
            //sustain: 2
            //release: 2

    // for (uint8_t i = 0; i < map_start[0]; i++)
    // {
    //     id = map_start[offset++];
    //     samples = map_start[offset++];
    //     printf("Patch %d, samples: %d.\n", id, samples);      
            
    //         for (uint8_t j = 0; j < samples; j++)
    //         {
    //             printf("\tkeys: %d - %d. Root: %d. offset: 0x%08x. length: %u\n", map_start[offset+0], map_start[offset+1], map_start[offset+2], 
    //             (map_start[offset+3] + (map_start[offset+4] << 8) + (map_start[offset+5] << 16) + (map_start[offset+6] << 24)),
    //             (map_start[offset+7] + (map_start[offset+8] << 8)));
    //             offset += 19;
    //             printf("\t\tnext offset: 0x%08x\n", offset);
    //         }
    //     offset+=4;
    // }



    multicore_launch_core1(main2);


uint64_t start_time;
uint64_t end_time;
uint64_t delta_time;


    printf("done\n");
     sleep_ms(2000);
    while(1)
    {
        sleep_ms(500);
        printf("sleeping in core %d\n", get_core_num());
    }
     

    midi_init(&midi);
    

    midi_result_t res = midi_next(&midi);

    printf("Res: %d, #%d: %s, type: %d, tracks: %d, time: %d\n", res, midi.index, midi.name, midi.type, midi.track_cnt, midi.time_div);

    for (uint8_t i = 0; i < midi.track_cnt; i++)
    {
        printf("\tTrack %d, size: %d, addr: 0x%08x\n",i, midi.tracks[i].size, midi.tracks[i].offset);
    }



    uint8_t msg[128];
    uint8_t len;
   

   alarm_in_us(midi.us_per_tick);

    while(1)
    {
        if (end_of_midi(&midi))
        {
            break;
        }

        while(_ticks < _next_tick || is_transmitting())
        {            
            
        }

        //make sure _next_tick starts at 0
        if (!process_all_tracks(&midi, msg, &len, &_next_tick))
        {
            break;
        }



        printf("Sending %d bytes at tick %u\n\tNext: %u\n", len, _ticks, _next_tick);
   
        start_transmit(msg, len);


       
    }



    puts("Goodbye, world!");
 
    while(1){

    }
}
