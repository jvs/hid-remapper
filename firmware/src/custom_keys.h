#pragma once

#include <stdint.h>

// Initialize the custom key handler
void custom_keys_init();

// Handle input events (called from set_input_state)
void custom_keys_handle_input(uint32_t usage, int32_t state_raw, int32_t state_scaled, uint8_t hub_port);

// Process timing and state transitions (called from process_mapping)
void custom_keys_process();

// HID Usage codes for commonly used keys
#define HID_KEY_A           0x00070004
#define HID_KEY_D           0x00070007
#define HID_KEY_F           0x00070009
#define HID_KEY_J           0x0007000D
#define HID_KEY_K           0x0007000E
#define HID_KEY_ESCAPE      0x00070029
#define HID_KEY_CAPSLOCK    0x00070039

#define HID_KEY_LEFT_CTRL   0x000700E0
#define HID_KEY_LEFT_ALT    0x000700E2

#define HID_KEY_UP          0x00070052
#define HID_KEY_DOWN        0x00070051
#define HID_KEY_LEFT        0x00070050
#define HID_KEY_RIGHT       0x0007004F

#define HID_MOUSE_X         0x00010030
#define HID_MOUSE_Y         0x00010031
#define HID_SCROLL_Y        0x00010038

// Symbol HID codes
#define HID_KEY_TILDE       0x00070035  // ~ (Shift + `)
#define HID_KEY_BACKTICK    0x00070035  // `
#define HID_KEY_EXCLAIM     0x0007001E  // ! (Shift + 1)
#define HID_KEY_AT          0x0007001F  // @ (Shift + 2)
#define HID_KEY_HASH        0x00070020  // # (Shift + 3)
#define HID_KEY_DOLLAR      0x00070021  // $ (Shift + 4)
#define HID_KEY_PERCENT     0x00070022  // % (Shift + 5)
#define HID_KEY_CARET       0x00070023  // ^ (Shift + 6)
#define HID_KEY_AMPERSAND   0x00070024  // & (Shift + 7)
#define HID_KEY_ASTERISK    0x00070025  // * (Shift + 8)
#define HID_KEY_LPAREN      0x00070026  // ( (Shift + 9)
#define HID_KEY_RPAREN      0x00070027  // ) (Shift + 0)
#define HID_KEY_LBRACKET    0x0007002F  // [
#define HID_KEY_RBRACKET    0x00070030  // ]
#define HID_KEY_LBRACE      0x0007002F  // { (Shift + [)
#define HID_KEY_RBRACE      0x00070030  // } (Shift + ])
#define HID_KEY_PIPE        0x00070031  // | (Shift + \)
#define HID_KEY_BACKSLASH   0x00070031  // \
#define HID_KEY_SLASH       0x00070038  // /
#define HID_KEY_QUOTE       0x00070034  // '
#define HID_KEY_DQUOTE      0x00070034  // " (Shift + ')
#define HID_KEY_COLON       0x00070033  // : (Shift + ;)
#define HID_KEY_SEMICOLON   0x00070033  // ;
#define HID_KEY_LESS        0x00070036  // < (Shift + ,)
#define HID_KEY_GREATER     0x00070037  // > (Shift + .)
#define HID_KEY_EQUAL       0x0007002E  // =
#define HID_KEY_MINUS       0x0007002D  // -
#define HID_KEY_PLUS        0x0007002E  // + (Shift + =)
#define HID_KEY_QUESTION    0x00070038  // ? (Shift + /)
#define HID_KEY_DOT         0x00070037  // .
#define HID_KEY_COMMA       0x00070036  // ,

#define HID_MODIFIER_SHIFT  0x00070002