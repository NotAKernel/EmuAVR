#pragma once
#include <cstdint>

class Clock {
public:
    Clock(uint32_t hz = 16000000);
    void tick(uint32_t cycles);
    uint64_t cyclesElapsed() const;

private:
    uint64_t cycles_;
    uint32_t freqHz_;
};