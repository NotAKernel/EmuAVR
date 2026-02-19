#include "SRAM.h"
#include <iostream>

SRAM::SRAM(size_t sizeBytes) : mem_(sizeBytes, 0) {}

uint8_t SRAM::read(uint16_t addr) {
    if (addr >= mem_.size()) {
        std::cout << "[SRAM] Read out of range 0x" << std::hex << addr << std::dec << "\n";
        return 0xFF;
    }
    uint8_t v = mem_[addr];
    std::cout << "[SRAM] Read 0x" << std::hex << (uint32_t)v << " from 0x" << addr << std::dec << "\n";
    return v;
}

void SRAM::write(uint16_t addr, uint8_t value) {
    if (addr >= mem_.size()) {
        std::cout << "[SRAM] Write out of range 0x" << std::hex << addr << std::dec << "\n";
        return;
    }
    mem_[addr] = value;
    std::cout << "[SRAM] Write 0x" << std::hex << (uint32_t)value << " to 0x" << addr << std::dec << "\n";
}