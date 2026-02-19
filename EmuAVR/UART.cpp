#include "UART.h"

UART::UART(const std::string & name) : name_(name), dataReg_(0), statusReg_(0) {}

uint8_t UART::read(uint16_t addr) {
    std::cout << "[UART " << name_ << "] Read offset " << addr << "\n";
    if (addr == 0) return dataReg_;
    return statusReg_;
}

void UART::write(uint16_t addr, uint8_t value) {
    std::cout << "[UART " << name_ << "] Write offset " << addr << " <= 0x" << std::hex << (uint32_t)value << std::dec << "\n";
    if (addr == 0) {
        // simulate transmit
        std::cout << "[UART " << name_ << "] TX: '" << (char)value << "' (0x" << std::hex << (uint32_t)value << std::dec << ")\n";
        dataReg_ = value;
    }
    else {
        statusReg_ = value;
    }
}