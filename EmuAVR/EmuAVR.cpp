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
#include <filesystem>
#include <thread>
#include <chrono>
#include <fstream>


int main(int argc, char** argv) {

    std::string hexPath;

    if (argc < 2) {
        std::cout << "Usage: EmuAVR <source.c | image.hex>\n";
        std::cout << "   or: EmuAVR --socket-wait\n";
        std::cout << "If source.c is given and avr-gcc is on PATH, the tool will try to compile it.\n";
        return 0;
    }

    std::string input = argv[1];

    // Socket-wait mode: wait for GUI to create a temp file with the path
    if (input == "--socket-wait") {
        std::cout << "[EmuAVR] Starting in socket-wait mode...\n";
        std::cout << "[EmuAVR] Waiting for: EmuAVR_pending.txt\n";

        // Wait up to 30 seconds for the file to appear
        int waitCount = 0;
        while (waitCount < 300) {
            if (std::filesystem::exists("EmuAVR_pending.txt")) {
                // Read the file path
                std::ifstream f("EmuAVR_pending.txt");
                std::getline(f, hexPath);
                f.close();

                if (!hexPath.empty()) {
                    std::cout << "[EmuAVR] Loaded file path: " << hexPath << "\n";
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waitCount++;
        }

        if (hexPath.empty()) {
            std::cout << "[EmuAVR] Timeout waiting for file. Exiting.\n";
            return 1;
        }

        // Clean up temp file
        std::filesystem::remove("EmuAVR_pending.txt");

    }
    else {
        hexPath = input;
    }

    std::error_code ec;

    if (std::filesystem::path(hexPath).extension() == ".c") {

		std::cout << "[EmuAVR] Toolchain not implemented yet. Please compile your .c file to .hex using avr-gcc and provide the .hex file directly.\n";
        return 1;
    }
    else if (std::filesystem::path(hexPath).extension() == ".hex") {
        // Use as-is
    }
    else {
        std::cout << "[EmuAVR] Unsupported input file type. Provide a .c or .hex file.\n";
        return 1;
    }

    if (input == "--socket-wait") {
        std::cout << "[EmuAVR] Waiting for GUI to connect on port 5555...\n";

        // Now this call will work perfectly!
        if (CPU::getJsonServer().waitForConnection(30000)) {
            std::cout << "[EmuAVR] GUI Connected! Starting simulation...\n";
        }
        else {
            std::cout << "[EmuAVR] Timeout waiting for GUI. Running anyway...\n";
        }
    }

    // Setup bus and devices
    Bus bus;
    auto sram = std::make_shared<SRAM>(2048);
    auto eeprom = std::make_shared<EEPROM>(512);
    auto portB = std::make_shared<Port>("B");
    auto uart0 = std::make_shared<UART>("USART0");
    auto spi0 = std::make_shared<SPI>("SPI0");
    auto twi0 = std::make_shared<TWI>("TWI0");

    // Map I/O registers (0x00-0x3F)
    auto dummy_io = std::make_shared<Port>("DUMMY_IO");

    // Map I/O registers (0x00-0x5F)
    bus.map(0x0023, 3, portB);

    // Extended I/O (0x40-0xFF)
    bus.map(0x004C, 4, spi0);         // SPI
    bus.map(0x00B8, 6, twi0);         // TWI
    bus.map(0x00C0, 7, uart0);        // UART 

    // SRAM (0x0100+)
    bus.map(0x0100, 2048, sram);

    // EEPROM at data memory space (0x9000+)
    bus.map(0x9000, 512, eeprom);

    // Load flash from HEX
    Flash flash;
    std::string flashErr;
    if (!flash.loadFromHex(hexPath, flashErr)) {
        std::cout << "[EmuAVR] Failed to load hex: " << flashErr << "\n";
        return 1;
    }

    CPU cpu(bus, flash);
    Clock clk(16000000);

    // Run program
    const uint64_t maxCycles = 10000;
    uint64_t cycles = cpu.run(maxCycles);
    clk.tick((uint32_t)cycles);

    std::cout << "[EmuAVR] Execution finished. Cycles elapsed: "
        << clk.cyclesElapsed() << "\n";

    return 0;
}