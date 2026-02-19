#include "Port.h"

Port::Port(const std::string & name) : name_(name) {
    regs_.fill(0);
}

uint8_t Port::read(uint16_t addr) {
    if (addr >= regs_.size()) {
        std::cout << "[Port " << name_ << "] Read out of range offset " << addr << "\n";
        return 0xFF;
    }
    uint8_t v = regs_[addr];
    std::cout << "[Port " << name_ << "] Read offset " << addr << " => 0x" << std::hex << (uint32_t)v << std::dec << "\n";
    return v;
}

void Port::write(uint16_t addr, uint8_t value) {
    if (addr >= regs_.size()) {
        std::cout << "[Port " << name_ << "] Write out of range offset " << addr << "\n";
        return;
    }
    regs_[addr] = value;
    std::cout << "[Port " << name_ << "] Write offset " << addr << " <= 0x" << std::hex << (uint32_t)value << std::dec << "\n";
}