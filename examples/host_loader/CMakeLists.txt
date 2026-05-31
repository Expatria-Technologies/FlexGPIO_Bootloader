cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)

set(PICO_PLATFORM rp2350 CACHE STRING "")
set(PICO_BOARD flexi_2350)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR} CACHE STRING "")

include(pico_sdk_import.cmake)

project(flexgpio_bootloader_host_example C CXX ASM)

pico_sdk_init()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/firmware_bin.c
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_firmware.py
        ${CMAKE_CURRENT_SOURCE_DIR}/firmware.bin
        ${CMAKE_CURRENT_BINARY_DIR}/firmware_bin.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/firmware.bin
    COMMENT "Embedding firmware binary"
)

add_executable(flexgpio_bootloader_host_example
    main.c
    rp2040_ram_loader.c
    ${CMAKE_CURRENT_BINARY_DIR}/firmware_bin.c
)

target_include_directories(flexgpio_bootloader_host_example PRIVATE .)

target_link_libraries(flexgpio_bootloader_host_example PRIVATE
    pico_stdlib
    hardware_i2c
)

pico_enable_stdio_uart(flexgpio_bootloader_host_example 1)
pico_enable_stdio_usb(flexgpio_bootloader_host_example 0)

pico_add_extra_outputs(flexgpio_bootloader_host_example)
