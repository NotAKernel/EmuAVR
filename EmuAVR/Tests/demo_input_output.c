/* demo_input_output.c
   Inline AVR assembly test for the emulator.
   - Writes DDRB and PORTB via OUT
   - Reads PINB via IN
   - Pulses PORTB bit0 when PINB bit0 is set
*/

asm(
".global main\n"
".text\n"
"main:\n"
"    ldi r16, 0x0F        \n" // DDRB = 0x0F (lower nibble outputs)
"    out 1, r16           \n" // IO addr 1 => DDRB (emulator maps IO at 0x20 + A)
"loop:\n"
"    in r17, 0            \n" // read PINB (IO addr 0)
"    ldi r18, 1           \n" // compare mask 0x01
"    cp r17, r18          \n" // compare PINB, #1
"    brne no_press        \n" // if not pressed, continue loop
"    ldi r16, 0x01        \n" // set PORTB bit0
"    out 2, r16           \n" // IO addr 2 => PORTB (emulator maps PORT at base+2)
"    rcall delay          \n"
"    ldi r16, 0x00        \n" // clear PORTB bit0
"    out 2, r16           \n"
"    rcall delay          \n"
"    rjmp loop            \n"
"no_press:\n"
"    rjmp loop            \n"
"\n"
"delay:\n"
"    ldi r20, 50          \n" // simple software delay (tunable)
"del_loop:\n"
"    dec r20              \n"
"    brne del_loop        \n"
"    ret                  \n"
);