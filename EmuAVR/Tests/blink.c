#include <avr/io.h>

int main(void) {
    // Set PB5 (Bit 5 on PORTB) as an output pin.
    // DDRB is at I/O address 0x04.
    DDRB = 0x20; 

    // 20 state changes = 10 full ON / OFF toggles.
    uint8_t count = 20; 
    uint8_t state = 0;

    while (count != 0) {
        
        // Swap the state variable.
        if (state == 0) {
            state = 0x20; // Prepare to turn LED ON.
        } else {
            state = 0x00; // Prepare to turn LED OFF.
        }
        
        // Write the state to PORTB (I/O address 0x05).
        // This will trigger the [LED STATUS] log in the emulator.
        PORTB = state;

		// d acts as a delay by performing two instructions (NOP and SUBI)
        // d = 50 provides a short, readable block of instructions between blinks.
        uint8_t d = 50; 
        while (d != 0) {
            asm volatile("nop"); // Supported NOP instruction.
            d = d - 1;           // Supported SUBI instruction.
        }

        count = count - 1;       // Decrement total toggle count.
    }

    // Explicit self-rjmp to trigger the emulator's internal halt logic.
    asm volatile("rjmp .");
    
    return 0;
}