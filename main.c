
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
bool process_midi = false;
uint32_t _ticks = 0;
uint32_t _next_tick = 0;


// Alarm interrupt handler
static volatile bool alarm_fired;
static void alarm_in_us(uint32_t delay_us) ;

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


int main() {
    int i = 0;
    
    stdio_init_all();

    serial_init();

sleep_ms(2000);

    printf("Starting...\n");

    puts("Hello, world!");

     

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

    // process_all_tracks(&midi);

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

//   
    while(1){
        //alarm_fired = false;
       
        // Wait for alarm to fire
       // while (!alarm_fired);

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
