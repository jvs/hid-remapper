#include "custom_keys.h"
#include "remapper.h"
#include <hardware/timer.h>
#include <hardware/gpio.h>
#include <pico/time.h>
#include <pico/stdio.h>
#include <cstring>
#include <cstdio>
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/sync.h>


// Timing constants (in microseconds)
static const uint64_t COMBO_WINDOW = 50000;      // 50ms window for J+K combo
static const uint64_t LEADER_TIMEOUT = 1000000;  // 1000ms leader timeout
static const uint64_t TAP_HOLD_THRESHOLD = 200000; // 200ms tap-hold threshold

// States
enum CustomKeyState {
    STATE_NORMAL,
    STATE_LEADER_WAITING,
    STATE_LEADER_FIRST
};

// Key state tracking
struct KeyState {
    bool pressed;
    bool prev_pressed;
    uint64_t press_time;
    uint64_t release_time;
};

// Modifier state (for F/D home row modifiers)
struct ModifierState {
    bool pressed;
    bool activated;
    uint64_t press_time;
    bool other_key_pressed;
};

// Leader sequence structure
struct LeaderSequence {
    uint32_t first_key;
    uint32_t second_key;
    uint32_t output_key;
    bool needs_shift;
};

// Global state
static CustomKeyState current_state = STATE_NORMAL;
static uint64_t state_start_time = 0;
static uint32_t leader_first_key = 0;
static bool alt_is_held = false;

// Recursion guard
static bool in_custom_handler = false;

// Simple NeoPixel control (assuming GPIO 16 for Feather RP2040)
#define NEOPIXEL_PIN 16
static void neopixel_set_color(uint8_t r, uint8_t g, uint8_t b) {
    // Simple bit-bang NeoPixel control
    gpio_init(NEOPIXEL_PIN);
    gpio_set_dir(NEOPIXEL_PIN, GPIO_OUT);
    
    uint32_t color = (g << 16) | (r << 8) | b; // GRB format
    
    // Disable interrupts for timing-critical section
    uint32_t interrupts = save_and_disable_interrupts();
    
    for (int bit = 23; bit >= 0; bit--) {
        if (color & (1 << bit)) {
            // Send 1: high for ~800ns, low for ~450ns
            gpio_put(NEOPIXEL_PIN, 1);
            sleep_us(1);
            gpio_put(NEOPIXEL_PIN, 0);
            sleep_us(1);
        } else {
            // Send 0: high for ~400ns, low for ~850ns  
            gpio_put(NEOPIXEL_PIN, 1);
            sleep_us(1);
            gpio_put(NEOPIXEL_PIN, 0);
            sleep_us(1);
        }
    }
    
    restore_interrupts(interrupts);
    sleep_us(50); // Reset time
}

// Key states
static KeyState j_state = {0};
static KeyState k_state = {0};
static KeyState caps_state = {0};

// Modifier states
static ModifierState f_modifier = {0};
static ModifierState d_modifier = {0};

// Leader sequences lookup table
static const LeaderSequence leader_sequences[] = {
    {'t', 'i', HID_KEY_TILDE, true},                    // ti -> ~
    {'t', 'l', HID_KEY_TILDE, true},                    // tl -> ~
    {'b', 't', HID_KEY_BACKTICK, false},                // bt -> `
    {'e', 'x', HID_KEY_EXCLAIM, true},                  // ex -> !
    {'a', 't', HID_KEY_AT, true},                       // at -> @
    {'h', 'a', HID_KEY_HASH, true},                     // ha -> hash
    {'d', 'o', HID_KEY_DOLLAR, true},                   // do -> $
    {'d', 'l', HID_KEY_DOLLAR, true},                   // dl -> $
    {'p', 'c', HID_KEY_PERCENT, true},                  // pc -> %
    {'p', 'e', HID_KEY_PERCENT, true},                  // pe -> %
    {'c', 'a', HID_KEY_CARET, true},                    // ca -> ^
    {'a', 'm', HID_KEY_AMPERSAND, true},                // am -> &
    {'s', 't', HID_KEY_ASTERISK, true},                 // st -> *
    {'m', 'u', HID_KEY_ASTERISK, true},                 // mu -> *
    {'m', 'l', HID_KEY_ASTERISK, true},                 // ml -> *
    {'l', 'p', HID_KEY_LPAREN, true},                   // lp -> (
    {'r', 'p', HID_KEY_RPAREN, true},                   // rp -> )
    {'l', 's', HID_KEY_LBRACKET, false},                // ls -> [
    {'r', 's', HID_KEY_RBRACKET, false},                // rs -> ]
    {'l', 'c', HID_KEY_LBRACE, true},                   // lc -> {
    {'r', 'c', HID_KEY_RBRACE, true},                   // rc -> }
    {'p', 'i', HID_KEY_PIPE, true},                     // pi -> |
    {'p', 'p', HID_KEY_PIPE, true},                     // pp -> |
    {'b', 's', HID_KEY_BACKSLASH, false},               // bs -> backslash
    {'f', 's', HID_KEY_SLASH, false},                   // fs -> /
    {'d', 'q', HID_KEY_DQUOTE, true},                   // dq -> "
    {'s', 'q', HID_KEY_QUOTE, false},                   // sq -> '
    {'c', 'l', HID_KEY_COLON, true},                    // cl -> :
    {'c', 'n', HID_KEY_COLON, true},                    // cn -> :
    {'c', 'o', HID_KEY_COLON, true},                    // co -> :
    {'s', 'c', HID_KEY_SEMICOLON, false},               // sc -> ;
    {'l', 'a', HID_KEY_LESS, true},                     // la -> <
    {'l', 't', HID_KEY_LESS, true},                     // lt -> <
    {'r', 'a', HID_KEY_GREATER, true},                  // ra -> >
    {'g', 't', HID_KEY_GREATER, true},                  // gt -> >
    {'e', 'q', HID_KEY_EQUAL, false},                   // eq -> =
    {'d', 'a', HID_KEY_MINUS, false},                   // da -> -
    {'m', 'i', HID_KEY_MINUS, false},                   // mi -> -
    {'p', 'l', HID_KEY_PLUS, true},                     // pl -> +
    {'q', 'q', HID_KEY_QUESTION, true},                 // qq -> ?
    {'q', 'u', HID_KEY_QUESTION, true},                 // qu -> ?
    {'d', 'd', HID_KEY_DOT, false},                     // dd -> .
    {'d', 't', HID_KEY_DOT, false},                     // dt -> .
    {'c', 'm', HID_KEY_COMMA, false},                   // cm -> ,
};

static const size_t NUM_LEADER_SEQUENCES = sizeof(leader_sequences) / sizeof(leader_sequences[0]);


// Helper function to convert HID usage code to character (for leader sequences)
static char hid_to_letter(uint32_t usage) {
    if (usage >= 0x00070004 && usage <= 0x0007001D) {
        return 'a' + (usage - 0x00070004);
    }
    return 0;
}

// Helper function to emit a key press - use recursion guard
static void emit_key_press(uint32_t usage) {
    // Temporarily disable custom handler to avoid recursion
    bool was_in_handler = in_custom_handler;
    in_custom_handler = true;
    set_input_state(usage, 1, 1, 0);
    in_custom_handler = was_in_handler;
}

// Helper function to emit a key release - use recursion guard
static void emit_key_release(uint32_t usage) {
    // Temporarily disable custom handler to avoid recursion
    bool was_in_handler = in_custom_handler;
    in_custom_handler = true;
    set_input_state(usage, 0, 0, 0);
    in_custom_handler = was_in_handler;
}

// Helper function to emit a key tap (press + release)
static void emit_key_tap(uint32_t usage, bool with_shift = false) {
    if (with_shift) {
        emit_key_press(HID_MODIFIER_SHIFT);
    }
    emit_key_press(usage);
    emit_key_release(usage);
    if (with_shift) {
        emit_key_release(HID_MODIFIER_SHIFT);
    }
}

// Helper function to update key state
static void update_key_state(KeyState* state, bool pressed) {
    uint64_t now = time_us_64();
    state->prev_pressed = state->pressed;
    state->pressed = pressed;

    if (pressed && !state->prev_pressed) {
        state->press_time = now;
    } else if (!pressed && state->prev_pressed) {
        state->release_time = now;
    }
}

// Check if a key just transitioned from released to pressed
static bool key_just_pressed(const KeyState* state) {
    return state->pressed && !state->prev_pressed;
}


// Simple combo detection: if both J and K are pressed, emit Escape
// Returns true if combo was detected (caller should block the keys)
static bool handle_combo_logic(uint32_t usage, bool pressed) {
    if (usage == HID_KEY_J) {
        update_key_state(&j_state, pressed);
    } else if (usage == HID_KEY_K) {
        update_key_state(&k_state, pressed);
    }

    // Check if both J and K are currently pressed
    if (j_state.pressed && k_state.pressed) {
        // Combo detected! Flash purple to indicate escape
        neopixel_set_color(128, 0, 128); // Purple for combo detected
        
        // Emit escape and reset states
        emit_key_tap(HID_KEY_ESCAPE);

        // Reset key states to avoid re-triggering
        j_state.pressed = false;
        k_state.pressed = false;

        return true; // Block both keys
    }

    return false; // No combo, let keys pass through
}

// Handle home row modifiers with sm_td-style timing
static void handle_home_row_modifier(ModifierState* mod, uint32_t mod_usage,
                                   uint32_t key_usage, bool pressed) {
    uint64_t now = time_us_64();

    if (key_usage == HID_KEY_F || key_usage == HID_KEY_D) {
        if (pressed && !mod->pressed) {
            // Key just pressed
            mod->pressed = true;
            mod->press_time = now;
            mod->other_key_pressed = false;
            mod->activated = false;
        } else if (!pressed && mod->pressed) {
            // Key just released
            mod->pressed = false;

            if (mod->activated) {
                // Was acting as modifier - release it
                emit_key_release(mod_usage);
                mod->activated = false;
            } else if (!mod->other_key_pressed &&
                      (now - mod->press_time < TAP_HOLD_THRESHOLD)) {
                // Was a tap - emit the original key
                emit_key_tap(key_usage);
            }
        }
    } else {
        // Some other key event
        if (mod->pressed && pressed) {
            // Another key pressed while modifier key is held
            mod->other_key_pressed = true;
            if (!mod->activated) {
                // Activate the modifier
                emit_key_press(mod_usage);
                mod->activated = true;
            }
        }
    }
}

// Handle leader key sequences
static void handle_leader_key(uint32_t usage, bool pressed) {
    uint64_t now = time_us_64();

    if (usage == HID_KEY_CAPSLOCK) {
        update_key_state(&caps_state, pressed);

        if (key_just_pressed(&caps_state) && current_state == STATE_NORMAL) {
            current_state = STATE_LEADER_WAITING;
            state_start_time = now;
        } else if (key_just_pressed(&caps_state) &&
                  (current_state == STATE_LEADER_WAITING || current_state == STATE_LEADER_FIRST)) {
            // CapsLock pressed while in leader mode - exit
            current_state = STATE_NORMAL;
        }
        return; // Don't pass CapsLock through to normal processing
    }

    if (current_state == STATE_LEADER_WAITING && pressed) {
        if (usage == HID_KEY_ESCAPE) {
            // Escape exits leader mode
            current_state = STATE_NORMAL;
        } else {
            // First key of sequence
            leader_first_key = usage;
            current_state = STATE_LEADER_FIRST;
            state_start_time = now;
        }
    } else if (current_state == STATE_LEADER_FIRST && pressed) {
        if (usage == HID_KEY_ESCAPE) {
            // Escape exits leader mode
            current_state = STATE_NORMAL;
        } else {
            // Second key of sequence - look up the combination
            char first_char = hid_to_letter(leader_first_key);
            char second_char = hid_to_letter(usage);

            for (size_t i = 0; i < NUM_LEADER_SEQUENCES; i++) {
                if (leader_sequences[i].first_key == first_char &&
                    leader_sequences[i].second_key == second_char) {
                    // Found matching sequence!
                    emit_key_tap(leader_sequences[i].output_key, leader_sequences[i].needs_shift);
                    current_state = STATE_NORMAL;
                    return;
                }
            }
            // No matching sequence found - just exit leader mode
            current_state = STATE_NORMAL;
        }
    }
}

// Handle mouse movement -> arrow keys / scroll
static void handle_mouse_movement(uint32_t usage, int32_t state_raw) {
    if (usage == HID_MOUSE_X) {
        if (state_raw > 0) {
            if (alt_is_held) {
                // Alt + right movement - no horizontal scroll, ignore
            } else {
                emit_key_tap(HID_KEY_RIGHT);
            }
        } else if (state_raw < 0) {
            if (alt_is_held) {
                // Alt + left movement - no horizontal scroll, ignore
            } else {
                emit_key_tap(HID_KEY_LEFT);
            }
        }
    } else if (usage == HID_MOUSE_Y) {
        if (state_raw > 0) {
            if (alt_is_held) {
                // Alt + down movement -> scroll down
                bool was_in_handler = in_custom_handler;
                in_custom_handler = true;
                set_input_state(HID_SCROLL_Y, -1, -1, 0);
                in_custom_handler = was_in_handler;
            } else {
                emit_key_tap(HID_KEY_DOWN);
            }
        } else if (state_raw < 0) {
            if (alt_is_held) {
                // Alt + up movement -> scroll up
                bool was_in_handler = in_custom_handler;
                in_custom_handler = true;
                set_input_state(HID_SCROLL_Y, 1, 1, 0);
                in_custom_handler = was_in_handler;
            } else {
                emit_key_tap(HID_KEY_UP);
            }
        }
    }
}

// Initialize custom key handler
void custom_keys_init() {
    // Debug: Flash LED to show custom_keys_init was called
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);
    gpio_put(25, 1);
    sleep_ms(200);
    gpio_put(25, 0);

    // Debug: Set NeoPixel to blue to show init was called
    neopixel_set_color(0, 0, 255); // Blue

    current_state = STATE_NORMAL;
    memset(&j_state, 0, sizeof(j_state));
    memset(&k_state, 0, sizeof(k_state));
    memset(&caps_state, 0, sizeof(caps_state));
    memset(&f_modifier, 0, sizeof(f_modifier));
    memset(&d_modifier, 0, sizeof(d_modifier));
    alt_is_held = false;
}

// Handle input events
void custom_keys_handle_input_impl(uint32_t usage, int32_t state_raw, int32_t state_scaled, uint8_t hub_port) {
    bool pressed = (state_raw != 0);

    // Track Alt state for mouse scrolling
    if (usage == HID_KEY_LEFT_ALT) {
        alt_is_held = pressed;
    }

    // Handle mouse movement
    if (usage == HID_MOUSE_X || usage == HID_MOUSE_Y) {
        if (state_raw != 0) {  // Only process actual movement
            handle_mouse_movement(usage, state_raw);
            return; // Don't pass mouse movement through
        }
    }

    // Handle combo detection (J+K -> Escape)
    if (usage == HID_KEY_J || usage == HID_KEY_K) {
        bool combo_detected = handle_combo_logic(usage, pressed);
        if (combo_detected) {
            return; // Combo was detected, don't pass the keys through
        }
        // If no combo, let keys pass through normally
    }

    // Handle home row modifiers (F -> Alt, D -> Ctrl)
    if (usage == HID_KEY_F) {
        handle_home_row_modifier(&f_modifier, HID_KEY_LEFT_ALT, HID_KEY_F, pressed);
        return; // Don't pass F through
    } else if (usage == HID_KEY_D) {
        handle_home_row_modifier(&d_modifier, HID_KEY_LEFT_CTRL, HID_KEY_D, pressed);
        return; // Don't pass D through
    } else {
        // For other keys, inform home row modifiers that another key was pressed
        if (pressed) {
            if (f_modifier.pressed) {
                handle_home_row_modifier(&f_modifier, HID_KEY_LEFT_ALT, usage, true);
            }
            if (d_modifier.pressed) {
                handle_home_row_modifier(&d_modifier, HID_KEY_LEFT_CTRL, usage, true);
            }
        }
    }

    // Handle leader key sequences
    handle_leader_key(usage, pressed);
}

void custom_keys_handle_input(uint32_t usage, int32_t state_raw, int32_t state_scaled, uint8_t hub_port) {
    // Debug: Flash LED on key input and log specific keys
    static uint32_t call_count = 0;
    call_count++;

    // Debug specific keys we care about with NeoPixel colors
    if (state_raw != 0) { // Only on key press, not release
        if (usage == HID_KEY_J) {
            neopixel_set_color(255, 0, 0); // Red for J
        } else if (usage == HID_KEY_K) {
            neopixel_set_color(255, 255, 0); // Yellow for K  
        } else if (usage == HID_KEY_F) {
            neopixel_set_color(0, 255, 0); // Green for F
        } else if (usage == HID_KEY_D) {
            neopixel_set_color(255, 0, 255); // Magenta for D
        } else if (usage == HID_KEY_CAPSLOCK) {
            neopixel_set_color(255, 255, 255); // White for CapsLock
        }
    }

    if (!in_custom_handler) {
        in_custom_handler = true;
        custom_keys_handle_input_impl(usage, state_raw, state_scaled, hub_port);
        in_custom_handler = false;
    }
}

// Process timing and state transitions
void custom_keys_process() {
    uint64_t now = time_us_64();

    // Handle timeouts
    switch (current_state) {
        // No combo timeout logic needed with simple approach

        case STATE_LEADER_WAITING:
        case STATE_LEADER_FIRST:
            if (now - state_start_time > LEADER_TIMEOUT) {
                // Leader timeout - exit leader mode
                current_state = STATE_NORMAL;
            }
            break;

        default:
            break;
    }
}
