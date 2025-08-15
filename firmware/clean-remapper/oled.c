#include "oled.h"
#include <stdio.h>
#include <string.h>
#include <hardware/i2c.h>
#include <hardware/gpio.h>
#include <pico/stdlib.h>

// OLED configuration for Adafruit SSD1306 128x64
#define OLED_I2C_PORT i2c1
#define OLED_SDA_PIN 2
#define OLED_SCL_PIN 3
#define OLED_ADDR_PRIMARY 0x3D
#define OLED_ADDR_SECONDARY 0x3C

static uint8_t oled_addr = OLED_ADDR_PRIMARY;
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// SSD1306 commands
#define SSD1306_DISPLAYOFF 0xAE
#define SSD1306_DISPLAYON 0xAF
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5
#define SSD1306_SETMULTIPLEX 0xA8
#define SSD1306_SETDISPLAYOFFSET 0xD3
#define SSD1306_SETSTARTLINE 0x40
#define SSD1306_CHARGEPUMP 0x8D
#define SSD1306_MEMORYMODE 0x20
#define SSD1306_SEGREMAP 0xA0
#define SSD1306_COMSCANDEC 0xC8
#define SSD1306_SETCOMPINS 0xDA
#define SSD1306_SETCONTRAST 0x81
#define SSD1306_SETPRECHARGE 0xD9
#define SSD1306_SETVCOMDETECT 0xDB
#define SSD1306_NORMALDISPLAY 0xA6
#define SSD1306_COLUMNADDR 0x21
#define SSD1306_PAGEADDR 0x22

static bool oled_ready = false;

static void oled_send_command(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(OLED_I2C_PORT, oled_addr, buf, 2, false);
}

static void oled_send_data(const uint8_t* data, size_t len) {
    uint8_t buf[len + 1];
    buf[0] = 0x40;  // Data mode
    memcpy(buf + 1, data, len);
    i2c_write_blocking(OLED_I2C_PORT, oled_addr, buf, len + 1, false);
}

bool oled_init(void) {
    // Initialize I2C
    i2c_init(OLED_I2C_PORT, 400000);
    gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_SDA_PIN);
    gpio_pull_up(OLED_SCL_PIN);
    
    sleep_ms(100);
    
    // Try to detect the correct I2C address
    uint8_t test_data;
    if (i2c_read_blocking(OLED_I2C_PORT, OLED_ADDR_PRIMARY, &test_data, 1, false) >= 0) {
        oled_addr = OLED_ADDR_PRIMARY;
        printf("OLED found at address 0x%02X\n", oled_addr);
    } else if (i2c_read_blocking(OLED_I2C_PORT, OLED_ADDR_SECONDARY, &test_data, 1, false) >= 0) {
        oled_addr = OLED_ADDR_SECONDARY;
        printf("OLED found at address 0x%02X\n", oled_addr);
    } else {
        printf("OLED not found at either address!\n");
        return false;
    }
    
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
    oled_send_command(0x14);
    oled_send_command(SSD1306_MEMORYMODE);
    oled_send_command(0x00);
    oled_send_command(SSD1306_SEGREMAP | 0x01);
    oled_send_command(SSD1306_COMSCANDEC);
    oled_send_command(SSD1306_SETCOMPINS);
    oled_send_command(0x12);
    oled_send_command(SSD1306_SETCONTRAST);
    oled_send_command(0xCF);
    oled_send_command(SSD1306_SETPRECHARGE);
    oled_send_command(0xF1);
    oled_send_command(SSD1306_SETVCOMDETECT);
    oled_send_command(0x40);
    oled_send_command(SSD1306_NORMALDISPLAY);
    oled_send_command(SSD1306_DISPLAYON);
    
    oled_ready = true;
    oled_clear();
    
    printf("OLED initialized successfully\n");
    return true;
}

void oled_clear(void) {
    if (!oled_ready) return;
    
    oled_send_command(SSD1306_COLUMNADDR);
    oled_send_command(0);
    oled_send_command(127);
    oled_send_command(SSD1306_PAGEADDR);
    oled_send_command(0);
    oled_send_command(7);
    
    uint8_t clear_data[128] = {0};
    for (int i = 0; i < 8; i++) {
        oled_send_data(clear_data, 128);
    }
}

void oled_display_text(const char* text) {
    if (!oled_ready) return;
    
    printf("OLED: %s\n", text);
    
    // Very simple text display - just draw some pixels for each character
    oled_send_command(SSD1306_COLUMNADDR);
    oled_send_command(0);
    oled_send_command(127);
    oled_send_command(SSD1306_PAGEADDR);
    oled_send_command(0);
    oled_send_command(1);
    
    uint8_t pattern[128] = {0};
    size_t len = strlen(text);
    
    // Simple character representation - each char gets 8 pixels wide
    for (size_t i = 0; i < len && i < 16; i++) {
        pattern[i * 8] = 0xFF;     // Vertical line
        pattern[i * 8 + 1] = 0x81; // Sides
        pattern[i * 8 + 2] = 0x81;
        pattern[i * 8 + 3] = 0x81;
        pattern[i * 8 + 4] = 0x81;
        pattern[i * 8 + 5] = 0x81;
        pattern[i * 8 + 6] = 0x81;
        pattern[i * 8 + 7] = 0xFF; // Vertical line
    }
    
    // Send two rows of pattern
    oled_send_data(pattern, 128);
    oled_send_data(pattern, 128);
}