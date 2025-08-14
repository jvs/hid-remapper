#include <cstdio>
#include <cstring>
#include <unordered_set>

#include "config.h"
#include "crc.h"
#include "globals.h"
#include "interval_override.h"
#include "our_descriptor.h"
#include "platform.h"
#include "remapper.h"

const uint8_t CONFIG_VERSION = 18;



void load_config(const uint8_t* persisted_config) {
}


PersistConfigReturnCode persist_config() {
    return PersistConfigReturnCode::SUCCESS;
}

void reset_resolution_multiplier() {
    // reset hi-res scroll on reboots
    resolution_multiplier = 0;
}

uint16_t handle_get_report1(uint8_t report_id, uint8_t* buffer, uint16_t reqlen) {
    return 0;
}

void handle_set_report1(uint8_t report_id, uint8_t const* buffer, uint16_t bufsize) {
}
