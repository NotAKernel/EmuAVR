#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>
#include <iostream>

class MemoryDevice {
public:
    virtual ~MemoryDevice() = default;
    // addr is device-local (i.e., offset from mapping base)
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t value) = 0;
};

class Bus {
public:
    // Map device to an address range [base, base+size)
    void map(uint16_t base, uint16_t size, std::shared_ptr<MemoryDevice> dev);

    // CPU / peripherals use these to access memory/io
    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t value);

private:
    struct Mapping { uint16_t base, size; std::shared_ptr<MemoryDevice> dev; };
    std::vector<Mapping> mappings_;
};