#pragma once
#include "Bus.h"
#include <cstdint>
#include <iostream>

class UART : public MemoryDevice {
public:
    UART(const std::string& name);
    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t value) override;

private:
    std::string name_;
    uint8_t dataReg_;
    uint8_t statusReg_;
};