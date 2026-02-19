#include <iostream>
#include <memory>
#include <string>
#include "Bus.h"
#include "SRAM.h"
#include "Flash.h"
#include "EEPROM.h"
#include "Port.h"
#include "UART.h"
#include "SPI.h"
#include "TWI.h"
#include "CPU.h"
#include "Clock.h"
#include "Toolchain.h"
#include <filesystem>

int main(int argc, char** argv) {

    if (argc < 2) {
        std::cout << "Usage: EmuAVR <source.c | image.hex>\n";
        std::cout << "If source.c is given and avr-gcc is on PATH, the tool will try to compile it.\n";
        return 0;
    }

    std::string input = argv[1];
    std::string hexPath;
    std::error_code ec;

    if (std::filesystem::path(input).extension() == ".c") {
        // try to compile to hex
        hexPath = "out_emavr.hex";
        std::string err;
        bool ok = Toolchain::compileToHex(input, hexPath, err);
        if (!ok) {
            std::cout << "[EmuAVR] Toolchain failed: " << err << "\n";
            std::cout << "[EmuAVR] If you don't have avr-gcc installed, you can pass a .hex file instead.\n";
            return 1;
        }
    } else if (std::filesystem::path(input).extension() == ".hex") {
        hexPath = input;
    } else {
        std::cout << "[EmuAVR] Unsupported input file type. Provide a .c or .hex file.\n";
        return 1;
    }

    // Setup bus and devices
    Bus bus;
    auto sram = std::make_shared<SRAM>(2048);      // 2KB SRAM
    auto eeprom = std::make_shared<EEPROM>(512);
    auto portB = std::make_shared<Port>("B");
    auto uart0 = std::make_shared<UART>("USART0");
    auto spi0 = std::make_shared<SPI>("SPI0");
    auto twi0 = std::make_shared<TWI>("TWI0");

    // Map devices into address space (simple example mapping)
    bus.map(0x0100, 2048, sram);    // SRAM
    bus.map(0x9000, 512, eeprom);   // EEPROM
    bus.map(0x0020, 4, portB);      // PORTB range
    bus.map(0x00C0, 4, uart0);
    bus.map(0x0040, 4, spi0);
    bus.map(0x0050, 4, twi0);

    // Load flash from HEX
    Flash flash;
    std::string flashErr;
    if (!flash.loadFromHex(hexPath, flashErr)) {
        std::cout << "[EmuAVR] Failed to load hex: " << flashErr << "\n";
        return 1;
    }

    CPU cpu(bus, flash);
    Clock clk(16000000);

    // Run: limit cycles to avoid runaway loops in this early stage
    const uint64_t maxCycles = 100000;
    uint64_t cycles = cpu.run(maxCycles);
    clk.tick((uint32_t)cycles);

    std::cout << "[EmuAVR] Execution finished. Cycles elapsed: " << clk.cyclesElapsed() << "\n";
    return 0;
}