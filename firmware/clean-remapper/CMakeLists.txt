cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../pico-sdk")
set(PICO_BOARD "adafruit_feather_rp2040")

include(../pico_sdk_import.cmake)

project(clean_remapper)

pico_sdk_init()

add_executable(clean_remapper
    main.c
    oled.c
)

target_include_directories(clean_remapper PRIVATE
    .
)

target_link_libraries(clean_remapper
    pico_stdlib
    pico_stdio_usb
    hardware_i2c
    hardware_gpio
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(clean_remapper 1)
pico_enable_stdio_uart(clean_remapper 0)

pico_add_extra_outputs(clean_remapper)