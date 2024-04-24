//TODO: count ticks, process buff based on ticks and send uart
//TODO: also filter out repeat status vals

#include <stdio.h>
#include "f_util.h"
#include "ff.h"
#include "pico/stdlib.h"

#include "rtc.h"

#include "string.h"
#include <stdlib.h>


#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "midi.h"
#include "midi_uart.h"


#define ALARM_NUM 0
#define ALARM_IRQ TIMER_IRQ_0

midi_info midi;
// bool process_midi = false;
uint32_t _ticks = 0;
uint32_t _tick_index = 0;
// uint8_t _buff[255];
 
// Alarm interrupt handler
static volatile bool alarm_fired;
static void alarm_in_us(uint32_t delay_us) ;

static void alarm_irq(void) {
    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // process_midi = true;
    

    
    if (!is_transmitting())
    {
        for (uint32_t i = 0; i < midi.tick_count; i++)
        {
            if (midi.tick_data[i].ticks == _ticks)
            {
                _tick_index = i;
                alarm_fired = true;
                
                break;
            }
        }        
        _ticks++;

    }
    else
    {
        alarm_in_us(500);
        return;        
    }

    if (_tick_index < midi.tick_count)
    {
        alarm_in_us(midi.us_per_tick);
        
    }  
    


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




// void msg_stream_cb(uint8_t * stream, uint8_t length)
// {
//     printf("Msg stream");
//     for (uint8_t i = 0; i < length; i++)
//     {
//         printf (", 0x%02x", stream[i]);
//     }
//     printf("\n");

//     start_transmit(stream, length);
  

// }

// void stage_data_transmit()
// {
//     uint8_t idx = 0;
//     midi_message msg;
//     while(cb_count())
//     {
//         if (cb_deque(&msg, _ticks))
//         {
//             for (uint8_t i = 0; i < msg.len; i++)
//             {
//                 _buff[idx++] = msg.data[i];
//             }
//         }
//         else
//         {
//             break;
//         }
        
//     }

//     if (idx > 0)
//     {
//         start_transmit(_buff, idx);
//     }
// }


int main() {
    int i = 0;
    
    stdio_init_all();
    time_init();
    serial_init();

sleep_ms(2000);

    printf("Starting...\n");

    puts("Hello, world!");



    midi_init();
    
    midi.tracks = 0; //Important so that the random value in tracks does not get used in "free";
    midi.us_per_tick  = DEFAULT_US_PER_TICK;
    midi_result_t res = midi_next(&midi);

    printf("Res: %d, #%d: %s, type: %d, tracks: %d, time: %d\n", res, midi.index, midi.name, midi.type, midi.track_cnt, midi.time_div);

    for (uint8_t i = 0; i < midi.track_cnt; i++)
    {
        printf("\tTrack %d, size: %d, addr: 0x%08x\n",i, midi.tracks[i].size, midi.tracks[i].address);
    }

    process_all_tracks(&midi);

    //start the ticks
    

    // res = midi_previous(&midi);
    // printf("Res: %d, #%d: %s, type: %d, tracks: %d, time: %d\n", res, midi.index, midi.name, midi.type, midi.track_cnt, midi.time_div);
    // res = midi_previous(&midi);
    // printf("Res: %d, #%d: %s, type: %d, tracks: %d, time: %d\n", res, midi.index, midi.name, midi.type, midi.track_cnt, midi.time_div);
    // res = midi_previous(&midi);
    // printf("Res: %d, #%d: %s, type: %d, tracks: %d, time: %d\n", res, midi.index, midi.name, midi.type, midi.track_cnt, midi.time_div);
    // res = midi_previous(&midi);
    // printf("Res: %d, #%d: %s, type: %d, tracks: %d, time: %d\n", res, midi.index, midi.name, midi.type, midi.track_cnt, midi.time_div);
   

    puts("Goodbye, world!");
sleep_ms(2000);
   alarm_in_us(midi.us_per_tick);
    while(1){

        if (alarm_fired)
        {
            alarm_fired = false;
            printf("sending %u bytes for tick %u\n", midi.tick_data[_tick_index].length, _ticks );
            start_transmit(&midi.event_bytes[midi.tick_data[_tick_index].data_index], midi.tick_data[_tick_index].length);
          
        }

    //    if (process_midi  )
    //    {
    //         if (!is_transmitting())
    //         {
    //             stage_data_transmit();
    //         }

    //         if (process_all_tracks(&midi, _ticks) != OK)
    //         {
    //             printf("Process all tracks failed\n");
    //         }
    //         // printf("tick us: %u\n", midi.us_per_tick);
    //         process_midi = false;
    //    }
    }
}
