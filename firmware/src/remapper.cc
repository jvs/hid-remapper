#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "descriptor_parser.h"
#include "globals.h"
#include "our_descriptor.h"
#include "platform.h"
#include "remapper.h"

#define MAX_REPORT_SIZE 64

const uint8_t MAPPING_FLAG_TAP = 1 << 1;
const uint8_t MAPPING_FLAG_HOLD = 1 << 2;

const uint8_t V_RESOLUTION_BITMASK = (1 << 0);
const uint8_t H_RESOLUTION_BITMASK = (1 << 2);
const uint32_t V_SCROLL_USAGE = 0x00010038;
const uint32_t H_SCROLL_USAGE = 0x000C0238;

const uint8_t NLAYERS = 4;
const uint32_t LAYERS_USAGE_PAGE = 0xFFF10000;
const uint32_t REGISTER_USAGE_PAGE = 0xFFF50000;

const uint32_t ROLLOVER_USAGE = 0x00070001;

const uint16_t STACK_SIZE = 16;

const uint8_t resolution_multiplier_masks[] = {
    V_RESOLUTION_BITMASK,
    H_RESOLUTION_BITMASK,
};

std::vector<reverse_mapping_t> reverse_mapping;
std::vector<reverse_mapping_t> reverse_mapping_layers;

std::unordered_map<uint8_t, std::unordered_map<uint32_t, usage_def_t>> our_usages;  // report_id -> usage -> usage_def
std::unordered_map<uint32_t, usage_def_t> our_usages_flat;

std::unordered_map<uint16_t, std::unordered_map<uint8_t, std::vector<usage_usage_def_t>>> their_used_usages;  // dev_addr+interface -> report_id -> (usage, usage_def) vector
std::unordered_map<uint16_t, std::unordered_map<uint8_t, std::vector<int32_t*>>> array_range_usages;          // dev_addr+interface -> report_id -> input_state ptr vector
std::unordered_map<uint16_t, std::unordered_map<uint8_t, std::vector<usage_def_t>>> rollover_usages;          // dev_addr+interface -> report_id -> usage_def vector


std::vector<usage_usage_def_t> our_array_range_usages;

// report_id -> ...
uint8_t* reports[MAX_INPUT_REPORT_ID + 1];
uint8_t* prev_reports[MAX_INPUT_REPORT_ID + 1];
uint8_t* report_masks_relative[MAX_INPUT_REPORT_ID + 1];
uint8_t* report_masks_absolute[MAX_INPUT_REPORT_ID + 1];
uint16_t report_sizes[MAX_INPUT_REPORT_ID + 1];

#define OR_BUFSIZE 8
uint8_t outgoing_reports[OR_BUFSIZE][MAX_REPORT_SIZE + 1];
uint8_t or_head = 0;
uint8_t or_tail = 0;
uint8_t or_items = 0;

std::vector<uint8_t> report_ids;

#define MAX_INPUT_STATES 1024
#define PREV_STATE_OFFSET MAX_INPUT_STATES

int32_t input_state[MAX_INPUT_STATES * 2];
std::unordered_map<uint64_t, int32_t*> usage_state_ptr;  // usage -> input_state pointer
uint32_t used_state_slots = 0;

std::unordered_map<uint32_t, int32_t> accumulated;  // usage -> relative movement, * 1000
uint8_t layer_state_mask = 1;

std::vector<int32_t*> relative_usages;  // input_state pointers


uint32_t reports_received;
uint32_t reports_sent;
uint32_t processing_time;


std::unordered_map<uint32_t, int32_t> monitor_input_state;
uint8_t monitor_usages_queued = 0;
monitor_report_t monitor_report[2] = { { .report_id = REPORT_ID_MONITOR }, { .report_id = REPORT_ID_MONITOR } };
uint8_t monitor_report_idx = 0;

#define NREGISTERS 32
int32_t registers[NREGISTERS] = { 0 };
std::vector<register_ptrs_t> register_ptrs;
uint8_t port_register = 0;

uint64_t frame_counter = 0;

#define HUB_PORT_NONE 255
#define NPORTS 15
std::unordered_map<uint8_t, uint8_t> hub_ports;  // dev_addr -> hub_port
uint16_t active_ports_mask = 0;

inline int32_t handle_scroll(map_source_t& map_source, uint32_t target_usage, int32_t movement, uint64_t now) {
    // movement is always non-zero
    int32_t ret = 0;
    if (resolution_multiplier &
        resolution_multiplier_masks[target_usage == H_SCROLL_USAGE]) {  // hi-res
        ret = movement;
    } else {  // lo-res
        if ((map_source.accumulated_scroll != 0) &&
            (now - map_source.last_scroll_timestamp > partial_scroll_timeout)) {
            map_source.accumulated_scroll = 0;
        }
        map_source.last_scroll_timestamp = now;
        map_source.accumulated_scroll += movement;
        int ticks = map_source.accumulated_scroll / (1000 * RESOLUTION_MULTIPLIER);
        map_source.accumulated_scroll -= ticks * (1000 * RESOLUTION_MULTIPLIER);
        ret = ticks * 1000;
    }
    return ret;
}

inline int8_t get_bit(const uint8_t* data, int len, uint16_t bitpos) {
    int byte_no = bitpos / 8;
    int bit_no = bitpos % 8;
    if (byte_no < len) {
        return (data[byte_no] & 1 << bit_no) ? 1 : 0;
    }
    return 0;
}

inline uint32_t get_bits(const uint8_t* data, int len, uint16_t bitpos, uint8_t size) {
    uint32_t value = 0;
    for (int i = 0; i < size; i++) {
        value |= get_bit(data, len, bitpos + i) << i;
    }
    return value;
}

inline void put_bit(uint8_t* data, int len, uint16_t bitpos, uint8_t value) {
    int byte_no = bitpos / 8;
    int bit_no = bitpos % 8;
    if (byte_no < len) {
        data[byte_no] &= ~(1 << bit_no);
        data[byte_no] |= (value & 1) << bit_no;
    }
}

inline void put_bits(uint8_t* data, int len, uint16_t bitpos, uint8_t size, uint32_t value) {
    for (int i = 0; i < size; i++) {
        put_bit(data, len, bitpos + i, (value >> i) & 1);
    }
}

bool needs_to_be_sent(uint8_t report_id) {
    uint8_t* report = reports[report_id];
    uint8_t* prev_report = prev_reports[report_id];
    uint8_t* relative = report_masks_relative[report_id];
    uint8_t* absolute = report_masks_absolute[report_id];

    for (int i = 0; i < report_sizes[report_id]; i++) {
        if ((report[i] & relative[i]) || ((report[i] & absolute[i]) != (prev_report[i] & absolute[i]))) {
            return true;
        }
    }
    return false;
}




bool assign_state_slot(uint32_t usage, uint8_t hub_port, bool raw) {
    uint64_t key = (raw ? ((uint64_t) 1 << 40) : 0) | ((uint64_t) hub_port << 32) | usage;
    if (usage_state_ptr.count(key) == 0) {
        if (used_state_slots >= MAX_INPUT_STATES) {
            printf("out of input_state slots!");
            return false;
        }

        usage_state_ptr[key] = input_state + used_state_slots++;
    }
    return true;
}

inline int32_t* get_state_ptr(uint32_t usage, uint8_t hub_port, bool assign_if_absent = false, bool raw = false) {
    uint64_t key = (raw ? ((uint64_t) 1 << 40) : 0) | ((uint64_t) hub_port << 32) | usage;
    auto search = usage_state_ptr.find(key);
    if (search != usage_state_ptr.end()) {
        return search->second;
    }

    if (assign_if_absent) {
        if (assign_state_slot(usage, hub_port, raw)) {
            their_descriptor_updated = true;
            return usage_state_ptr[key];  // it's zero, but maybe someone wants to write to it
        }
    }

    return NULL;
}


void set_mapping_from_config() {
    std::unordered_map<uint64_t, std::vector<map_source_t>> reverse_mapping_map;  // hub_port+target -> sources list
    std::unordered_map<uint32_t, uint8_t> mapped_on_layers;  // usage -> layer mask


    reverse_mapping.clear();
    reverse_mapping_layers.clear();
    used_state_slots = 0;
    usage_state_ptr.clear();
    register_ptrs.clear();
    memset(input_state, 0, sizeof(input_state));


    if (unmapped_passthrough_layer_mask) {
        for (auto const& [usage, usage_def] : our_usages_flat) {
            uint8_t unmapped_layers = unmapped_passthrough_layer_mask & ~mapped_on_layers[usage];
            if (unmapped_layers) {
                if (assign_state_slot(usage, 0, false)) {
                    reverse_mapping_map[usage].push_back((map_source_t){
                        .usage = usage,
                        .layer_mask = unmapped_layers,
                        .input_state = get_state_ptr(usage, 0),
                    });
                }
            }
        }

        for (auto const& array_usage : our_array_range_usages) {
            for (uint32_t usage = array_usage.usage; usage <= array_usage.usage_def.usage_maximum; usage++) {
                uint8_t unmapped_layers = unmapped_passthrough_layer_mask & ~mapped_on_layers[usage];
                if (unmapped_layers) {
                    if (assign_state_slot(usage, 0, false)) {
                        reverse_mapping_map[usage].push_back((map_source_t){
                            .usage = usage,
                            .layer_mask = unmapped_layers,
                            .input_state = get_state_ptr(usage, 0),
                        });
                    }
                }
            }
        }

        for (auto const& [report_id, usage_map] : their_usages[OUR_OUT_INTERFACE]) {
            for (auto const& [usage, usage_def] : usage_map) {
                uint8_t unmapped_layers = unmapped_passthrough_layer_mask & ~mapped_on_layers[usage];
                if (unmapped_layers) {
                    if (assign_state_slot(usage, 0, false)) {
                        reverse_mapping_map[usage].push_back((map_source_t){
                            .usage = usage,
                            .layer_mask = unmapped_layers,
                            .input_state = get_state_ptr(usage, 0),
                        });
                    }
                }
            }
        }
    }

    for (auto const& [hub_port_target, sources] : reverse_mapping_map) {
        uint8_t hub_port = (hub_port_target >> 32) & 0xFF;
        uint32_t target = hub_port_target & 0xFFFFFFFF;
        reverse_mapping_t rev_map = {
            .target = target,
            .hub_port = hub_port,
            .sources = sources,
        };
        if (our_descriptor->default_value != nullptr) {
            rev_map.default_value = our_descriptor->default_value(target);
            // This helps in cases where nothing is plugged in to provide state for a source
            // and a default of zero is not good, but the proper way to solve this would be
            // to not execute mappings with unplugged sources.
            for (auto const& source : sources) {
                if (!source.tap && !source.hold && (source.scaling == 1000)) {
                    *(source.input_state) = rev_map.default_value;
                }
            }
        }
        if ((target == (DIGIPOT_USAGE_PAGE | 0)) ||
            (target == (DIGIPOT_USAGE_PAGE | 1)) ||
            (target == (DIGIPOT_USAGE_PAGE | 2)) ||
            (target == (DIGIPOT_USAGE_PAGE | 3))) {
            rev_map.default_value = 128;
            for (auto const& source : sources) {
                if (!source.tap && !source.hold && (source.scaling == 1000)) {
                    *(source.input_state) = 128;
                }
            }
        }
        if ((target & 0xFFFF0000) == GPIO_USAGE_PAGE) {
            rev_map.our_usages.push_back((out_usage_def_t){
                .data = gpio_out_state,
                .len = sizeof(gpio_out_state),
                .size = 1,
                .bitpos = (uint16_t) (target & 0xFFFF),
            });
        } else if ((target & 0xFFFF0000) == DIGIPOT_USAGE_PAGE) {
            rev_map.our_usages.push_back((out_usage_def_t){
                .data = (uint8_t*) digipot_state,
                .len = sizeof(digipot_state),
                .size = 9,
                .bitpos = (uint16_t) ((target & 0xFFFF) * 16),
            });
        } else if ((target & 0xFFFF0000) == REGISTER_USAGE_PAGE) {
            rev_map.our_usages.push_back((out_usage_def_t){
                .data = (uint8_t*) registers,
                .len = sizeof(registers),
                .size = 8 * sizeof(registers[0]),
                .bitpos = (uint16_t) (((target & 0xFFFF) - 1) * 8 * sizeof(registers[0])),
            });
        } else {
            bool handled = false;
            for (auto const& array_usage : our_array_range_usages) {
                if ((target >= array_usage.usage) && (target <= array_usage.usage_def.usage_maximum)) {
                    rev_map.our_usages.push_back((out_usage_def_t){
                        .data = reports[array_usage.usage_def.report_id],
                        .len = report_sizes[array_usage.usage_def.report_id],
                        .size = array_usage.usage_def.size,
                        .bitpos = array_usage.usage_def.bitpos,
                        .array_count = array_usage.usage_def.count,
                        .array_index = array_usage.usage_def.logical_minimum + target - array_usage.usage,
                    });
                    handled = true;
                    break;
                }
            }
            if (!handled) {
                auto search = our_usages_flat.find(target);
                if (search != our_usages_flat.end()) {
                    const usage_def_t& our_usage = search->second;
                    rev_map.our_usages.push_back((out_usage_def_t){
                        .data = reports[our_usage.report_id],
                        .len = report_sizes[our_usage.report_id],
                        .size = our_usage.size,
                        .bitpos = our_usage.bitpos,
                    });
                    rev_map.is_relative = our_usage.is_relative;
                }
            }
        }
        if ((target & 0xFFFF0000) == LAYERS_USAGE_PAGE) {
            reverse_mapping_layers.push_back(rev_map);
        } else {
            reverse_mapping.push_back(rev_map);
        }
    }

    set_gpio_inout_masks(0, 0);
    update_their_descriptor_derivates();
}

bool differ_on_absolute(const uint8_t* report1, const uint8_t* report2, uint8_t report_id) {
    uint8_t* absolute = report_masks_absolute[report_id];

    for (int i = 0; i < report_sizes[report_id]; i++) {
        if ((report1[i] & absolute[i]) != (report2[i] & absolute[i])) {
            return true;
        }
    }

    return false;
}

void aggregate_relative(uint8_t* prev_report, const uint8_t* report, uint8_t report_id) {
    for (auto const& [usage, usage_def] : our_usages[report_id]) {
        if (usage_def.is_relative) {
            int32_t val1 = get_bits(report, report_sizes[report_id], usage_def.bitpos, usage_def.size);
            if (usage_def.logical_minimum < 0) {
                if (val1 & (1 << (usage_def.size - 1))) {
                    val1 |= 0xFFFFFFFF << usage_def.size;
                }
            }
            if (val1) {
                int32_t val2 = get_bits(prev_report, report_sizes[report_id], usage_def.bitpos, usage_def.size);
                if (usage_def.logical_minimum < 0) {
                    if (val2 & (1 << (usage_def.size - 1))) {
                        val2 |= 0xFFFFFFFF << usage_def.size;
                    }
                }

                put_bits(prev_report, report_sizes[report_id], usage_def.bitpos, usage_def.size, val1 + val2);
            }
        }
    }
}


void process_mapping(bool auto_repeat) {
    if (suspended) {
        return;
    }

    uint64_t now = get_time();
    frame_counter++;


    layer_state_mask = 1;


    for (auto const& reg_ptr : register_ptrs) {
        *reg_ptr.state_ptr = *reg_ptr.register_ptr;
    }


    memcpy(input_state + PREV_STATE_OFFSET, input_state, used_state_slots * sizeof(input_state[0]));
    digipot_state[0] = 128;
    digipot_state[1] = 128;
    digipot_state[2] = 128;
    digipot_state[3] = 128;
    digipot_state[4] = 0;
    digipot_state[5] = 0;

    for (auto& rev_map : reverse_mapping) {
        uint32_t target = rev_map.target;
        bool register_target = (target & 0xFFFF0000) == REGISTER_USAGE_PAGE;
        if (rev_map.is_relative) {
            for (auto& map_source : rev_map.sources) {
                if ((map_source.orig_source_port != 0) &&
                    !(active_ports_mask & (1 << map_source.orig_source_port))) {
                    continue;
                }
                int32_t value = 0;
                if (auto_repeat || map_source.is_relative) {
                    if (layer_state_mask & map_source.layer_mask) {
                        value = *map_source.input_state;
                        if (map_source.is_binary) {
                            value = !!value;
                        }
                        value *= map_source.scaling;
                        if ((map_source.usage & 0xFFFF0000) == REGISTER_USAGE_PAGE) {
                            value /= 1000;
                        }
                    }
                }
                if (value != 0) {
                    if (target == V_SCROLL_USAGE || target == H_SCROLL_USAGE) {
                        accumulated[target] += handle_scroll(map_source, target, value * RESOLUTION_MULTIPLIER, now);
                    } else {
                        accumulated[target] += value;
                    }
                }
            }
        } else {  // our_usage is absolute
            int32_t value = rev_map.default_value;
            for (auto const& map_source : rev_map.sources) {
                if ((map_source.orig_source_port != 0) &&
                    !(active_ports_mask & (1 << map_source.orig_source_port))) {
                    continue;
                }
                if ((layer_state_mask & map_source.layer_mask)) {
                    if (map_source.tap || map_source.hold) {
                        value += 1 * map_source.scaling / 1000 - rev_map.default_value;
                    }
                    if (!map_source.tap && !map_source.hold) {
                        if (map_source.is_relative && !register_target) {
                            if (*map_source.input_state * map_source.scaling > 0) {
                                value += 1;
                            }
                        } else {
                            if ((*map_source.input_state != 0) || (rev_map.default_value != 0)) {
                                int32_t candidate = *map_source.input_state;
                                if (map_source.is_binary) {
                                    candidate = !!candidate;
                                }
                                if ((candidate != 0) || !map_source.is_binary) {
                                    candidate = (int64_t) candidate * map_source.scaling / 1000;
                                    if ((map_source.usage & 0xFFFF0000) == REGISTER_USAGE_PAGE) {
                                        candidate /= 1000;
                                    }
                                    if (candidate != rev_map.default_value) {
                                        value += candidate - rev_map.default_value;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // we don't currently have any absolute usages that can be negative
            if ((value < 0) && !register_target) {
                value = 0;
            }
            if (register_target) {
                value *= 1000;
            }
            if ((value != rev_map.default_value) || register_target) {
                for (auto const& out_usage_def : rev_map.our_usages) {
                    if (out_usage_def.array_count == 0) {
                        uint32_t effective_value = value;
                        if ((out_usage_def.size < 32) && (effective_value > ((1 << out_usage_def.size) - 1))) {
                            effective_value = (1 << out_usage_def.size) - 1;
                        }
                        put_bits(out_usage_def.data, out_usage_def.len, out_usage_def.bitpos, out_usage_def.size, effective_value);
                    } else {  // array range
                        for (int i = 0; i < out_usage_def.array_count; i++) {
                            int32_t existing_val = get_bits(out_usage_def.data, out_usage_def.len, out_usage_def.bitpos + i * out_usage_def.size, out_usage_def.size);
                            // theoretically zero could be a valid index, but let's ignore that for now
                            if (existing_val == 0) {
                                put_bits(out_usage_def.data, out_usage_def.len, out_usage_def.bitpos + i * out_usage_def.size, out_usage_def.size, out_usage_def.array_index);
                                break;
                            }
                        }
                        // we don't do RollOver
                    }
                }
            }
        }
    }


    for (auto state : relative_usages) {
        *state = 0;
    }

    for (auto& [usage, accumulated_val] : accumulated) {
        if (accumulated_val == 0) {
            continue;
        }
        usage_def_t& our_usage = our_usages_flat[usage];
        // XXX I don't think this is necessary now that we only do process_mapping once per frame (existing_val is always zero)
        int32_t existing_val = get_bits((uint8_t*) reports[our_usage.report_id], report_sizes[our_usage.report_id], our_usage.bitpos, our_usage.size);
        if (our_usage.logical_minimum < 0) {
            if (existing_val & (1 << (our_usage.size - 1))) {
                existing_val |= 0xFFFFFFFF << our_usage.size;
            }
        }
        int32_t truncated = accumulated_val / 1000;
        accumulated_val -= truncated * 1000;
        if (truncated != 0) {
            put_bits((uint8_t*) reports[our_usage.report_id], report_sizes[our_usage.report_id], our_usage.bitpos, our_usage.size, existing_val + truncated);
        }
    }

    for (unsigned int i = 0; i < report_ids.size(); i++) {  // XXX what order should we go in? maybe keyboard first so that mappings to ctrl-left click work as expected?
        uint8_t report_id = report_ids[i];
        if (our_descriptor->sanitize_report != nullptr) {
            our_descriptor->sanitize_report(report_id, reports[report_id], report_sizes[report_id]);
        }
        if (needs_to_be_sent(report_id)) {
            if (or_items == OR_BUFSIZE) {
                printf("overflow!\n");
                break;
            }
            uint8_t prev = (or_tail + OR_BUFSIZE - 1) % OR_BUFSIZE;
            if ((or_items > 0) &&
                (outgoing_reports[prev][0] == report_id) &&
                !differ_on_absolute(outgoing_reports[prev] + 1, reports[report_id], report_id)) {
                aggregate_relative(outgoing_reports[prev] + 1, reports[report_id], report_id);
            } else {
                outgoing_reports[or_tail][0] = report_id;
                memcpy(outgoing_reports[or_tail] + 1, reports[report_id], report_sizes[report_id]);
                memcpy(prev_reports[report_id], reports[report_id], report_sizes[report_id]);
                or_tail = (or_tail + 1) % OR_BUFSIZE;
                or_items++;
            }
        }
        if (our_descriptor->clear_report != nullptr) {
            our_descriptor->clear_report(reports[report_id], report_id, report_sizes[report_id]);
        } else {
            memset(reports[report_id], 0, report_sizes[report_id]);
        }
    }

    for (auto const [interface_report_id, report] : out_reports) {
        // XXX we assume everything is absolute
        if (memcmp(report, prev_out_reports[interface_report_id], out_report_sizes[interface_report_id])) {
            queue_out_report(interface_report_id >> 16, interface_report_id & 0xFF, report, out_report_sizes[interface_report_id]);
            memcpy(prev_out_reports[interface_report_id], report, out_report_sizes[interface_report_id]);
        }
        memset(report, 0, out_report_sizes[interface_report_id]);
    }

    processing_time += get_time() - now;
}

bool send_report(send_report_t do_send_report) {
    if (suspended || (or_items == 0)) {
        return false;
    }

    uint8_t report_id = outgoing_reports[or_head][0];

    bool sent = false;
    if (our_descriptor == &our_descriptors[our_descriptor_number]) {
        sent = do_send_report(0, outgoing_reports[or_head], report_sizes[report_id] + 1);
    }

    // XXX even if not sent?
    or_head = (or_head + 1) % OR_BUFSIZE;
    or_items--;

    reports_sent++;

    return sent;
}

bool send_monitor_report(send_report_t do_send_report) {
    if ((monitor_usages_queued == 0) || suspended) {
        return false;
    }

    bool sent = do_send_report(1, (uint8_t*) &monitor_report[monitor_report_idx], sizeof(monitor_report_t));

    monitor_report_idx = (monitor_report_idx + 1) % 2;
    memset(&(monitor_report[monitor_report_idx].items), 0, sizeof(monitor_report[0].items));
    monitor_usages_queued = 0;

    return sent;
}

void monitor_usage(uint32_t usage, int32_t value, uint8_t hub_port) {
    if (monitor_usages_queued == sizeof(monitor_report[0].items) / sizeof(monitor_report[0].items[0])) {
        return;
    }
    monitor_report[monitor_report_idx].items[monitor_usages_queued++] = {
        .usage = usage,
        .value = value,
        .hub_port = hub_port,
    };
}

inline void read_input(const uint8_t* report, int len, uint32_t source_usage, const usage_def_t& their_usage, uint8_t interface_idx) {
    int32_t value = 0;
    if (their_usage.is_array) {
        for (unsigned int i = 0; i < their_usage.count; i++) {
            uint32_t bits = get_bits(report, len, their_usage.bitpos + i * their_usage.size, their_usage.size);
            if (((their_usage.index_mask == 0) && (bits == their_usage.index)) ||
                (their_usage.index_mask & (1 << bits))) {
                value = 1;
                break;
            }
        }
    } else {
        value = get_bits(report, len, their_usage.bitpos, their_usage.size);
        if ((their_usage.logical_minimum < 0) || (their_usage.logical_maximum < 0)) {
            if (value & (1 << (their_usage.size - 1))) {
                value |= 0xFFFFFFFF << their_usage.size;
            }
        }
    }

    if (their_usage.is_relative) {
        if (their_usage.input_state_0 != NULL) {
            *(their_usage.input_state_0) += value;
        }
        if (their_usage.input_state_n != NULL) {
            *(their_usage.input_state_n) = value;  // XXX does it need to be += ?
        }
    } else {
        int32_t scaled_value;
        if (their_usage.should_be_scaled) {
            scaled_value = (int64_t) (value - their_usage.logical_minimum) * 255 / (their_usage.logical_maximum - their_usage.logical_minimum);  // XXX
        } else {
            scaled_value = value;
        }
        if (their_usage.input_state_0 != NULL) {
            if ((their_usage.size == 1) || their_usage.is_array) {
                if (value) {
                    *(their_usage.input_state_0) |= 1 << interface_idx;
                } else {
                    *(their_usage.input_state_0) &= ~(1 << interface_idx);
                }
            } else {
                *(their_usage.input_state_0) = scaled_value;
            }
        }
        if (their_usage.input_state_n != NULL) {
            *(their_usage.input_state_n) = scaled_value;
        }
    }
}

inline void read_input_range(const uint8_t* report, int len, uint32_t source_usage, const usage_def_t& their_usage, uint8_t interface_idx, uint8_t hub_port) {
    // is_array and !is_relative is implied
    for (unsigned int i = 0; i < their_usage.count; i++) {
        uint32_t bits = get_bits(report, len, their_usage.bitpos + i * their_usage.size, their_usage.size);
        // XXX consider negative indexes
        if ((bits >= their_usage.logical_minimum) &&
            (bits <= their_usage.logical_minimum + their_usage.usage_maximum - source_usage)) {
            uint32_t actual_usage = source_usage + bits - their_usage.logical_minimum;
            int32_t* state_ptr_0 = get_state_ptr(actual_usage, 0);
            if (state_ptr_0 != NULL) {
                *state_ptr_0 |= 1 << interface_idx;
            }
            if (hub_port != HUB_PORT_NONE) {
                int32_t* state_ptr_n = get_state_ptr(actual_usage, hub_port);
                if (state_ptr_n != NULL) {
                    *state_ptr_n = 1 << interface_idx;  // set the bit because in do_handle_received_report we clear it not knowing if it's "0" or "n"
                }
            }
        }
    }
}

inline void monitor_read_input(const uint8_t* report, int len, uint32_t source_usage, const usage_def_t& their_usage, uint8_t interface_idx, uint8_t hub_port) {
    int32_t value = 0;
    if (their_usage.is_array) {
        for (unsigned int i = 0; i < their_usage.count; i++) {
            uint32_t bits = get_bits(report, len, their_usage.bitpos + i * their_usage.size, their_usage.size);
            if (((their_usage.index_mask == 0) && (bits == their_usage.index)) ||
                (their_usage.index_mask & (1 << bits))) {
                value = 1;
                break;
            }
        }
    } else {
        value = get_bits(report, len, their_usage.bitpos, their_usage.size);
        if ((their_usage.logical_minimum < 0) || (their_usage.logical_maximum < 0)) {
            if (value & (1 << (their_usage.size - 1))) {
                value |= 0xFFFFFFFF << their_usage.size;
            }
        }
    }

    if (their_usage.is_relative) {
        if (value != 0) {
            monitor_usage(source_usage, value, hub_port);
        }
    } else {
        if ((their_usage.size == 1) || their_usage.is_array) {
            if (value != (1 & (monitor_input_state[source_usage] >> interface_idx))) {
                monitor_usage(source_usage, value, hub_port);
            }
            if (value) {
                monitor_input_state[source_usage] |= 1 << interface_idx;
            } else {
                monitor_input_state[source_usage] &= ~(1 << interface_idx);
            }
        } else {
            if (value != monitor_input_state[source_usage]) {
                monitor_usage(source_usage, value, hub_port);
            }
            monitor_input_state[source_usage] = value;
        }
    }
}

inline void monitor_read_input_range(const uint8_t* report, int len, uint32_t source_usage, const usage_def_t& their_usage, uint8_t interface_idx, uint8_t hub_port) {
    // is_array and !is_relative is implied
    for (unsigned int i = 0; i < their_usage.count; i++) {
        uint32_t bits = get_bits(report, len, their_usage.bitpos + i * their_usage.size, their_usage.size);
        // XXX consider negative indexes
        if ((bits >= their_usage.logical_minimum) &&
            (bits <= their_usage.logical_minimum + their_usage.usage_maximum - source_usage)) {
            uint32_t actual_usage = source_usage + bits - their_usage.logical_minimum;
            // for array range inputs, "key-up" events (value=0) don't show up in the monitor
            if (monitor_enabled && ((actual_usage & 0xFFFF) != 0)) {
                monitor_usage(actual_usage, 1, hub_port);
            }
        }
    }
}

void handle_received_report(const uint8_t* report, int len, uint16_t interface, uint8_t external_report_id) {
    if (our_descriptor->handle_received_report != nullptr) {
        our_descriptor->handle_received_report(report, len, interface, external_report_id);
    }
}

static inline bool is_rollover(const uint8_t* report, int len, uint16_t interface, uint8_t report_id) {
    for (auto const& usage_def : rollover_usages[interface][report_id]) {
        if (usage_def.is_array) {
            for (unsigned int i = 0; i < usage_def.count; i++) {
                if (get_bits(report, len, usage_def.bitpos + i * usage_def.size, usage_def.size) == usage_def.index) {
                    return true;
                }
            }
        } else {
            if (get_bits(report, len, usage_def.bitpos, usage_def.size) != 0) {
                return true;
            }
        }
    }

    return false;
}

void do_handle_received_report(const uint8_t* report, int len, uint16_t interface, uint8_t external_report_id) {
    if (len == 0) {
        return;
    }

    reports_received++;

    my_mutex_enter(MutexId::THEIR_USAGES);

    uint8_t report_id = 0;
    if (has_report_id_theirs[interface]) {
        if (external_report_id != 0) {
            report_id = external_report_id;
        } else {
            report_id = report[0];
            report++;
            len--;
        }
    }

    uint8_t interface_idx = interface_index[interface];
    uint8_t hub_port = hub_ports[interface >> 8];

    if (!is_rollover(report, len, interface, report_id)) {
        for (int32_t* state_ptr : array_range_usages[interface][report_id]) {
            *state_ptr &= ~(1 << interface_idx);
        }

        for (auto const& their : their_used_usages[interface][report_id]) {
            if (their.usage_def.usage_maximum == 0) {
                read_input(report, len, their.usage, their.usage_def, interface_idx);
            } else {
                read_input_range(report, len, their.usage, their.usage_def, interface_idx, hub_port);
            }
        }
    }

    if (monitor_enabled) {
        for (auto const& [their_usage, their_usage_def] : their_usages[interface][report_id]) {
            if (their_usage_def.usage_maximum == 0) {
                monitor_read_input(report, len, their_usage, their_usage_def, interface_idx, hub_port);
            } else {
                monitor_read_input_range(report, len, their_usage, their_usage_def, interface_idx, hub_port);
            }
        }
    }

    my_mutex_exit(MutexId::THEIR_USAGES);
}

void set_input_state(uint32_t usage, int32_t state_raw, int32_t state_scaled, uint8_t hub_port) {
    int32_t* state_ptr = get_state_ptr(usage, hub_port, false, true);
    if (state_ptr != NULL) {
        *state_ptr = state_raw;
    }
    state_ptr = get_state_ptr(usage, hub_port, false, false);
    if (state_ptr != NULL) {
        *state_ptr = state_scaled;
    }
}

void rlencode(const std::set<uint64_t>& usage_ranges, std::vector<usage_rle_t>& output) {
    uint32_t start_usage = 0;
    uint32_t count = 0;
    for (auto const& range : usage_ranges) {
        uint32_t usage_minimum = range >> 32;
        uint32_t usage_maximum = range & 0xFFFFFFFF;

        if (start_usage == 0) {
            start_usage = usage_minimum;
            count = 1 + usage_maximum - usage_minimum;
            continue;
        }
        if (usage_minimum <= start_usage + count) {
            if (usage_maximum < start_usage + count) {
                continue;
            }
            count += 1 + usage_maximum - (start_usage + count);
        } else {
            output.push_back({ .usage = start_usage, .count = count });
            start_usage = usage_minimum;
            count = 1 + usage_maximum - usage_minimum;
        }
    }
    if (start_usage != 0) {
        output.push_back({ .usage = start_usage, .count = count });
    }
}

bool should_scale_input(const usage_def_t& their_usage) {
    if ((their_usage.size == 1) ||
        (their_usage.is_relative) ||
        ((their_usage.logical_minimum == 0) && (their_usage.logical_maximum == 255)) ||
        ((their_usage.logical_minimum == 1) && (their_usage.logical_maximum == 255)) ||
        ((their_usage.logical_minimum == 0) && (their_usage.logical_maximum == 1)) ||
        (their_usage.logical_minimum == their_usage.logical_maximum)) {
        return false;
    }
    return true;
}

void update_their_descriptor_derivates() {
    std::unordered_set<int32_t*> relative_usage_set;
    std::unordered_set<int32_t*> binary_usage_set;
    std::set<uint64_t> their_usage_ranges_set;

    relative_usages.clear();
    their_used_usages.clear();
    array_range_usages.clear();
    rollover_usages.clear();

    for (auto& [interface, report_id_usage_map] : their_usages) {
        uint8_t hub_port = hub_ports[interface >> 8];
        for (auto& [report_id, usage_map] : report_id_usage_map) {
            for (auto [usage, usage_def] : usage_map) {
                usage_def.should_be_scaled = should_scale_input(usage_def);
                if (usage_def.usage_maximum == 0) {
                    int32_t* state_ptr_0 = get_state_ptr(usage, 0);
                    int32_t* state_ptr_n = get_state_ptr(usage, hub_port);
                    int32_t* state_ptr_raw_0 = get_state_ptr(usage, 0, false, true);
                    int32_t* state_ptr_raw_n = get_state_ptr(usage, hub_port, false, true);
                    their_usage_ranges_set.insert(((uint64_t) usage << 32) | usage);
                    if (usage_def.is_relative) {
                        if (state_ptr_0 != NULL) {
                            relative_usage_set.insert(state_ptr_0);
                        }
                        if (state_ptr_n != NULL) {
                            relative_usage_set.insert(state_ptr_n);
                        }
                        if (state_ptr_raw_0 != NULL) {
                            relative_usage_set.insert(state_ptr_raw_0);
                        }
                        if (state_ptr_raw_n != NULL) {
                            relative_usage_set.insert(state_ptr_raw_n);
                        }
                    }
                    if ((usage_def.size == 1) || usage_def.is_array) {
                        if (state_ptr_0 != NULL) {
                            binary_usage_set.insert(state_ptr_0);
                        }
                        if (state_ptr_n != NULL) {
                            binary_usage_set.insert(state_ptr_n);
                        }
                        if (state_ptr_raw_0 != NULL) {
                            binary_usage_set.insert(state_ptr_raw_0);
                        }
                        if (state_ptr_raw_n != NULL) {
                            binary_usage_set.insert(state_ptr_raw_n);
                        }
                    }
                    if ((state_ptr_0 != NULL) || (state_ptr_n != NULL)) {
                        usage_def.input_state_0 = state_ptr_0;
                        usage_def.input_state_n = state_ptr_n;
                        their_used_usages[interface][report_id].push_back((usage_usage_def_t){
                            .usage = usage,
                            .usage_def = usage_def,
                        });
                    }
                    if ((state_ptr_raw_0 != NULL) || (state_ptr_raw_n != NULL)) {
                        usage_def.input_state_0 = state_ptr_raw_0;
                        usage_def.input_state_n = state_ptr_raw_n;
                        usage_def.should_be_scaled = false;
                        their_used_usages[interface][report_id].push_back((usage_usage_def_t){
                            .usage = usage,
                            .usage_def = usage_def,
                        });
                    }
                    if (usage == ROLLOVER_USAGE) {
                        rollover_usages[interface][report_id].push_back(usage_def);
                    }
                } else {  // usage_maximum != 0, array range usage
                    their_usage_ranges_set.insert(((uint64_t) usage << 32) | usage_def.usage_maximum);
                    bool any_used = false;
                    for (uint32_t actual_usage = usage; actual_usage <= usage_def.usage_maximum; actual_usage++) {
                        int32_t* state_ptr_0 = get_state_ptr(actual_usage, 0);
                        int32_t* state_ptr_n = get_state_ptr(actual_usage, hub_port);
                        if (state_ptr_0 != NULL) {
                            any_used = true;
                            array_range_usages[interface][report_id].push_back(state_ptr_0);
                            binary_usage_set.insert(state_ptr_0);
                        }
                        if (state_ptr_n != NULL) {
                            any_used = true;
                            array_range_usages[interface][report_id].push_back(state_ptr_n);
                            binary_usage_set.insert(state_ptr_n);
                        }
                        if (actual_usage == ROLLOVER_USAGE) {
                            rollover_usages[interface][report_id].push_back((usage_def_t){
                                .size = usage_def.size,
                                .bitpos = usage_def.bitpos,
                                .is_array = true,
                                .index = usage_def.logical_minimum + actual_usage - usage,
                                .count = usage_def.count,
                            });
                        }
                    }
                    if (any_used) {
                        their_used_usages[interface][report_id].push_back((usage_usage_def_t){
                            .usage = usage,
                            .usage_def = usage_def,
                        });
                    }
                }
            }
        }
    }

    for (int32_t* ptr : relative_usage_set) {
        relative_usages.push_back(ptr);
    }

    their_usages_rle.clear();
    rlencode(their_usage_ranges_set, their_usages_rle);

    for (auto& rev_map : reverse_mapping) {
        for (auto& map_source : rev_map.sources) {
            map_source.is_relative = relative_usage_set.count(map_source.input_state) > 0;
            map_source.is_binary = (binary_usage_set.count(map_source.input_state) > 0) ||
                                   ((map_source.usage & 0xFFFF0000) == GPIO_USAGE_PAGE);
        }
        auto search = their_out_usages_flat.find(rev_map.target);
        if (search != their_out_usages_flat.end()) {
            rev_map.our_usages.clear();
            for (auto dev_addr_int_rep_id : search->second) {
                uint8_t hub_port = hub_ports[dev_addr_int_rep_id >> 24];
                if ((rev_map.hub_port == 0) || (rev_map.hub_port == hub_port)) {
                    auto const& our_usage2 = their_out_usages[dev_addr_int_rep_id >> 16][dev_addr_int_rep_id & 0xFFFF][rev_map.target];
                    rev_map.our_usages.push_back((out_usage_def_t){
                        .data = out_reports[dev_addr_int_rep_id],
                        .len = out_report_sizes[dev_addr_int_rep_id],
                        .size = our_usage2.size,
                        .bitpos = our_usage2.bitpos,
                    });
                }
            }
        }
    }

    // Some keyboards have the same usage as both non-array and array inputs.
    // By reading the non-array ones first we get the right result regardless of which they actually use.
    for (auto& [interface, report_id_usages_map] : their_used_usages) {
        for (auto& [report_id, usages_vector] : report_id_usages_map) {
            std::sort(usages_vector.begin(), usages_vector.end(),
                [](const usage_usage_def_t& a, const usage_usage_def_t& b) {
                    return (a.usage_def.is_array < b.usage_def.is_array);
                });
        }
    }
}

void parse_our_descriptor() {
    std::unordered_map<uint8_t, std::unordered_map<uint32_t, usage_def_t>> our_feature_usages;

    our_usages.clear();
    our_usages_rle.clear();
    their_usages.erase(OUR_OUT_INTERFACE);
    has_report_id_theirs.erase(OUR_OUT_INTERFACE);
    our_usages_flat.clear();
    our_array_range_usages.clear();

    for (unsigned int i = 0; i < report_ids.size(); i++) {
        uint8_t report_id = report_ids[i];
        delete[] reports[report_id];
        delete[] prev_reports[report_id];
        delete[] report_masks_relative[report_id];
        delete[] report_masks_absolute[report_id];
    }

    report_ids.clear();
    memset(report_sizes, 0, sizeof(report_sizes));
    memset(reports, 0, sizeof(reports));
    memset(prev_reports, 0, sizeof(prev_reports));
    memset(report_masks_relative, 0, sizeof(report_masks_relative));
    memset(report_masks_absolute, 0, sizeof(report_masks_absolute));

    auto report_sizes_map = parse_descriptor(
        our_usages,
        their_usages[OUR_OUT_INTERFACE],
        our_feature_usages,
        has_report_id_theirs[OUR_OUT_INTERFACE],
        boot_protocol_keyboard ? boot_kb_report_descriptor : our_descriptor->descriptor,
        boot_protocol_keyboard ? boot_kb_report_descriptor_length : our_descriptor->descriptor_length);

    for (auto const& [report_id, size] : report_sizes_map[ReportType::INPUT]) {
        report_sizes[report_id] = size;
        reports[report_id] = new uint8_t[size];
        memset(reports[report_id], 0, size);
        prev_reports[report_id] = new uint8_t[size];
        memset(prev_reports[report_id], 0, size);
        report_masks_relative[report_id] = new uint8_t[size];
        memset(report_masks_relative[report_id], 0, size);
        report_masks_absolute[report_id] = new uint8_t[size];
        memset(report_masks_absolute[report_id], 0, size);

        report_ids.push_back(report_id);
    }

    std::set<uint64_t> our_usage_ranges_set;
    for (auto const& [report_id, usage_map] : our_usages) {
        for (auto const& [usage, usage_def] : usage_map) {
            if (usage_def.usage_maximum == 0) {
                our_usages_flat[usage] = usage_def;
                our_usage_ranges_set.insert(((uint64_t) usage << 32) | (usage_def.usage_maximum ? usage_def.usage_maximum : usage));

                if (usage_def.is_relative) {
                    put_bits(report_masks_relative[report_id], report_sizes[report_id], usage_def.bitpos, usage_def.size, 0xFFFFFFFF);
                } else {
                    put_bits(report_masks_absolute[report_id], report_sizes[report_id], usage_def.bitpos, usage_def.size, 0xFFFFFFFF);
                }
            } else {  // array range
                our_array_range_usages.push_back((usage_usage_def_t){
                    .usage = usage,
                    .usage_def = usage_def,
                });
                our_usage_ranges_set.insert(((uint64_t) usage << 32) | usage_def.usage_maximum);
                put_bits(report_masks_absolute[report_id], report_sizes[report_id], usage_def.bitpos, usage_def.size * usage_def.count, 0xFFFFFFFF);
            }
        }
    }

    rlencode(our_usage_ranges_set, our_usages_rle);
}

void print_stats() {
    printf("%lu %lu %lu\n", reports_received, reports_sent, processing_time);
    reports_received = 0;
    reports_sent = 0;
    processing_time = 0;
}

void reset_state() {
    memset(registers, 0, sizeof(registers));
    accumulated.clear();
    layer_state_mask = 1;
    frame_counter = 0;
}

void set_monitor_enabled(bool enabled) {
    if (monitor_enabled != enabled) {
        monitor_input_state.clear();
        monitor_enabled = enabled;
    }
}

void device_connected_callback(uint16_t interface, uint16_t vid, uint16_t pid, uint8_t hub_port) {
    hub_ports[interface >> 8] = (hub_port != 0) ? hub_port : HUB_PORT_NONE;
    if (hub_port != 0) {
        active_ports_mask |= 1 << hub_port;
    }
    if (our_descriptor->device_connected != nullptr) {
        our_descriptor->device_connected(interface, vid, pid);
    }
}

void device_disconnected_callback(uint8_t dev_addr) {
    if (our_descriptor->device_disconnected != nullptr) {
        our_descriptor->device_disconnected(dev_addr);
    }
    clear_descriptor_data(dev_addr);
    uint8_t hub_port = hub_ports[dev_addr];
    if ((hub_port != 0) && (hub_port != HUB_PORT_NONE)) {
        active_ports_mask &= ~(1 << hub_port);
    }
    hub_ports.erase(dev_addr);
}

uint16_t handle_get_report0(uint8_t report_id, uint8_t* buffer, uint16_t reqlen) {
    if (our_descriptor->handle_get_report != nullptr) {
        return our_descriptor->handle_get_report(report_id, buffer, reqlen);
    }
    return 0;
}

void handle_set_report0(uint8_t report_id, const uint8_t* buffer, uint16_t reqlen) {
    if (our_descriptor->handle_set_report != nullptr) {
        our_descriptor->handle_set_report(report_id, buffer, reqlen);
    }
}

bool set_report0_synchronous(uint8_t report_id) {
    if (our_descriptor->set_report_synchronous != nullptr) {
        return our_descriptor->set_report_synchronous(report_id);
    }
    return false;
}

void handle_get_report_response(uint16_t interface, uint8_t report_id, uint8_t* report, uint16_t len) {
    if (our_descriptor->handle_get_report_response != nullptr) {
        our_descriptor->handle_get_report_response(interface, report_id, report, len);
    }
}

void handle_set_report_complete(uint16_t interface, uint8_t report_id) {
    if (our_descriptor->handle_set_report_complete != nullptr) {
        our_descriptor->handle_set_report_complete(interface, report_id);
    }
}
