#include <avr/io.h>

void main(void) {
    asm volatile (
        // Load values into registers and write to SRAM addresses
        "ldi r16, 0xAA\n\t"    // Load 0xAA into R16
        "sts 0x0100, r16\n\t"  // Write R16 to SRAM address 0x0100
        
        "ldi r17, 0xBB\n\t"    // Load 0xBB into R17
        "sts 0x0010, r17\n\t"  // Write R17 to SRAM address 0x0010
        
        "ldi r18, 0xCC\n\t"    // Load 0xCC into R18
        "sts 0x1000, r18\n\t"  // Write R18 to SRAM address 0x1000
        
        "ldi r19, 0xDD\n\t"    // Load 0xDD into R19
        "sts 0xFFFF, r19\n\t"  // Write R19 to SRAM address 0xFFFF
        
        "ldi r20, 0xEE\n\t"    // Load 0xEE into R20
        "sts 0x0000, r20\n\t"  // Write R20 to SRAM address 0x0000
        
        "rjmp .-0\n\t"         // Infinite loop
    );
}