#include "TWI.h"

TWI::TWI(const std::string & name) : name_(name), reg_(0) {}

uint8_t TWI::read(uint16_t addr) {
    std::cout << "[TWI " << name_ << "] Read offset " << addr << "\n";
    return reg_;
}

void TWI::write(uint16_t addr, uint8_t value) {
    reg_ = value;
    std::cout << "[TWI " << name_ << "] Write offset " << addr << " <= 0x" << std::hex << (uint32_t)value << std::dec << "\n";
}