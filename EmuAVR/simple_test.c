#include <avr/io.h>

int main(void) {
    asm volatile (
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
        "rjmp .-0\n\t"  // Infinite loop
    );
    return 0;
}