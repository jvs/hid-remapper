#include <stdio.h>
#include <pico/stdlib.h>
#include "oled.h"

int main() {
    // Initialize system
    stdio_init_all();
    
    printf("Clean Remapper - Step 1: Hello World\n");
    
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