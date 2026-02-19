#pragma once
#include "Bus.h"
#include <array>
#include <cstdint>
#include <iostream>

// A simple GPIO/PORT peripheral that exposes a small register block:
// offset 0 = PINx (read), offset 1 = DDRx (read/write), offset 2 = PORTx (read/write)
class Port : public MemoryDevice {
public:
    Port(const std::string& name);

    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t value) override;

private:
    std::string name_;
    std::array<uint8_t, 3> regs_; // PIN, DDR, PORT
};