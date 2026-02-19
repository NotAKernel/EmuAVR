#include "EEPROM.h"
#include <iostream>

EEPROM::EEPROM(size_t sizeBytes) : mem_(sizeBytes, 0xFF) {}

uint8_t EEPROM::read(uint16_t addr) {
    if (addr >= mem_.size()) {
        std::cout << "[EEPROM] Read out of range 0x" << std::hex << addr << std::dec << "\n";
        return 0xFF;
    }
    uint8_t v = mem_[addr];
    std::cout << "[EEPROM] Read 0x" << std::hex << (uint32_t)v << " from 0x" << addr << std::dec << "\n";
    return v;
}

void EEPROM::write(uint16_t addr, uint8_t value) {
    if (addr >= mem_.size()) {
        std::cout << "[EEPROM] Write out of range 0x" << std::hex << addr << std::dec << "\n";
        return;
    }
    mem_[addr] = value;
    std::cout << "[EEPROM] Write 0x" << std::hex << (uint32_t)value << " to 0x" << addr << std::dec << "\n";
}