#include "Port.h"

Port::Port(const std::string& name) : name_(name) {
    regs_.fill(0);
}

uint8_t Port::read(uint16_t addr) {
    if (addr >= regs_.size()) return 0xFF;
    return regs_[addr]; // Silent read
}

void Port::write(uint16_t addr, uint8_t value) {
    if (addr >= regs_.size()) return;

    uint8_t old_value = regs_[addr];
    regs_[addr] = value;

    if (addr == 2) {
        bool was_on = (old_value > 0);
        bool is_on = (value > 0);

        if (!was_on && is_on) {
            std::cout << "[Port " << name_ << "] ---> [ LED STATUS: ON ]" << std::endl;
        }
        else if (was_on && !is_on) {
            std::cout << "[Port " << name_ << "] ---> [ LED STATUS: OFF ]" << std::endl;
        }
    }
}