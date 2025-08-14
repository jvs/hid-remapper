#ifndef JVS_H
#define JVS_H

#include <stdint.h>

void jvs_init();
bool jvs_handle_input(uint32_t usage, int32_t state_raw);
void jvs_process_mapping(bool auto_repeat);

#endif
