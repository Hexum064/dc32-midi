# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
#set(PICO_SDK_PATH "/home/carlk/pi/pico/pico-sdk")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(grumpy2-main C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(./FatFs_SPI build)

# Add executable. Default name is the project name, version 0.1
add_executable(grumpy2-main 
    main.c
    hw_config.c
    midi_file_utils.c
    midi_utils.c
    midi_player.c
    midi_uart.c    
)

# Add the standard library and FatFS/SPI to the build
target_link_libraries(grumpy2-main 
    pico_stdlib
    FatFs_SPI
)

pico_set_program_name(grumpy2-main "grumpy2-main")
pico_set_program_version(grumpy2-main "0.1")

# Choose source and destination for standard input and output:
#   See 4.1. Serial input and output on Raspberry Pi Pico in Getting started with Raspberry Pi Pico (https://datasheets.raspberrypi.org/pico/getting-started-with-pico.pdf)
#   and 2.7.1. Standard Input/Output (stdio) Support in Raspberry Pi Pico C/C++ SDK (https://datasheets.raspberrypi.org/pico/raspberry-pi-pico-c-sdk.pdf):
pico_enable_stdio_uart(grumpy2-main 0)
pico_enable_stdio_usb(grumpy2-main 1)

pico_add_extra_outputs(grumpy2-main)

