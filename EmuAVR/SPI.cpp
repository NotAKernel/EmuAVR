#include "SPI.h"

SPI::SPI(const std::string & name) : name_(name), reg_(0) {}

uint8_t SPI::read(uint16_t addr) {
    std::cout << "[SPI " << name_ << "] Read offset " << addr << "\n";
    return reg_;
}

void SPI::write(uint16_t addr, uint8_t value) {
    reg_ = value;
    std::cout << "[SPI " << name_ << "] Write offset " << addr << " <= 0x" << std::hex << (uint32_t)value << std::dec << "\n";
}