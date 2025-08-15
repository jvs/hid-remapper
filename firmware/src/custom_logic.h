#ifndef _CUSTOM_LOGIC_H_
#define _CUSTOM_LOGIC_H_

#include <stdint.h>

// Custom remapping logic functions
void custom_logic_init();
void custom_logic_process();
bool custom_remap_key(uint32_t usage, int32_t* value);

// NeoPixel control
void neopixel_init();
void neopixel_set_color(uint8_t r, uint8_t g, uint8_t b);
void neopixel_set_enabled(bool enabled);

// OLED display control
void oled_init();
void oled_update_status(const char* status);
void oled_clear();

#endif