#ifndef _TYPES_H_
#define _TYPES_H_

#include <stdint.h>
#include <cstddef>
#include <vector>

struct usage_def_t {
    uint8_t report_id;
    uint8_t size;
    uint16_t bitpos;
    bool is_relative;
    bool is_array = false;
    bool should_be_scaled = false;
    int32_t logical_minimum;
    int32_t logical_maximum;
    uint32_t index = 0;      // for arrays
    uint32_t count = 0;      // for arrays
    uint32_t usage_maximum;  // effective, for arrays/usage ranges
    int32_t* input_state_0 = NULL;
    int32_t* input_state_n = NULL;
    uint8_t index_mask = 0;
};

struct usage_usage_def_t {
    uint32_t usage;
    usage_def_t usage_def;
};

struct map_source_t {
    uint32_t usage;
    int32_t scaling = 1000;  // * 1000
    bool tap = false;
    bool hold = false;
    bool is_relative = false;
    bool is_binary = false;
    uint8_t orig_source_port = 0;
    uint8_t layer_mask = 1;
    int32_t* input_state;
    int32_t accumulated_scroll;
    uint64_t last_scroll_timestamp;  // XXX we can make this 32 or less bits
};

struct out_usage_def_t {
    uint8_t* data;
    uint16_t len;
    uint8_t size;
    uint16_t bitpos;
    uint8_t array_count;
    uint32_t array_index;
};

struct reverse_mapping_t {
    uint32_t target;
    uint8_t default_value = 0;  // should be int32_t theoretically, but currently all defaults fit uint8_t
    uint8_t hub_port = 0;
    bool is_relative = false;
    std::vector<out_usage_def_t> our_usages;
    std::vector<map_source_t> sources;
};

struct usage_rle_t {
    uint32_t usage;
    uint32_t count;
};

struct register_ptrs_t {
    int32_t* register_ptr;
    int32_t* state_ptr;
};


struct __attribute__((packed)) mapping_config11_t {
    uint32_t target_usage;
    uint32_t source_usage;
    int32_t scaling;  // * 1000
    uint8_t layer_mask;
    uint8_t flags;
    uint8_t hub_ports = 0;
};

#define QUIRK_FLAG_RELATIVE_MASK 0b10000000
#define QUIRK_FLAG_SIGNED_MASK 0b01000000
#define QUIRK_SIZE_MASK 0b00111111

struct __attribute__((packed)) quirk_t {
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t interface;
    uint8_t report_id;
    uint32_t usage;
    uint16_t bitpos;
    uint8_t size_flags;
};


enum class MutexId : int8_t {
    THEIR_USAGES,
    QUIRKS,
    N
};

struct __attribute__((packed)) monitor_report_item_t {
    uint32_t usage;
    int32_t value;
    uint8_t hub_port;
};

struct __attribute__((packed)) monitor_report_t {
    uint8_t report_id;
    monitor_report_item_t items[7];
};

struct __attribute__((packed)) uint16_val_t {
    uint16_t val;
};

#endif
