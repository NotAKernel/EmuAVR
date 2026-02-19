#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include "Bus.h"
#include "Flash.h"

class CPU {
public:
    explicit CPU(Bus& bus, Flash& flash);

    // Reset CPU to initial state
    void reset();

    // Read/write general purpose registers (R0..R31).
    uint8_t readReg(uint8_t reg);
    void writeReg(uint8_t reg, uint8_t value);

    // Run until halt or cycle limit. Emits JSON events to stdout.
    // Returns total cycles executed.
    uint64_t run(uint64_t maxCycles);

    // Single step: executes one instruction, returns cycles consumed (or 0 on halt)
    uint32_t step();

    // Direct access to bus for demo
    Bus& bus();

private:
    Bus& bus_;
    Flash& flash_;
    uint16_t PC_; // word address
    uint16_t SP_;
    std::array<uint8_t, 32> R_;
    uint8_t SREG_;

    // Helper to emit JSON event line
    void emitJson(const std::string& json);
};