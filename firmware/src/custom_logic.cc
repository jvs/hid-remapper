#include "custom_logic.h"
#include "globals.h"
#include <cstdio>
#include <cstring>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/i2c.h>
#include "pico/stdlib.h"

// Caps lock state tracking
static bool caps_lock_state = false;

// NeoPixel state
static bool neopixel_enabled = true;
static uint8_t neopixel_r = 0, neopixel_g = 255, neopixel_b = 0; // Default green

void custom_logic_init() {
    printf("Custom logic initialized\n");
    neopixel_init();
    oled_init();
}

void custom_logic_process() {
    // Called periodically from main loop
    // Update displays, handle state changes, etc.
}

bool custom_remap_key(uint32_t usage, int32_t* value) {
    // HID usage for Caps Lock is 0x00070039
    const uint32_t CAPS_LOCK_USAGE = 0x00070039;
    // HID usage for 'A' key is 0x00070004
    const uint32_t A_KEY_USAGE = 0x00070004;
    // HID usage for 'B' key is 0x00070005
    // const uint32_t B_KEY_USAGE = 0x00070005;  // Reserved for future use
    
    // Track caps lock state
    if (usage == CAPS_LOCK_USAGE && *value > 0) {
        caps_lock_state = !caps_lock_state;
        printf("Caps Lock: %s\n", caps_lock_state ? "ON" : "OFF");
        
        // Update NeoPixel color based on caps lock state
        if (caps_lock_state) {
            neopixel_set_color(255, 0, 0); // Red when caps on
        } else {
            neopixel_set_color(0, 255, 0); // Green when caps off
        }
        
        // Update OLED display
        oled_update_status(caps_lock_state ? "CAPS: ON" : "CAPS: OFF");
        
        return false; // Don't modify the caps lock key itself
    }
    
    // Remap A to B when caps lock is on
    if (usage == A_KEY_USAGE && caps_lock_state && *value > 0) {
        printf("Remapping A -> B (caps lock is on)\n");
        // This would need integration with the remapping system
        // For now, just log the event
        return true; // Indicate we handled this key
    }
    
    return false; // No custom remapping applied
}

// NeoPixel functions (Feather-specific)
#define NEOPIXEL_PIN 16  // Feather built-in NeoPixel pin

void neopixel_init() {
    // Initialize NeoPixel GPIO pin
    gpio_init(NEOPIXEL_PIN);
    gpio_set_dir(NEOPIXEL_PIN, GPIO_OUT);
    gpio_put(NEOPIXEL_PIN, 0);
    printf("NeoPixel initialized on pin %d\n", NEOPIXEL_PIN);
}

// Simple bit-bang WS2812 implementation
static void neopixel_send_bit(bool bit) {
    if (bit) {
        // HIGH for ~0.8us, LOW for ~0.45us
        gpio_put(NEOPIXEL_PIN, 1);
        sleep_us(1);  // Approximately 0.8us
        gpio_put(NEOPIXEL_PIN, 0);
        sleep_us(1);  // Approximately 0.45us (will be longer but should work)
    } else {
        // HIGH for ~0.4us, LOW for ~0.85us  
        gpio_put(NEOPIXEL_PIN, 1);
        sleep_us(1);  // Approximately 0.4us (will be longer but should work)
        gpio_put(NEOPIXEL_PIN, 0);
        sleep_us(1);  // Approximately 0.85us
    }
}

static void neopixel_send_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        neopixel_send_bit((byte >> i) & 1);
    }
}

void neopixel_set_color(uint8_t r, uint8_t g, uint8_t b) {
    neopixel_r = r;
    neopixel_g = g;
    neopixel_b = b;
    
    if (neopixel_enabled) {
        // Simple implementation without interrupt disabling for now
        // (WS2812 timing might be less precise but should still work)
        
        // WS2812 expects GRB order
        neopixel_send_byte(g);
        neopixel_send_byte(r);
        neopixel_send_byte(b);
        
        // Reset signal (>50us low)
        gpio_put(NEOPIXEL_PIN, 0);
        sleep_us(60);
        
        printf("NeoPixel: R=%d G=%d B=%d\n", r, g, b);
    }
}

void neopixel_set_enabled(bool enabled) {
    neopixel_enabled = enabled;
    if (!enabled) {
        // Turn off NeoPixel
        printf("NeoPixel disabled\n");
    } else {
        // Restore current color
        neopixel_set_color(neopixel_r, neopixel_g, neopixel_b);
    }
}

// OLED functions (SSD1306 via I2C)
#define OLED_I2C_PORT i2c1
#define OLED_I2C_SDA 2   // Feather SDA pin
#define OLED_I2C_SCL 3   // Feather SCL pin  
#define OLED_I2C_ADDR 0x3D  // Adafruit 5297 default address
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// SSD1306 command constants
#define SSD1306_DISPLAYOFF 0xAE
#define SSD1306_DISPLAYON 0xAF
#define SSD1306_SETDISPLAYOFFSET 0xD3
#define SSD1306_SETCOMPINS 0xDA
#define SSD1306_SETVCOMDETECT 0xDB
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5
#define SSD1306_SETPRECHARGE 0xD9
#define SSD1306_SETMULTIPLEX 0xA8
#define SSD1306_SETLOWCOLUMN 0x00
#define SSD1306_SETHIGHCOLUMN 0x10
#define SSD1306_SETSTARTLINE 0x40
#define SSD1306_MEMORYMODE 0x20
#define SSD1306_COLUMNADDR 0x21
#define SSD1306_PAGEADDR 0x22
#define SSD1306_COMSCANINC 0xC0
#define SSD1306_COMSCANDEC 0xC8
#define SSD1306_SEGREMAP 0xA0
#define SSD1306_CHARGEPUMP 0x8D
#define SSD1306_NORMALDISPLAY 0xA6

static bool oled_ready = false;

static void oled_send_command(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};  // 0x00 = command mode
    i2c_write_blocking(OLED_I2C_PORT, OLED_I2C_ADDR, buf, 2, false);
}

static void oled_send_data(const uint8_t* data, size_t len) {
    uint8_t buf[len + 1];
    buf[0] = 0x40;  // 0x40 = data mode
    memcpy(buf + 1, data, len);
    i2c_write_blocking(OLED_I2C_PORT, OLED_I2C_ADDR, buf, len + 1, false);
}

void oled_init() {
    // Initialize I2C
    i2c_init(OLED_I2C_PORT, 400000);  // 400kHz
    gpio_set_function(OLED_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(OLED_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_I2C_SDA);
    gpio_pull_up(OLED_I2C_SCL);
    
    sleep_ms(100);  // Let display settle
    
    // Initialize SSD1306
    oled_send_command(SSD1306_DISPLAYOFF);
    oled_send_command(SSD1306_SETDISPLAYCLOCKDIV);
    oled_send_command(0x80);
    oled_send_command(SSD1306_SETMULTIPLEX);
    oled_send_command(OLED_HEIGHT - 1);
    oled_send_command(SSD1306_SETDISPLAYOFFSET);
    oled_send_command(0x00);
    oled_send_command(SSD1306_SETSTARTLINE | 0x00);
    oled_send_command(SSD1306_CHARGEPUMP);
    oled_send_command(0x14);  // Enable charge pump
    oled_send_command(SSD1306_MEMORYMODE);
    oled_send_command(0x00);  // Horizontal addressing mode
    oled_send_command(SSD1306_SEGREMAP | 0x01);
    oled_send_command(SSD1306_COMSCANDEC);
    oled_send_command(SSD1306_SETCOMPINS);
    oled_send_command(0x12);
    oled_send_command(SSD1306_SETPRECHARGE);
    oled_send_command(0xF1);
    oled_send_command(SSD1306_SETVCOMDETECT);
    oled_send_command(0x40);
    oled_send_command(SSD1306_NORMALDISPLAY);
    oled_send_command(SSD1306_DISPLAYON);
    
    oled_ready = true;
    printf("OLED initialized (SSD1306 128x64)\n");
    oled_clear();
}

void oled_update_status(const char* status) {
    if (!oled_ready) return;
    
    // For now, just display text on first line
    // This is a very basic implementation - a real font would be better
    printf("OLED: %s\n", status);
    
    // Set cursor to start of display
    oled_send_command(SSD1306_COLUMNADDR);
    oled_send_command(0);   // Column start
    oled_send_command(127); // Column end
    oled_send_command(SSD1306_PAGEADDR);
    oled_send_command(0);   // Page start
    oled_send_command(0);   // Page end (first row only)
    
    // Send a simple pattern representing text
    uint8_t text_pattern[128] = {0};
    for (size_t i = 0; i < strlen(status) && i < 16; i++) {
        // Very basic character representation
        text_pattern[i * 8] = 0xFF;
        text_pattern[i * 8 + 1] = 0x81;
        text_pattern[i * 8 + 2] = 0x81;
        text_pattern[i * 8 + 3] = 0xFF;
    }
    
    oled_send_data(text_pattern, 128);
}

void oled_clear() {
    if (!oled_ready) return;
    
    // Set address range to entire display
    oled_send_command(SSD1306_COLUMNADDR);
    oled_send_command(0);   // Column start
    oled_send_command(127); // Column end
    oled_send_command(SSD1306_PAGEADDR);
    oled_send_command(0);   // Page start
    oled_send_command(7);   // Page end
    
    // Clear all pages
    uint8_t clear_data[128] = {0};
    for (int page = 0; page < 8; page++) {
        oled_send_data(clear_data, 128);
    }
    
    printf("OLED cleared\n");
}