# Clean Remapper

A fresh start on building a USB HID remapper with clean, understandable code.

## Step 1: Hello World ✨
- Display "Hello World" on OLED
- Count up every 2 seconds
- Verify hardware is working

## Hardware Setup
- Adafruit Feather RP2040
- OLED display (SSD1306 128x64) connected via STEMMA QT
  - SDA → Pin 2
  - SCL → Pin 3
  - VCC → 3.3V
  - GND → GND

## Build
```bash
mkdir build
cd build
cmake ..
make
```

## Flash
Copy `clean_remapper.uf2` to the Feather in bootloader mode.

## Next Steps
- Step 2: USB Host - Read keyboard events
- Step 3: Mouse Support - Read mouse events  
- Step 4: USB Device - Forward events unchanged
- Step 5: State machine - Custom remapping logic
- Step 6: Ticks - Timing-based features