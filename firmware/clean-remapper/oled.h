#ifndef OLED_H
#define OLED_H

#include <stdbool.h>

// Initialize the OLED display
bool oled_init(void);

// Display text on the OLED (simple implementation)
void oled_display_text(const char* text);

// Clear the display
void oled_clear(void);

#endif