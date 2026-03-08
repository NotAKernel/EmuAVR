#include <avr/io.h>

int main(void) {
    asm volatile (
        // Load values into R16-R31 (ldi works here)
        "ldi r16, 16\n\t"
        "ldi r17, 17\n\t"
        "ldi r18, 18\n\t"
        "ldi r19, 19\n\t"
        "ldi r20, 20\n\t"
        "ldi r21, 21\n\t"
        "ldi r22, 22\n\t"
        "ldi r23, 23\n\t"
        "ldi r24, 24\n\t"
        "ldi r25, 25\n\t"
        "ldi r26, 26\n\t"
        "ldi r27, 27\n\t"
        "ldi r28, 28\n\t"
        "ldi r29, 29\n\t"
        "ldi r30, 30\n\t"
        "ldi r31, 31\n\t"
        // Load values into R0-R15 (need intermediate register)
        "ldi r16, 0\n\t"   "mov r0, r16\n\t"
        "ldi r16, 1\n\t"   "mov r1, r16\n\t"
        "ldi r16, 2\n\t"   "mov r2, r16\n\t"
        "ldi r16, 3\n\t"   "mov r3, r16\n\t"
        "ldi r16, 4\n\t"   "mov r4, r16\n\t"
        "ldi r16, 5\n\t"   "mov r5, r16\n\t"
        "ldi r16, 6\n\t"   "mov r6, r16\n\t"
        "ldi r16, 7\n\t"   "mov r7, r16\n\t"
        "ldi r16, 8\n\t"   "mov r8, r16\n\t"
        "ldi r16, 9\n\t"   "mov r9, r16\n\t"
        "ldi r16, 10\n\t"  "mov r10, r16\n\t"
        "ldi r16, 11\n\t"  "mov r11, r16\n\t"
        "ldi r16, 12\n\t"  "mov r12, r16\n\t"
        "ldi r16, 13\n\t"  "mov r13, r16\n\t"
        "ldi r16, 14\n\t"  "mov r14, r16\n\t"
        "ldi r16, 15\n\t"  "mov r15, r16\n\t"
    );
    
    while (1) {
        asm volatile("nop");
    }
    
    return 0;
}