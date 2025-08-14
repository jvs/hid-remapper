#include <cstdio>
#include <cstring>
#include <unordered_set>

#include "config.h"
#include "globals.h"
#include "interval_override.h"
#include "our_descriptor.h"
#include "platform.h"
#include "remapper.h"

const uint8_t CONFIG_VERSION = 18;


PersistConfigReturnCode persist_config() {
    return PersistConfigReturnCode::SUCCESS;
}

void reset_resolution_multiplier() {
    // reset hi-res scroll on reboots
    resolution_multiplier = 0;
}
