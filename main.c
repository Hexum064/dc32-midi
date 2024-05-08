
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

#define US_FOR_11K 3 //91

uint8_t InterpolateHermite4pt3oX(uint8_t* x, uint32_t t)
{
    // float c0 = x[1];
    uint64_t c0 = x[1];
    // float c1 = .5F * (x[2] - x[0]);
    uint64_t c1 = (x[2] - x[0]) >> 1;
    // float c2 = x[0] - (2.5F * x[1]) + (2 * x[2]) - (.5F * x[3]);
    uint64_t c2 = x[0] - ((x[1] << 1) + (x[1] >> 1)) + (x[2] << 1) - (x[3] >> 1);
    // float c3 = (.5F * (x[3] - x[0])) + (1.5F * (x[1] - x[2]));
    uint64_t c3 = ((x[3] - x[0]) >> 1) + ((x[1] - x[2]) + ((x[1] - x[2]) >> 1));
    c0 <<= 24;
    c1 <<= 24;
    c2 <<= 24;
    c3 <<= 24;
    return ((((((c3 * t) + c2) * t) + c1) * t) + c0) >> 24; //TODO: This still needs to be calculated
    //return c3;
}


//Simulate scratching of `inwave`: 
// `rate` is the speedup/slowdown factor. 
// result mixed into `outwave`
// "Sample" is a typedef for the raw audio type.
void pitch_sample(uint8_t * outwave, uint8_t * inwave, uint32_t rate, uint32_t offset, uint16_t inputLen)
{
   uint64_t index = 0;
   while (index < inputLen)
   {
      uint16_t i = index >> 24;      //get the integer value of (what was a float) index
      uint64_t frac = index-i;      //will be between 0 and 1
    //   uint8_t s1 = inwave[i + offset];
    //   uint8_t s2 = inwave[i+1 + offset];
      *outwave++ = InterpolateHermite4pt3oX((inwave+(1) + offset),rate);
      index++;
   }
}

/*
void ScratchMix(uint8_t * outwave, uint8_t * inwave, float rate, uint32_t offset, uint16_t inputLen)
{
   float index = 0;
   while (index < inputLen)
   {
      int i = (int)index;          
      float frac = index-i;      //will be between 0 and 1
    //   uint8_t s1 = inwave[i + offset];
    //   uint8_t s2 = inwave[i+1 + offset];
      *outwave++ = InterpolateHermite4pt3oX(inwave+((i-1) + offset),frac);
      index+=rate;
   }
}
rate = .3

1
index = 0
i = 0
frac = 0;

2
index = .3
i = 0;
frac = .3

3
index = .6
i = 0
frac = .6

4
index = .9
i = 0
frac = .9

5
index = 1.2
i = 1
frac = .2

*/

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
    voices[0].octave_div_cnt--;
    if (voices[0].octave_div_cnt == 0)
    {
        uint64_t start_time;
        uint64_t end_time;
        // start_time = time_us_64();
        //TODO: Might need to load the sample in local mem
        uint16_t d = *(voices[0].sample_data + (uint16_t)voices[0].offset) << 8; // [(uint16_t)sample_offset] << 8;//  samples_start[sample_offset] << 8;        
        // i2c_data[1] = d;
        // i2c_write_blocking(AUDIO_I2C, AUDIO_I2C_ADDR, i2c_data, 3, true );
        gpio_put_masked(0xFF00, d);
        // printf("**%u: 0x%04x.\n",sample_offset, d);
        //Send I2C: try i2c_write_byte_raw next
        
        // voices[0].offset++;
        voices[0].offset += voices[0].offset_step;
        playing_sample = voices[0].offset < voices[0].sample_len;
        voices[0].octave_div_cnt = voices[0].octave_div;
        // end_time = time_us_64();
        // printf("%llu\n", end_time-start_time);
    }


    if (playing_sample)
    {
        alarm_2_in_us(US_FOR_11K);
    }
    else 
    {
        printf("Finished with sample in core (%d)\n", get_core_num());
    }
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
    // printf("Index 0x%04x, Data: 0x%04x\n", index, offset);
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
    uint8_t key_index = find_key(channel, key);
    key_info keyInfo = channels[channel].keys[key_index];    
    voices[some_index].root = map_start[keyInfo.key_offset+2];
    uint8_t rate_index = 60 + (int8_t)(key - voices[some_index].root);
    voices[some_index].sample_len = (uint16_t)map_start[keyInfo.key_offset+7] + (uint16_t)(map_start[keyInfo.key_offset+8] << 8);
    voices[some_index].loop_start = (uint16_t)map_start[keyInfo.key_offset+9] + (uint16_t)(map_start[keyInfo.key_offset+10] << 8);
    // uint8_t * base_sample_data = (uint8_t *)malloc(base_len);
    uint32_t sample_data_offset = 0;
    uint8_t * offset_ptr = &sample_data_offset;
    *(offset_ptr++) = map_start[keyInfo.key_offset+3];
    *(offset_ptr++) = map_start[keyInfo.key_offset+4];
    *(offset_ptr++) = map_start[keyInfo.key_offset+5];
    *(offset_ptr++) = map_start[keyInfo.key_offset+6];
    voices[some_index].sample_data = samples_start + sample_data_offset;
    // printf("Offset: 0x%08x, base len: %llu\n", sample_data_offset, base_len);

    // uint32_t rate = pitches[rate_index];
    
  
    voices[some_index].decay = (uint16_t)map_start[keyInfo.key_offset+13] + (uint16_t)(map_start[keyInfo.key_offset+14] << 8);
    voices[some_index].sustain = (uint16_t)map_start[keyInfo.key_offset+15] + (uint16_t)(map_start[keyInfo.key_offset+16] << 8);
    voices[some_index].release = (uint16_t)map_start[keyInfo.key_offset+17] + (uint16_t)(map_start[keyInfo.key_offset+18] << 8);
    // voices[some_index].sample_data = (uint8_t *)realloc(voices[some_index].sample_data, voices[some_index].sample_len);
    voices[some_index].velocity = velocity;
    //copy_from_flash(voices[some_index].sample_data, sample_data_offset, base_len);
        // uint64_t start_time;
        // uint64_t end_time;
        // start_time = time_us_64();
        // pitch_sample(voices[some_index].sample_data, base_sample_data, rate, 0, base_len);
        // end_time = time_us_64();
        // printf("Pitch change time: %llu\n", end_time - start_time);
    // pitch_sample(voices[some_index].sample_data, samples_start, rate, sample_data_offset, base_len);
    // printf("New Len: %d, key: %d, root: %d, loop start: %d, rate: %lu, orig len: %llu, orig loop start: %llu\n", 
    // voices[some_index].sample_len, key, voices[some_index].root, voices[some_index].loop_start, rate, base_len, base_loop_start);
    

    
    int8_t diff = key-voices[some_index].root;
    int8_t step_index = diff % 12;
    step_index = step_index < 0 ? step_index + 12 : step_index;
    uint8_t div = 5 - ((diff - step_index) / 12);
    voices[some_index].octave_div = 1 << div;
    voices[some_index].octave_div_cnt = voices[some_index].octave_div;
    voices[some_index].offset_step = twelfths[step_index];
    voices[some_index].offset = 0;
    // printf("Key: %d, lo: %d, hi: %d, root %d\n", key, channels[channel].keys[key_index].low_key, channels[channel].keys[key_index].high_key, voices[some_index].root);
    /*

    if (key > root)
    root = 68
    key = 80
    diff = key-root = 12
 !   step_index = diff % 12 = 0
    oct_div = 5 - ((diff - step_index) / 12) = 4

    root = 68
    key = 97
    diff = 29
 !   step_index = 5
    oct_div = 5 - (24 / 12) = 3

    if (key == root)
    diff = 0
    step_index = 0
    oct_div = 5

    if (root > key)
    root = 68
    key = 56
    diff = root-key = 12
 !   step_index = diff % 12 = 0
    oct_div = 5 + ((diff-step_index) / 12) = 6

    root = 68 
    key = 34
    diff = 34
 !   step_index = 10
    oct_div = 5 + (24/12) = 7

    */

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
    printf("playing samples\n");
    uint8_t patch = 0;

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

    // uint16_t map_offset_index = patch * 2 + 1;
    // uint16_t map_offset = (uint16_t)map_start[map_offset_index++] + (uint16_t)(map_start[map_offset_index] << 8);
    // map_offset += 5 + 257; //sample offset start + map offset
    // printf("Map offset: 0x%04x\n", map_offset);
    // sample_start = (uint32_t)map_start[map_offset++] + (uint32_t)(map_start[map_offset++] << 8) + (uint32_t)(map_start[map_offset++] << 16) + (uint32_t)(map_start[map_offset++] << 24);
    // // sample_offset = sample_start;
    
    // printf("Sample start: 0x%08x\n", sample_start);
    // orig_len = (map_start[map_offset++] + (map_start[map_offset] << 8));
    // pitched_sample = (uint8_t *)malloc(orig_len);
    // copy_from_flash(pitched_sample, sample_start, orig_len);
    // pitched_sample = (uint8_t *)malloc(1);
    // float rate;
    for (int8_t i = 37; i < 87; i++)
    {
        // playing_sample = true;
        // step = (float)i/12;
        // sample_offset = 0;
        // sample_len = orig_len;
        // printf("Playing with step %f\n", step);
        // rate = pitches[i];
        // sample_len = orig_len / rate;
        // pitched_sample = (uint8_t *)realloc(pitched_sample, sample_len);
        // pitch_sample(pitched_sample, samples_start, rate, sample_start, orig_len );

        // printf("playing at rate %f (note %d)\n", rate, i);
        start_time = time_us_64();
        load_sample(0, i, 128);
        end_time = time_us_64();

        delta_time = end_time - start_time;        
        printf("Key: %d load time: %llu. Offset: 0x%08x, oct div: %d, step: %f\n", i, delta_time, voices[0].sample_data, voices[0].octave_div, voices[0].offset_step);
        playing_sample = true;
        alarm_2_in_us(US_FOR_11K);
        
        while(playing_sample){}
    }


}


int main() {
    int i = 0;
    
    dac_io_init();

    stdio_init_all();

    serial_init();

    // i2c_init(i2c1, I2C_BAUD);
    // // Make the I2C pins available to picotool
    // gpio_set_function( GPIO_SDA1, GPIO_FUNC_I2C  );
    // gpio_set_function( GPIO_SCL1, GPIO_FUNC_I2C ); 
    // gpio_pull_up(GPIO_SDA1);
    // gpio_pull_up(GPIO_SCL1);

sleep_ms(2000);

    printf("Starting...\n");

    puts("Hello, world!");

bool count_down = false;
uint8_t val = 0;
uint8_t  sine_wave[256] = {
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

    // while(1)
    // {
    //     gpio_put_masked(0xFF00, (sine_wave[val])<<8);
    //     // printf("Output: 0x%04x\n", (sine_wave[val])<<8);
    //     sleep_us(8);
    //     val++;
    // }

    // while(1)
    // {
    //     if (val == 0xff)
    //     {
    //         count_down = true;
    //     }

    //     if (val == 0)
    //     {
    //         count_down = false;
    //     }
    //     gpio_put_masked(0xFF00, val<<8);
    //     printf("Output: 0x%04x\n", val<<8);
    //     sleep_ms(200);

    //     if (count_down)
    //     {
    //         val--;
    //     }
    //     else
    //     {
    //         val++;
    //     }

    // }

    multicore_launch_core1(main2);

    // printf("---Patches: %d---\n", map_start[0]);
    // offset = 2 * map_start[0] + 1;
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

    // }

uint64_t start_time;
uint64_t end_time;
uint64_t delta_time;

    // start_time = time_us_64();
    // load_channel(0, 55);
    // end_time = time_us_64();

    // delta_time = end_time - start_time;


    // printf("---Channel %d -> patch %d---\n", 0, 55);
    // printf("\tkey count: %d\n", channels[0].key_count);
    // printf("\tLoad time: %llu\n", delta_time);

    // for (uint8_t i = 0; i < channels[0].key_count; i++)
    // {
    //     printf("\t\tkey %d: high key: %d, offset: 0x%04x\n", i, channels[0].keys[i].high_key, channels[0].keys[i].key_offset);
    // }

    // printf("--Testing key load time: Lower than root--\n");
    // start_time = time_us_64();
    // load_sample(0, 68, 127);
    // end_time = time_us_64();
    // delta_time = end_time - start_time;
    // printf("\tdelta: %llu\n", delta_time);



    // printf("--Testing key load time: Higher than root--\n");
    // start_time = time_us_64();
    // load_sample(0, 68, 127);
    // end_time = time_us_64();
    // delta_time = end_time - start_time;
    // printf("\tdelta: %llu\n", delta_time);




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
