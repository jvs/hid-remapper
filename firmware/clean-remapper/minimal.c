#include <pico/stdlib.h>
#include <bsp/board_api.h>

int main() {
    // Initialize board (this sets up the LED)
    board_init();
    
    // Blink forever using the same method as hid-remapper
    while (true) {
        board_led_write(true);
        sleep_ms(250);
        board_led_write(false);
        sleep_ms(250);
    }
    
    return 0;
}
