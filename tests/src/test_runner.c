// test_runner.c - Test the dylib before and after stripping
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdint.h>

typedef void (*set_magic_value_t)(uint64_t);
typedef uint64_t (*get_magic_value_t)(void);
typedef uint64_t* (*get_magic_address_t)(void);
typedef void (*increment_magic_t)(void);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <dylib_path>\n", argv[0]);
        return 1;
    }
    
    const char *dylib_path = argv[1];
    
    printf("=== Testing dylib: %s ===\n\n", dylib_path);
    
    // Load the dylib
    void *handle = dlopen(dylib_path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "Failed to load dylib: %s\n", dlerror());
        return 1;
    }
    
    printf("[+] Dylib loaded successfully\n\n");
    
    // Get function pointers
    set_magic_value_t set_magic = (set_magic_value_t)dlsym(handle, "set_magic_value");
    if (!set_magic) printf("[-] set_magic dlsym failed: %s\n", dlerror());
    
    get_magic_value_t get_magic = (get_magic_value_t)dlsym(handle, "get_magic_value");
    if (!get_magic) printf("[-] get_magic dlsym failed: %s\n", dlerror());
    
    get_magic_address_t get_addr = (get_magic_address_t)dlsym(handle, "get_magic_address");
    if (!get_addr) printf("[-] get_addr dlsym failed: %s\n", dlerror());
    
    increment_magic_t increment = (increment_magic_t)dlsym(handle, "increment_magic");
    if (!increment) printf("[-] increment dlsym failed: %s\n", dlerror());
    
    if (!set_magic || !get_magic || !get_addr || !increment) {
        fprintf(stderr, "Failed to resolve symbols\n");
        dlclose(handle);
        return 1;
    }
    
    // Test the functions
    printf("--- Testing Functions ---\n");
    uint64_t value = get_magic();
    printf("Initial value: 0x%llx\n\n", value);
    
    set_magic(0x1337C0DE);
    printf("\n");
    
    increment();
    printf("\n");
    
    uint64_t *addr = get_addr();
    printf("Direct memory access: 0x%llx\n\n", *addr);
    
    // Try to find the symbol directly (will fail if stripped)
    printf("--- Checking Symbol Visibility ---\n");
    void *symbol = dlsym(handle, "__patcher_target_qword");
    if (symbol) {
        printf("[!] Symbol '__patcher_target_qword' is EXPORTED\n");
        printf("    Address: %p\n", symbol);
        printf("    Value: 0x%llx\n", *(uint64_t*)symbol);
    } else {
        printf("[+] Symbol '__patcher_target_qword' is HIDDEN (stripped)\n");
        printf("    But functions still work!\n");
    }
    
    printf("\n=== Test Complete ===\n");
    
    dlclose(handle);
    return 0;
}
