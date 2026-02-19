#pragma once
#include "Bus.h"
#include <string>
#include <iostream>

class SPI : public MemoryDevice {
public:
    SPI(const std::string& name);
    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t value) override;

private:
    std::string name_;
    uint8_t reg_;
};