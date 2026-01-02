// target.h - Public interface
#ifndef TARGET_H
#define TARGET_H

#include <stdint.h>

void set_magic_value(uint64_t value);
uint64_t get_magic_value(void);
uint64_t* get_magic_address(void);
void increment_magic(void);

#endif
