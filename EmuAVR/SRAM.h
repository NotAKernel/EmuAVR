#pragma once
#include "Bus.h"
#include <vector>
#include <cstdint>

class SRAM : public MemoryDevice {
public:
    explicit SRAM(size_t sizeBytes);
    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t value) override;

private:
    std::vector<uint8_t> mem_;
};
