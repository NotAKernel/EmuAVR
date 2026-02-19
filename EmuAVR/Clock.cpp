#include "Clock.h"

Clock::Clock(uint32_t hz) : cycles_(0), freqHz_(hz) {}

void Clock::tick(uint32_t cycles) {
    cycles_ += cycles;
}

uint64_t Clock::cyclesElapsed() const { return cycles_; }