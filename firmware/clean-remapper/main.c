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
    
    // Initialize onboard LED for debugging
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    printf("Clean Remapper - Step 1: Hello World\n");
    
    // Scan for I2C devices first
    i2c_scan();
    
    // Initialize OLED display
    if (oled_init()) {
        printf("OLED initialized successfully\n");
        
        // Display hello world message
        oled_display_text("Hello World!");
        
        // Keep the message visible and blink LED constantly
        while (true) {
            // Blink LED
            gpio_put(LED_PIN, 1);
            sleep_ms(500);
            gpio_put(LED_PIN, 0);
            sleep_ms(500);
            
            printf("Hello from clean remapper!\n");
            
            // Update display every few seconds
            static int counter = 0;
            char msg[32];
            snprintf(msg, sizeof(msg), "Count: %d", counter++);
            oled_display_text(msg);
        }
    } else {
        printf("Failed to initialize OLED\n");
        while (true) {
            // Blink LED even if OLED fails
            gpio_put(LED_PIN, 1);
            sleep_ms(200);
            gpio_put(LED_PIN, 0);
            sleep_ms(200);
            
            printf("OLED init failed - check connections\n");
        }
    }
    
    return 0;
}