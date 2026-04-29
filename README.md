# EmuAVR

---

## How to Use

The project primarily consists of:

- A GUI (Graphical User Interface) written in Python
- An Emulator of the AVR ATmega328p, coded in C++
- A JSON web socket, used for communication between the GUI and the Emulator

The project was developed and built in Visual Studio, thus it is best viewed and used within their IDE. To open it, double click 'EmuAVR.slnx' in your file explorer and choose to open with Visual Studio - or open the project from within the IDE itself.

Each time the emulator is modified/updated, the project is built to produce a new .exe file. This file is then moved to the StartUp (/EmuAVR/EmuAVR/StartUp) folder, which is pointed to by the GUI as the location to find the emulator. With this .exe, the GUI can now run the emulator from within itself without the user needing to specify the file location.

> (NOTE: if building the project yourself in either Debug or Release, please ensure your Visual Studio toolset is compatible. If not, install it from the Visual Studio interface. Once successfully built, copy the new .exe (found in /EmuAVR/x64/Debug) into the StartUp folder.)

To use the GUI, a .hex file must be loaded. These are located within the EmuAVR project, under 'Tests'. The main test is the 'sram_write_test.hex', as it showcases CPU register read/write and transferring those values to locations in SRAM. The .c version of the file has further explanations, and is one method of writing programs for this project. The 'blink.hex' test showcases the LED working by toggling it on and off until program halt.

In order to produce a compatible INTEL .hex file, you must have the avr-gcc toolchain installed. With it, you can run a command to parse the .c file into a .elf file, and then into a .hex file. A future version of the project will hopefully have a small version of this toolchain internally packaged (the licence is free), to allow the user to simply pass in the .c file and be returned a .hex file. For now, MinGW is the tool used to convert files.

> (Chained commands to convert valid C file to Intel HEX, assuming you're already in the directory of the file: avr-gcc -mmcu=atmega328p -Os file_name.c -o file_name.elf; avr-objcopy -O ihex file_name.elf file_name.hex)

The .c files are written using standard AVR instructions, such as LDI (Load Immediate), which are directly interpreted by the avr-gcc toolchain. It is also possible to write a traditional .c file, using branching and iteration as usual, but it is harder to account for the covered instructions that way (see CPU.cpp for all covered instructions).

Once the .hex file is loaded, it runs instantly if it manages to connect to the socket created by the .exe. Using the start commands, as seen in EmuAVR.cpp, it is possible to run the .exe in a CMD line interface, but the GUI is the preferred method.

### Detailed Instructions
- Start the GUI by double-clicking the 'EmuGUI.exe' file in the base directory of the project, which includes build files for the Python environment. This means you can run it on a machine that doesn't have Python installed. It may take a bit longer for the GUI to open - DO NOT spam click.
- Once within the GUI, pressing the 'Hex/C' button to load a file will open the File Explorer in the base directory of the project. Navigate EmuAVR -> Tests to find pre-built .hex files along with their .c counterparts.
- The 'Force Stop' button halts the emulator's execution immediately.
- The 'Clear All Memory' button clears all registers and CPU state variables (PC, SP, SREG). Before running the emulator each time, the program clears memory automatically already.
- Use 'Clear Log' to free up the Event Log area.
- You can enter values into the SRAM field next to the 'Set' button, and then click the button to jump to that area in memory. The arrows shift up and down memory incrementally.

And that's it! You may study the given .c files to understand how the emulator works, along with CPU.cpp to see the instruction implementations.

---

## Structure of the Project

The UML_Diagrams folder has a system hierarchy in PlantUML format. In order to view these, simply visit the PlantUML website and paste in the code and run it. The diagrams are also already visible in the first draft of the Final Year Project documentation.

The structure is best viewed in Visual Studio, as it uses filters instead of folders to split up the components logically.

The Core is made up of the CPU and the Clock. Communication contains the Bus, Ports and communication methods. Memory contains Flash, EEPROM and SRAM. It is structured to model the AVR ATmega328p as closely as possible, to facilitate a true emulation.

---

## How it Works

The entire project is based around the AVR ATmega328p emulator (in this case dubbed the 'EmuAVR').

### The Emulator

This constitutes the bulk of the development of the project. It models the chosen microcontroller (uC) in an object-oriented class structure. The CPU, as in the physical world, acts as the heart and brains of the operation. The Bus is a software representation of the physical data-bus found on the uC - it is essential for the communication between every component. A MemoryDevice class, established within the Bus class, acts as a layer of abstraction that lets the memory space in the Bus class store 'devices' in the specified addresses of memory without concerning itself with what the device concretely is. This allows for both memory (SRAM, EEPROM and Flash) to be stored along with the I/O devices indiscriminately.

Not available in the demo, there will be a simple LED available through one of the ports that can be set HIGH or LOW (ON or OFF). Most of the current functionality is found in the implementation of a comprehensive base instruction set, faithful to the mappings, opcodes, masks and status register modifications found in the datasheets for the AVR family.

The emulator accepts a non-argument execution, which builds the project and tests the operation of the socket. It also accepts an argument --socket-mode, which begins the simple handshake necessary for the GUI to receive the JSON-formatted information being emitted by the emulator. The emulator itself can parse, store and execute the instructions provided in the .hex file it is passed. Each instruction is coded to produce a JSON string that is user-readable, providing information about the instruction and the relevant variables. The CPU also produces a status log with each completed instruction. By default, the emulator is set to run for 10'000 cycles - but this can be altered as desired in 'EmuAVR.cpp'.

After execution, the emulator closes the socket and completes - to be executed again with the next .hex file.

### The JSON Socket

This is a web socket opened on port 5555, that acts as an intermediary between the emulator and the GUI. Since the emulator executes immediately upon running it, there has to be a way for the GUI to start the emulator, connect to the socket created by the emulator, and only then should the .hex file run and produce JSON logs of its operation. To solve this, the GUI runs the executable in '--socket-mode' and writes the location of the .hex file into a temporary file. The emulator then confirms this location, parses the .hex file, loads it into memory and runs through each instruction, producing a JSON log with every one. When the .exe is not operational, the socket remains closed.

### The GUI

The GUI was coded in Python using PyQT. It presents the user with an easy to use interface for running their embedded code, along with vision of the internal CPU registers and all of SRAM. The Event Log section is where the JSON socket output is written to, and provides ample insight into the operation of the emulator, along with any errors that occur. It also provides an option to clear memory, which will be done automatically every time a .hex file is run in the finished version, to mimic the physical wiping of a uC. The Program Counter (PC), Stack Pointer (SP) and Status Register (SR) are all readily available also.

---

## Final Thoughts

The EmuAVR project provides a strong foundation for developers to write embedded code and test it in a safe, virtual environment without the strict need for a physical microcontroller.
