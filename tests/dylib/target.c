// target.c - The dylib with our hidden variable
#include <stdio.h>
#include <stdint.h>

// The variable we want to find then hide
// Export it initially so we can locate it
__attribute__((used, visibility("default")))
uint64_t __patcher_target_qword = 0xDEADBEEFCAFEBABE;

// Some regular functions that use this variable
__attribute__((visibility("default")))
void set_magic_value(uint64_t value) {
    __patcher_target_qword = value;
    printf("[dylib] Magic value set to: 0x%llx\n", __patcher_target_qword);
}

__attribute__((visibility("default")))
uint64_t get_magic_value(void) {
    printf("[dylib] Magic value is: 0x%llx\n", __patcher_target_qword);
    return __patcher_target_qword;
}

__attribute__((visibility("default")))
uint64_t* get_magic_address(void) {
    printf("[dylib] Magic variable address: %p\n", (void*)&__patcher_target_qword);
    return &__patcher_target_qword;
}

__attribute__((visibility("default")))
void increment_magic(void) {
    __patcher_target_qword++;
    printf("[dylib] Magic value incremented to: 0x%llx\n", __patcher_target_qword);
}
