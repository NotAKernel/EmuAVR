#include "Bus.h"

void Bus::map(uint16_t base, uint16_t size, std::shared_ptr<MemoryDevice> dev) {
    mappings_.push_back(Mapping{ base, size, dev });
}

uint8_t Bus::read(uint16_t addr) {
    for (auto& m : mappings_) {
        if (addr >= m.base && addr < (uint32_t)m.base + m.size) {
            uint16_t local = addr - m.base;
            return m.dev->read(local);
        }
    }
    std::cout << "[Bus] Read from unmapped address 0x" << std::hex << (uint32_t)addr << std::dec << "\n";
    return 0xFF;
}

void Bus::write(uint16_t addr, uint8_t value) {
    for (auto& m : mappings_) {
        if (addr >= m.base && addr < (uint32_t)m.base + m.size) {
            uint16_t local = addr - m.base;
            m.dev->write(local, value);
            return;
        }
    }
    std::cout << "[Bus] Write to unmapped address 0x" << std::hex << (uint32_t)addr
        << " value 0x" << (uint32_t)value << std::dec << "\n";
}