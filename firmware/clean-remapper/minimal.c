#include <pico/stdlib.h>
#include <hardware/gpio.h>

int main() {
    // Initialize the onboard LED
    const uint LED_PIN = 25;  // Standard Pico LED pin
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    // Blink forever
    while (true) {
        gpio_put(LED_PIN, 1);
        sleep_ms(250);
        gpio_put(LED_PIN, 0);
        sleep_ms(250);
    }
    
    return 0;
}