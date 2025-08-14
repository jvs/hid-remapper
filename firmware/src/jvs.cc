#include "jvs.h"
#include "globals.h"
#include "remapper.h"
#include "our_descriptor.h"
#include "hardware/gpio.h"
#include <cstring>
#include <unordered_map>

// Constants from remapper.cc
#define MAX_REPORT_SIZE 64
#define OR_BUFSIZE 8

// Keyboard and mouse report IDs (values from our_descriptor.cc)
#define REPORT_ID_KEYBOARD 2
#define REPORT_ID_MOUSE 1

// External report arrays from remapper.cc - THIS is what sends to the computer
extern uint8_t* reports[MAX_INPUT_REPORT_ID + 1];
extern uint16_t report_sizes[MAX_INPUT_REPORT_ID + 1];

// State tracking
static bool caps_lock_active = false;
static bool ctrl_pressed = false;
static bool space_pressed = false;
static bool ctrl_space_detected = false;

// Event queue for timed sequences
struct QueuedEvent {
    uint32_t time_ms;
    uint8_t report[16];
    uint8_t size;
    bool active;
};

static QueuedEvent event_queue[32];
static uint8_t queue_head = 0;
static uint8_t queue_tail = 0;
static uint32_t current_time_ms = 0;

void jvs_init() {
    // Initialize event queue
    memset(event_queue, 0, sizeof(event_queue));
    queue_head = 0;
    queue_tail = 0;
    current_time_ms = 0;
}

// Helper function to queue a keyboard report
void queue_keyboard_report(const uint8_t* report_data, uint8_t size, uint32_t delay_ms = 0) {

    if (delay_ms == 0) {
        // Send immediately to reports[REPORT_ID_KEYBOARD] - this is the CORRECT output path!
        if (reports[REPORT_ID_KEYBOARD] != nullptr && report_sizes[REPORT_ID_KEYBOARD] >= size) {
            memcpy(reports[REPORT_ID_KEYBOARD], report_data, size);
        }
    } else {
        // Queue for later
        uint8_t next_tail = (queue_tail + 1) % 32;
        if (next_tail != queue_head) {
            event_queue[queue_tail].time_ms = current_time_ms + delay_ms;
            memcpy(event_queue[queue_tail].report, report_data, size);
            event_queue[queue_tail].size = size;
            event_queue[queue_tail].active = true;
            queue_tail = next_tail;
        }
    }
}

// Helper to create a keyboard report with modifiers and key
void send_key(uint8_t modifiers, uint8_t key, uint32_t delay_ms = 0) {
    uint8_t report[16] = {0};
    report[0] = modifiers;  // Modifier byte
    report[2] = key;        // First key slot
    queue_keyboard_report(report, 16, delay_ms);
}

// Helper to send key release (all zeros)
void send_key_release(uint32_t delay_ms = 0) {
    uint8_t report[16] = {0};
    queue_keyboard_report(report, 16, delay_ms);
}

// Send "Hello World!" sequence
void send_hello_world() {
    // H (shift + h)
    send_key(0x02, 0x0B, 0);    // H
    send_key_release(50);

    // e
    send_key(0x00, 0x08, 50);
    send_key_release(100);

    // l
    send_key(0x00, 0x0F, 100);
    send_key_release(150);

    // l
    send_key(0x00, 0x0F, 150);
    send_key_release(200);

    // o
    send_key(0x00, 0x12, 200);
    send_key_release(250);

    // space
    send_key(0x00, 0x2C, 250);
    send_key_release(300);

    // W (shift + w)
    send_key(0x02, 0x1A, 300);
    send_key_release(350);

    // o
    send_key(0x00, 0x12, 350);
    send_key_release(400);

    // r
    send_key(0x00, 0x15, 400);
    send_key_release(450);

    // l
    send_key(0x00, 0x0F, 450);
    send_key_release(500);

    // d
    send_key(0x00, 0x07, 500);
    send_key_release(550);

    // !
    send_key(0x02, 0x1E, 550);
    send_key_release(600);
}

bool jvs_handle_input(uint32_t usage, int32_t state) {
    // Detect Caps Lock (usage 0x00070039)
    if (usage == 0x00070039 && state == 1) {
        caps_lock_active = !caps_lock_active;
        return false; // Don't pass through caps lock
    }

    // Track Ctrl key (left ctrl: 0x000700E0, right ctrl: 0x000700E4)
    if (usage == 0x000700E0 || usage == 0x000700E4) {
        ctrl_pressed = (state != 0);
        return true; // Pass through ctrl key normally
    }

    // Track Space key (0x0007002C)
    if (usage == 0x0007002C) {
        space_pressed = (state != 0);

        // Detect Ctrl+Space combination
        if (ctrl_pressed && space_pressed && !ctrl_space_detected) {
            ctrl_space_detected = true;
            send_hello_world();
            return false; // Don't pass through the space press
        } else if (!space_pressed) {
            ctrl_space_detected = false;
        }

        return true; // Pass through space normally if not ctrl+space
    }

    // Example 1: Remap 'a' to 'b' when caps lock is active
    if (caps_lock_active && usage == 0x00070004 && state != 0) {
        // Send 'b' instead of 'a'
        send_key(0x00, 0x05); // 0x05 is 'b'
        return false; // Don't pass through the original 'a'
    }

    // For all other events, pass through to normal hid-remapper processing
    return true;
}

void jvs_process_mapping(bool auto_repeat) {
    // Update our time counter (simplified - in real implementation you'd use hardware timer)
    current_time_ms += 1; // Assume 1ms per call (adjust based on actual call frequency)

    // Process queued events
    while (queue_head != queue_tail && event_queue[queue_head].active) {
        if (current_time_ms >= event_queue[queue_head].time_ms) {
            // Time to send this event to reports[REPORT_ID_KEYBOARD]
            if (reports[REPORT_ID_KEYBOARD] != nullptr && report_sizes[REPORT_ID_KEYBOARD] >= event_queue[queue_head].size) {
                memcpy(reports[REPORT_ID_KEYBOARD], event_queue[queue_head].report, event_queue[queue_head].size);
            }

            // Mark event as processed
            event_queue[queue_head].active = false;
            queue_head = (queue_head + 1) % 32;
        } else {
            break; // Events are ordered by time
        }
    }
}

