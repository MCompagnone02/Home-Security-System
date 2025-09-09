cmake_minimum_required(VERSION 3.13)

# Configurazione FreeRTOS PORT per RP2040
set(FREERTOS_PORT GCC_RP2040 CACHE STRING "")

# Include Pico SDK
include(pico_sdk_import.cmake)

# Progetto
project(Home-Security-System C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

# Inizializza Pico SDK
pico_sdk_init()

# Configurazione target FreeRTOS
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config INTERFACE src)
target_compile_definitions(freertos_config
        INTERFACE
        projCOVERAGE_TEST=0
)

# Kernel FreeRTOS
add_subdirectory(${PICO_SDK_PATH}/lib/FreeRTOS-Kernel FreeRTOS-Build)

# Heap manager
target_sources(freertos_kernel PRIVATE
        ${PICO_SDK_PATH}/lib/FreeRTOS-Kernel/portable/MemMang/heap_4.c
)

# Eseguibile principale
add_executable(Home-Security-System
        src/main.c
)

# Directories
target_include_directories(Home-Security-System PRIVATE
        src
)

# Link delle librerie
set(LINK_LIBRARIES
        pico_stdlib
        pico_time
        hardware_gpio
        freertos_kernel
)

# Target link delle librerie
target_link_libraries(Home-Security-System
        pico_stdlib
        pico_time
        hardware_gpio
        freertos_kernel
)

# USB output
pico_enable_stdio_usb(Home-Security-System 1)
pico_enable_stdio_uart(Home-Security-System 1)

# Flags
target_compile_definitions(Home-Security-System
        PRIVATE
        PICO_STACK_SIZE=4096
)
