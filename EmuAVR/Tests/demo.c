#include <stdint.h>

int main(void) {
    // Declare a data array in SRAM to test memory operations
    volatile uint8_t test_data[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
    };
    
    // Simple operations to test memory access
    uint8_t result = 0;
    for (int i = 0; i < 16; i++) {
        result += test_data[i];
    }
    
    // Infinite loop (self-RJMP acts as halt)
    while(1) {
        asm("nop");
    }
    
    return 0;
}