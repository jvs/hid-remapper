#include <stdio.h>
#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include "oled.h"

static void i2c_scan() {
    printf("Scanning I2C bus...\n");
    for (int addr = 0x08; addr < 0x78; ++addr) {
        uint8_t rxdata;
        if (i2c_read_blocking(i2c1, addr, &rxdata, 1, false) >= 0) {
            printf("Found I2C device at address 0x%02X\n", addr);
        }
    }
}

int main() {
    // Initialize system
    stdio_init_all();
    
    printf("Clean Remapper - Step 1: Hello World\n");
    
    // Scan for I2C devices first
    i2c_scan();
    
    // Initialize OLED display
    if (oled_init()) {
        printf("OLED initialized successfully\n");
        
        // Display hello world message
        oled_display_text("Hello World!");
        
        // Keep the message visible and blink LED to show we're alive
        while (true) {
            printf("Hello from clean remapper!\n");
            sleep_ms(2000);
            
            // Update display every few seconds
            static int counter = 0;
            char msg[32];
            snprintf(msg, sizeof(msg), "Count: %d", counter++);
            oled_display_text(msg);
        }
    } else {
        printf("Failed to initialize OLED\n");
        while (true) {
            printf("OLED init failed - check connections\n");
            sleep_ms(1000);
        }
    }
    
    return 0;
}