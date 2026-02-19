#pragma once
#include "Bus.h"
#include <vector>
#include <cstdint>

class EEPROM : public MemoryDevice {
public:
    explicit EEPROM(size_t sizeBytes = 1024);
    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t value) override;

private:
    std::vector<uint8_t> mem_;
};