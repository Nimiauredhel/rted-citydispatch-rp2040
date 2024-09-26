cmake_minimum_required(VERSION 3.12)
set(PICO_SDK_PATH "~/Libraries/pico-sdk")

# pull in pico sdk (must be before project)
include(pico_sdk_import.cmake)

project(program C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD_17)

set(PICO_EXAMPLES_PATH ${PROJECT_SOURCE_DIR})

# initialize the sdk
pico_sdk_init()

add_executable(program
    program.c
)

FILE(GLOB FreeRTOS_src FreeRTOS-Kernel/*.c)

add_library( FreeRTOS STATIC
    ${FreeRTOS_src}
    FreeRTOS-Kernel/portable/GCC/ARM_CM0/port.c
    FreeRTOS-Kernel/portable/MemMang/heap_4.c
)

target_include_directories( FreeRTOS PUBLIC
    FreeRTOS-Config
    FreeRTOS-Kernel/include
    FreeRTOS-Kernel/portable/GCC/ARM_CM0
)

# pull in out pico_stdlib which pulls in common-
target_link_libraries( program
    pico_stdlib
    pico_malloc
    pico_stdio_usb
    FreeRTOS
)

# create map/bin/hex file etc.
pico_add_extra_outputs(program)
