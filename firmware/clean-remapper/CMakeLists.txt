cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../pico-sdk")
set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/../tinyusb")
set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/../Pico-PIO-USB")
set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/../src/boards")

include(../pico_sdk_import.cmake)

project(clean_remapper)

pico_sdk_init()

check_and_add_pico_pio_usb_support()

add_executable(clean_remapper
    minimal.c
)

target_include_directories(clean_remapper PRIVATE
    .
    ../src
    ../src/tusb_config_both
    ${PICO_TINYUSB_PATH}/src
    ${PICO_TINYUSB_PATH}/hw
)

target_link_libraries(clean_remapper
    pico_stdlib
    tinyusb_device
    tinyusb_board
)

pico_set_binary_type(clean_remapper copy_to_ram)
if(PICO_PLATFORM STREQUAL "rp2040")
    pico_set_linker_script(clean_remapper ${CMAKE_CURRENT_LIST_DIR}/../remapper_single.ld)
elseif(PICO_PLATFORM STREQUAL "rp2350-arm-s")
    pico_set_linker_script(clean_remapper ${CMAKE_CURRENT_LIST_DIR}/../remapper_single_rp2350.ld)
else()
    message(FATAL_ERROR "Unknown PICO_PLATFORM.")
endif()

pico_add_extra_outputs(clean_remapper)