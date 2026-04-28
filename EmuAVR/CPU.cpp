#include "CPU.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cinttypes>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstring>

// Create a single server instance for the process. Port can be changed here.
static JsonSocketServer g_json_server(5555);

JsonSocketServer& CPU::getJsonServer() {
    return g_json_server;
}

class CPURegs : public MemoryDevice {
    CPU& cpu_;
public:
    explicit CPURegs(CPU& cpu) : cpu_(cpu) {}
    uint8_t read(uint16_t addr) override { return cpu_.readReg((uint8_t)addr); }
    void write(uint16_t addr, uint8_t value) override { cpu_.writeReg((uint8_t)addr, value); }
};

CPU::CPU(Bus& bus, Flash& flash) : bus_(bus), flash_(flash) {
    // Map R0-R31 to SRAM space 0x0000 - 0x001F
    bus_.map(0x0000, 32, std::make_shared<CPURegs>(*this));
    reset();
}

void CPU::reset() {
    PC_ = 0;
    SP_ = 0x08FF;
    R_.fill(0);
    SREG_ = 0;
    std::cout << "[CPU] Reset: PC=" << PC_ << " SP=0x" << std::hex << SP_ << std::dec << "\n";
}

uint8_t CPU::readReg(uint8_t reg) {
    if (reg >= R_.size()) {
        std::cout << "[CPU] readReg invalid reg " << (int)reg << "\n";
        return 0xFF;
    }
    uint8_t v = R_[reg];
    std::ostringstream ss;
    ss << "{\"type\":\"reg\",\"op\":\"read\",\"r\":" << (int)reg << ",\"value\":" << (int)v << ",\"pc\":" << PC_ << "}";
    emitJson(ss.str());
    return v;
}

void CPU::writeReg(uint8_t reg, uint8_t value) {
    if (reg >= R_.size()) {
        std::cout << "[CPU] writeReg invalid reg " << (int)reg << "\n";
        return;
    }
    R_[reg] = value;
    std::ostringstream ss;
    ss << "{\"type\":\"reg\",\"op\":\"write\",\"r\":" << (int)reg << ",\"value\":" << (int)value << ",\"pc\":" << PC_ << "}";
    emitJson(ss.str());
}

void CPU::emitJson(const std::string& json) {
    std::cout << json << std::endl;

    try {
        g_json_server.sendLine(json);
    }
    catch (...) {}

}

uint64_t CPU::run(uint64_t maxCycles) {
    uint64_t executed = 0;
    keep_running_ = true; // Reset the flag when we start

    while (executed < maxCycles && keep_running_) {
        uint32_t c = step();
        if (c == 0) break;
        executed += c;

        // Optional: Check if the GUI is still there
        // If the socket is dead, there's no point in running
        if (!getJsonServer().hasConnection()) {
            std::cout << "[CPU] Socket lost. Stopping...\n";
            break;
        }
    }
    return executed;
}

static inline uint8_t sreg_get_c(uint8_t SREG) { return SREG & 1; }
static inline void sreg_set_flag(uint8_t& SREG, int bit, bool v) {
    if (v) SREG |= (1 << bit);
    else SREG &= ~(1 << bit);
}

static uint16_t read16_from_sram(Bus& bus, uint16_t addr) {
    uint8_t lo = bus.read(addr);
    uint8_t hi = bus.read(addr + 1);
    return (uint16_t)((hi << 8) | lo);
}

static void write16_to_sram(Bus& bus, uint16_t addr, uint16_t val) {
    uint8_t lo = val & 0xFF;
    uint8_t hi = (val >> 8) & 0xFF;
    bus.write(addr, lo);
    bus.write(addr + 1, hi);
}

uint32_t CPU::step() {
    // Fetch word from Flash
    uint16_t instr = flash_.fetchWord(PC_);
    // Emit instruction fetch event
    {
        std::ostringstream ss;
        ss << "{\"type\":\"instr_fetch\",\"pc\":" << PC_
            << ",\"word\":\"0x" << std::hex << std::setw(4) << std::setfill('0') << instr << std::dec << "\"}";
        emitJson(ss.str());
    }

    // Helpers
    auto rd_rr_extract = [&](uint16_t w) -> std::pair<uint8_t, uint8_t> {
        uint8_t rd = static_cast<uint8_t>((w >> 4) & 0x1F);
        uint8_t rr = static_cast<uint8_t>((w & 0x0F) | ((w >> 5) & 0x10));
        return { rd, rr };
        };

    auto io_addr_extract = [&](uint16_t w) -> uint8_t {
        uint8_t low4 = static_cast<uint8_t>(w & 0x0F);
        uint8_t high2 = static_cast<uint8_t>(((w >> 9) & 0x03) << 4);
        return static_cast<uint8_t>(low4 | high2);
        };

    auto push_byte = [&](uint8_t b) {
        bus_.write(SP_, b); // Write first
        std::ostringstream ss;
        ss << "{\"type\":\"mem\",\"op\":\"write\",\"space\":\"sram\",\"addr\":" << SP_ << ",\"value\":" << (int)b << ",\"pc\":" << PC_ << "}";
        emitJson(ss.str());
        SP_ = static_cast<uint16_t>(SP_ - 1); // Post-decrement
        };

    auto pop_byte = [&]() -> uint8_t {
        SP_ = static_cast<uint16_t>(SP_ + 1); // Pre-increment
        uint8_t v = bus_.read(SP_);
        std::ostringstream ss;
        ss << "{\"type\":\"mem\",\"op\":\"read\",\"space\":\"sram\",\"addr\":" << SP_ << ",\"value\":" << (int)v << ",\"pc\":" << PC_ << "}";
        emitJson(ss.str());
        return v;
        };

    uint16_t top4 = instr & 0xF000;
    uint16_t top6 = instr & 0xFC00;

    auto rd_imm = [&](uint16_t w) -> uint8_t { return static_cast<uint8_t>(16 + ((w >> 4) & 0x0F)); };
    auto k_imm = [&](uint16_t w) -> uint8_t { return static_cast<uint8_t>((w & 0x000F) | ((w >> 8) & 0xF0)); };
    auto rel12 = [&](uint16_t w) -> int16_t {
        int16_t k = w & 0x0FFF;
        if (k & 0x0800) k |= 0xF000;
        return k;
        };
    auto rel7 = [&](uint16_t w) -> int8_t {
        int8_t k = (w >> 3) & 0x7F;
        if (k & 0x40) k |= 0x80;
        return k;
        };
    auto disp6 = [&](uint16_t w) -> uint8_t {
        return static_cast<uint8_t>(((w >> 7) & 0x18) | ((w >> 8) & 0x07) | ((w >> 1) & 0x20));
        };
    auto bit3 = [&](uint16_t w) -> uint8_t { return static_cast<uint8_t>(w & 0x07); };

    uint32_t c = 0;

    // 1) Simple control and call/return/branch
    if (instr == 0x0000) { // NOP
        emitJson("{\"type\":\"instruction\",\"pc\":" + std::to_string(PC_) + ",\"mnemonic\":\"NOP\",\"cycles\":1}");
        PC_ += 1; c = 1;
    }

    // CPC (Compare with Carry)
    else if (top6 == 0x0400) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t Rdr = readReg(rd), Rrr = readReg(rr);
        uint8_t carry_in = sreg_get_c(SREG_);

        // Result = Rd - Rr - Carry
        uint16_t res16 = (uint16_t)Rdr - (uint16_t)Rrr - carry_in;
        uint8_t res = static_cast<uint8_t>(res16 & 0xFF);

        bool H = ((Rdr & 0x0F) < ((Rrr & 0x0F) + carry_in));
        bool C = ((int)Rdr < (int)Rrr + (int)carry_in);
        bool N = (res & 0x80) != 0;
        bool V = (((Rdr ^ Rrr) & (Rdr ^ res)) & 0x80) != 0;
        bool S = N ^ V;

        // Note: Z flag is only cleared if result is non-zero. 
        // It is NOT set if result is zero (to allow multi-byte comparison).
        if (res != 0) sreg_set_flag(SREG_, 1, false);

        sreg_set_flag(SREG_, 5, H);
        sreg_set_flag(SREG_, 0, C);
        sreg_set_flag(SREG_, 2, N);
        sreg_set_flag(SREG_, 3, V);
        sreg_set_flag(SREG_, 4, S);

        std::ostringstream ss;
        ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"CPC\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // RJMP
    else if (top4 == 0xC000) {
        int16_t k = instr & 0x0FFF;
        if (k & 0x0800) k |= 0xF000;
        uint16_t oldPC = PC_;
        uint16_t newPC = static_cast<uint16_t>(PC_ + 1 + k);
        if (newPC == oldPC) {
            std::ostringstream ss;
            ss << "{\"type\":\"halt\",\"pc\":" << oldPC << ",\"reason\":\"self_rjmp\"}";
            emitJson(ss.str());
            return 0;
        }
        PC_ = newPC;
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"RJMP\",\"offset\":" << k << ",\"new_pc\":" << PC_ << ",\"cycles\":2}";
        emitJson(ss.str());
        c = 2;
    }

    // RCALL
    else if (top4 == 0xD000) {
        int16_t k = instr & 0x0FFF;
        if (k & 0x0800) k |= 0xF000;
        uint16_t returnAddr = static_cast<uint16_t>(PC_ + 1);
        push_byte(static_cast<uint8_t>((returnAddr >> 8) & 0xFF));
        push_byte(static_cast<uint8_t>(returnAddr & 0xFF));
        uint16_t oldPC = PC_;
        PC_ = static_cast<uint16_t>(PC_ + 1 + k);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"RCALL\",\"offset\":" << k << ",\"new_pc\":" << PC_ << ",\"cycles\":3}";
        emitJson(ss.str());
        c = 3;
    }

    // CALL (best-effort multiword: 0x940E followed by target word)
    else if ((instr & 0xFE0E) == 0x940E) {
        uint16_t target = flash_.fetchWord(PC_ + 1);
        uint16_t returnAddr = static_cast<uint16_t>(PC_ + 2);
        push_byte(static_cast<uint8_t>((returnAddr >> 8) & 0xFF));
        push_byte(static_cast<uint8_t>(returnAddr & 0xFF));
        uint16_t oldPC = PC_;
        PC_ = target;
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"CALL\",\"target\":" << target << ",\"new_pc\":" << PC_ << ",\"cycles\":4}";
        emitJson(ss.str());
        c = 4;
    }

    // JMP (best-effort multiword: 0x940C followed by target)
    else if ((instr & 0xFE0E) == 0x940C) {
        uint16_t target = flash_.fetchWord(PC_ + 1);
        uint16_t oldPC = PC_;
        PC_ = target;
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"JMP\",\"target\":" << target << ",\"new_pc\":" << PC_ << ",\"cycles\":3}";
        emitJson(ss.str());
        c = 3;
    }

    // RET
    else if (instr == 0x9508) {
        uint8_t low = pop_byte();
        uint8_t high = pop_byte();
        uint16_t newPC = static_cast<uint16_t>((high << 8) | low);
        uint16_t oldPC = PC_;
        PC_ = newPC;
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"RET\",\"new_pc\":" << PC_ << ",\"cycles\":4}";
        emitJson(ss.str());
        c = 4;
    }

    // BRxx family (BRBS / BRBC)
    else if ((instr & 0xF800) == 0xF000) {
        // Bit 10 determines if branch when the flag is Cleared (1) or Set (0)
        bool is_brbc = (instr & 0x0400) != 0;

        // Bits 0-2 determine WHICH bit in SREG we are checking
        uint8_t sreg_bit = instr & 0x07;

        // Bits 3-9 hold the 7-bit signed offset
        uint8_t k7 = (instr >> 3) & 0x7F;

        // Sign-extend the 7-bit value to a standard 8-bit signed integer
        int8_t offset = (k7 & 0x40) ? static_cast<int8_t>(k7 | 0x80) : static_cast<int8_t>(k7);

        // Evaluate the condition
        bool bit_set = (SREG_ & (1 << sreg_bit)) != 0;
        bool take = is_brbc ? !bit_set : bit_set;

        uint16_t oldPC = PC_;
        if (take) {
            PC_ = static_cast<uint16_t>(PC_ + 1 + offset);
            std::ostringstream ss;
            ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"" << (is_brbc ? "BRBC" : "BRBS")
                << "\",\"sreg_bit\":" << (int)sreg_bit << ",\"taken\":true,\"offset\":" << (int)offset
                << ",\"new_pc\":" << PC_ << ",\"cycles\":2}";
            emitJson(ss.str());
            c = 2;
        }
        else {
            PC_ += 1;
            std::ostringstream ss;
            ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"" << (is_brbc ? "BRBC" : "BRBS")
                << "\",\"sreg_bit\":" << (int)sreg_bit << ",\"taken\":false,\"offset\":" << (int)offset
                << ",\"new_pc\":" << PC_ << ",\"cycles\":1}";
            emitJson(ss.str());
            c = 1;
        }
    }

    // SBRC / SBRS (Skip if Bit in Register Cleared/Set)
    else if ((instr & 0xFC08) == 0xFC00) {
        uint8_t rr = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t b = static_cast<uint8_t>(instr & 0x07);
        bool is_sbrs = (instr & 0x0200) != 0; // Bit 9 distinguishes SBRS (1) from SBRC (0)
        uint8_t val = readReg(rr);
        bool bit_set = (val & (1 << b)) != 0;

        std::ostringstream ss;
        ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"" << (is_sbrs ? "SBRS" : "SBRC") << "\",\"rr\":\"R" << (int)rr << "\",\"bit\":" << (int)b << "}";
        emitJson(ss.str());

        if (bit_set == is_sbrs) {
            // Skip next instruction
            uint16_t next_instr = flash_.fetchWord(PC_ + 1);
            // 32-bit instructions (CALL, JMP, LDS, STS) take 2 words, so skip 2
            bool is_32bit = ((next_instr & 0xFE0E) == 0x940E) ||
                ((next_instr & 0xFE0E) == 0x940C) ||
                ((next_instr & 0xFE0F) == 0x9000) ||
                ((next_instr & 0xFE0F) == 0x9200);
            PC_ += (is_32bit ? 3 : 2);
            c = (is_32bit ? 3 : 2); // Takes 2 or 3 cycles if skip is taken
        }
        else {
            PC_ += 1;
            c = 1;
        }
    }

    // SWAP
    else if ((instr & 0xFE0F) == 0x9402) {
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t val = readReg(d);
        uint8_t swapped = static_cast<uint8_t>((val << 4) | (val >> 4));
        writeReg(d, swapped);

        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"SWAP\",\"reg\":\"R" << (int)d << "\",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // IN
    else if ((instr & 0xF800) == 0xB000) {
        uint8_t rd = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t A = io_addr_extract(instr);
        uint8_t val = 0;

        // Intercept Core CPU Registers
        if (A == 0x3F) val = SREG_;
        else if (A == 0x3E) val = static_cast<uint8_t>(SP_ >> 8);
        else if (A == 0x3D) val = static_cast<uint8_t>(SP_ & 0xFF);
        else val = bus_.read(0x0020 + A);

        writeReg(rd, val);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"IN\",\"rd\":\"R" << (int)rd << "\",\"io\":" << (int)A << ",\"value\":" << (int)val << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // OUT
    else if ((instr & 0xF800) == 0xB800) {
        uint8_t rr = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t A = io_addr_extract(instr);
        uint8_t val = readReg(rr);

        // Intercept Core CPU Registers
        if (A == 0x3F) SREG_ = val;
        else if (A == 0x3E) SP_ = static_cast<uint16_t>((SP_ & 0x00FF) | (val << 8));
        else if (A == 0x3D) SP_ = static_cast<uint16_t>((SP_ & 0xFF00) | val);
        else bus_.write(0x0020 + A, val);

        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"OUT\",\"rr\":\"R" << (int)rr << "\",\"io\":" << (int)A << ",\"value\":" << (int)val << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // LDI immediate
    else if ((instr & 0xF000) == 0xE000) {
        uint8_t K_low = instr & 0x000F;
        uint8_t K_high = (instr >> 8) & 0x000F;
        uint8_t K = K_low | (K_high << 4);
        uint8_t d = static_cast<uint8_t>(16 + ((instr >> 4) & 0x0F));
        writeReg(d, K);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LDI\",\"dest\":\"R" << (int)d << "\",\"imm\":" << (int)K << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // LDS (Load Direct from SRAM) - 32-bit instruction
    else if ((instr & 0xFE0F) == 0x9000) {
        uint16_t addr = flash_.fetchWord(PC_ + 1);
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t val = bus_.read(addr);
        {
            std::ostringstream ss;
            ss << "{\"type\":\"mem\",\"op\":\"read\",\"space\":\"sram\",\"addr\":" << addr << ",\"value\":" << (int)val << ",\"pc\":" << PC_ << "}";
            emitJson(ss.str());
        }
        writeReg(d, val);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LDS\",\"dest\":\"R" << (int)d << "\",\"addr\":\"0x" << std::hex << std::setw(4) << std::setfill('0') << addr << std::dec << "\",\"value\":" << (int)val << ",\"cycles\":2}";
        emitJson(ss.str());
        PC_ += 2; c = 2;
    }

    // STS (Store Direct to SRAM) - 32-bit instruction
    else if ((instr & 0xFE0F) == 0x9200) {
        uint16_t addr = flash_.fetchWord(PC_ + 1);
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t val = readReg(d);
        bus_.write(addr, val);
        {
            std::ostringstream ss;
            ss << "{\"type\":\"mem\",\"op\":\"write\",\"space\":\"sram\",\"addr\":" << addr << ",\"value\":" << (int)val << ",\"pc\":" << PC_ << "}";
            emitJson(ss.str());
        }
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"STS\",\"reg\":\"R" << (int)d << "\",\"addr\":\"0x" << std::hex << std::setw(4) << std::setfill('0') << addr << std::dec << "\",\"value\":" << (int)val << ",\"cycles\":2}";
        emitJson(ss.str());
        PC_ += 2; c = 2;
    }

    // SUBI
    else if ((instr & 0xF000) == 0x5000) {
        uint8_t K = static_cast<uint8_t>((instr & 0x000F) | ((instr >> 4) & 0xF0));
        uint8_t d = static_cast<uint8_t>(16 + ((instr >> 4) & 0x0F));
        uint8_t Rdr = readReg(d);
        uint16_t res16 = (uint16_t)Rdr - (uint16_t)K;
        uint8_t res = static_cast<uint8_t>(res16 & 0xFF);
        bool H = ((Rdr & 0x0F) < (K & 0x0F));
        bool C = (Rdr < K);
        bool Z = (res == 0);
        bool N = (res & 0x80) != 0;
        bool V = (((Rdr ^ K) & (Rdr ^ res)) & 0x80) != 0;
        bool S = N ^ V;
        writeReg(d, res);
        sreg_set_flag(SREG_, 5, H); sreg_set_flag(SREG_, 0, C); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"SUBI\",\"reg\":\"R" << (int)d << "\",\"imm\":" << (int)K << ",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // ANDI
    else if ((instr & 0xF000) == 0x7000) {
        uint8_t K = static_cast<uint8_t>((instr & 0x000F) | ((instr >> 4) & 0xF0));
        uint8_t d = static_cast<uint8_t>(16 + ((instr >> 4) & 0x0F));
        uint8_t res = static_cast<uint8_t>(readReg(d) & K);
        writeReg(d, res);
        bool Z = (res == 0);
        bool N = (res & 0x80) != 0;
        bool V = false;
        bool S = N ^ V;
        sreg_set_flag(SREG_, 5, false); sreg_set_flag(SREG_, 0, false); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ANDI\",\"reg\":\"R" << (int)d << "\",\"imm\":" << (int)K << ",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // ORI
    else if ((instr & 0xF000) == 0x6000) {
        uint8_t K = static_cast<uint8_t>((instr & 0x000F) | ((instr >> 4) & 0xF0));
        uint8_t d = static_cast<uint8_t>(16 + ((instr >> 4) & 0x0F));
        uint8_t res = static_cast<uint8_t>(readReg(d) | K);
        writeReg(d, res);
        bool Z = (res == 0);
        bool N = (res & 0x80) != 0;
        bool V = false;
        bool S = N ^ V;
        sreg_set_flag(SREG_, 5, false); sreg_set_flag(SREG_, 0, false); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ORI\",\"reg\":\"R" << (int)d << "\",\"imm\":" << (int)K << ",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // ADD
    else if (top6 == 0x0C00) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t Rdr = readReg(rd), Rrr = readReg(rr);
        uint16_t res16 = (uint16_t)Rdr + (uint16_t)Rrr;
        uint8_t res = static_cast<uint8_t>(res16 & 0xFF);
        bool H = (((Rdr & 0x0F) + (Rrr & 0x0F)) & 0x10) != 0;
        bool C = (res16 & 0x100) != 0;
        bool Z = (res == 0); bool N = (res & 0x80) != 0;
        bool V = ((~(Rdr ^ Rrr) & (Rdr ^ res)) & 0x80) != 0; bool S = N ^ V;
        writeReg(rd, res);
        sreg_set_flag(SREG_, 5, H); sreg_set_flag(SREG_, 0, C); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ADD\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // ADC
    else if (top6 == 0x1C00) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t Rdr = readReg(rd), Rrr = readReg(rr);
        uint8_t carry_in = sreg_get_c(SREG_);
        uint16_t res16 = (uint16_t)Rdr + (uint16_t)Rrr + carry_in;
        uint8_t res = static_cast<uint8_t>(res16 & 0xFF);
        bool H = ((((Rdr & 0x0F) + (Rrr & 0x0F) + carry_in) & 0x10) != 0);
        bool C = (res16 & 0x100) != 0; bool Z = (res == 0); bool N = (res & 0x80) != 0;
        bool V = ((~(Rdr ^ Rrr) & (Rdr ^ res)) & 0x80) != 0; bool S = N ^ V;
        writeReg(rd, res);
        sreg_set_flag(SREG_, 5, H); sreg_set_flag(SREG_, 0, C); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ADC\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // SUB
    else if (top6 == 0x1800) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t Rdr = readReg(rd), Rrr = readReg(rr);
        uint16_t res16 = (uint16_t)Rdr - (uint16_t)Rrr;
        uint8_t res = static_cast<uint8_t>(res16 & 0xFF);
        bool H = ((Rdr & 0x0F) < (Rrr & 0x0F)); bool C = (Rdr < Rrr);
        bool Z = (res == 0); bool N = (res & 0x80) != 0;
        bool V = (((Rdr ^ Rrr) & (Rdr ^ res)) & 0x80) != 0; bool S = N ^ V;
        writeReg(rd, res);
        sreg_set_flag(SREG_, 5, H); sreg_set_flag(SREG_, 0, C); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"SUB\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // SBC
    else if (top6 == 0x0800) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t Rdr = readReg(rd), Rrr = readReg(rr);
        uint8_t carry_in = sreg_get_c(SREG_);
        uint16_t res16 = (uint16_t)Rdr - (uint16_t)Rrr - carry_in;
        uint8_t res = static_cast<uint8_t>(res16 & 0xFF);
        bool H = ((Rdr & 0x0F) < ((Rrr & 0x0F) + carry_in));
        bool C = ((int)Rdr < (int)Rrr + (int)carry_in);
        bool Z = (res == 0); bool N = (res & 0x80) != 0;
        bool V = (((Rdr ^ Rrr) & (Rdr ^ res)) & 0x80) != 0; bool S = N ^ V;
        writeReg(rd, res);
        sreg_set_flag(SREG_, 5, H); sreg_set_flag(SREG_, 0, C); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"SBC\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // AND/EOR/OR (register)
    else if (top6 == 0x2000 || top6 == 0x2400 || top6 == 0x2800) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t a = readReg(rd), b = readReg(rr), res = 0;
        std::string m;
        if (top6 == 0x2000) { res = a & b; m = "AND"; }
        else if (top6 == 0x2400) { res = a ^ b; m = "EOR"; }
        else { res = a | b; m = "OR"; }
        bool N = (res & 0x80) != 0; bool V = false; bool S = N ^ V;
        if (res != 0) sreg_set_flag(SREG_, 1, false);
        sreg_set_flag(SREG_, 5, false); sreg_set_flag(SREG_, 0, false);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        writeReg(rd, res);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"" << m << "\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // CP
    else if (top6 == 0x1400) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t Rdr = readReg(rd), Rrr = readReg(rr);
        uint16_t res16 = (uint16_t)Rdr - (uint16_t)Rrr;
        uint8_t res = static_cast<uint8_t>(res16 & 0xFF);
        bool H = ((Rdr & 0x0F) < (Rrr & 0x0F)); bool C = (Rdr < Rrr);
        bool Z = (res == 0); bool N = (res & 0x80) != 0;
        bool V = (((Rdr ^ Rrr) & (Rdr ^ res)) & 0x80) != 0; bool S = N ^ V;
        sreg_set_flag(SREG_, 5, H); sreg_set_flag(SREG_, 0, C); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"CP\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // CPI
    else if ((instr & 0xF000) == 0x3000) {
        uint8_t K = static_cast<uint8_t>((instr & 0x000F) | ((instr >> 4) & 0xF0));
        uint8_t d = static_cast<uint8_t>(16 + ((instr >> 4) & 0x0F));
        uint8_t Rdr = readReg(d);
        uint16_t res16 = (uint16_t)Rdr - (uint16_t)K;
        uint8_t res = static_cast<uint8_t>(res16 & 0xFF);
        bool H = ((Rdr & 0x0F) < (K & 0x0F)); bool C = (Rdr < K);
        bool Z = (res == 0); bool N = (res & 0x80) != 0;
        bool V = (((Rdr ^ K) & (Rdr ^ res)) & 0x80) != 0; bool S = N ^ V;
        sreg_set_flag(SREG_, 5, H); sreg_set_flag(SREG_, 0, C); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"CPI\",\"reg\":\"R" << (int)d << "\",\"imm\":" << (int)K << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // CPSE
    else if (top6 == 0x1000) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t a = readReg(rd), b = readReg(rr);
        std::ostringstream ss;
        ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"CPSE\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\"}";
        emitJson(ss.str());
        if (a == b) {
            PC_ += 2;
            std::ostringstream ss2; ss2 << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"CPSE_SKIP\",\"skipped\":1}";
            emitJson(ss2.str());
            c = 1;
        }
        else {
            PC_ += 1;
            c = 1;
        }
    }

    // PUSH
    else if ((instr & 0xFE0F) == 0x920F) {
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t val = readReg(d);
        push_byte(val);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"PUSH\",\"reg\":" << (int)d << ",\"value\":" << (int)val << ",\"cycles\":2}";
        emitJson(ss.str());
        PC_ += 1; c = 2;
    }

    // POP
    else if ((instr & 0xFE0F) == 0x900F) {
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t v = pop_byte();
        writeReg(d, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"POP\",\"reg\":" << (int)d << ",\"value\":" << (int)v << ",\"cycles\":2}";
        emitJson(ss.str());
        PC_ += 1; c = 2;
    }

    // MOV (Copy Register)
    else if (top6 == 0x2C00) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t val = readReg(rr);
        writeReg(rd, val);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"MOV\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\",\"value\":" << (int)val << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; c = 1;
    }

    // Unsupported/Unknown Instruction
    else {
        std::ostringstream ss;
        ss << "{\"type\":\"unsupported\",\"pc\":" << PC_ << ",\"word\":\"0x" << std::hex << std::setw(4) << std::setfill('0') << instr << std::dec << "\"}";
        emitJson(ss.str());
        return 0;
    }

    // Emit CPU state after each instruction
    if (c > 0) {
        std::ostringstream ss;
        ss << "{\"type\":\"cpu_state\",\"pc\":" << PC_ << ",\"sp\":\"0x" << std::hex
            << std::setw(4) << std::setfill('0') << SP_ << "\",\"sreg\":\"0x"
            << std::setw(2) << std::setfill('0') << (int)SREG_ << std::dec << "\"}";
        emitJson(ss.str());
    }

    return c;
}

Bus& CPU::bus() { return bus_; }