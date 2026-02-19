#include "CPU.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cinttypes>
#include <cstdint>

CPU::CPU(Bus & bus, Flash & flash) : bus_(bus), flash_(flash) {
    reset();
}

void CPU::reset() {
    PC_ = 0;
    // Stack pointer set to top of example SRAM mapping (0x08FF used in earlier scaffold)
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
}

uint64_t CPU::run(uint64_t maxCycles) {
    uint64_t executed = 0;
    while (executed < maxCycles) {
        uint32_t c = step();
        if (c == 0) break; // halted or unsupported opcode
        executed += c;
    }
    return executed;
}

static inline uint8_t sreg_get_c(uint8_t SREG) { return SREG & 1; }
static inline void sreg_set_flag(uint8_t& SREG, int bit, bool v) {
    if (v) SREG |= (1 << bit);
    else SREG &= ~(1 << bit);
}

// Helper: read 16-bit little-endian word from SRAM (used for LDS)
static uint16_t read16_from_sram(Bus& bus, uint16_t addr) {
    uint8_t lo = bus.read(addr);
    uint8_t hi = bus.read(addr + 1);
    return (uint16_t)((hi << 8) | lo);
}

// Helper: write 16-bit little-endian word to SRAM
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

    // Small helpers (kept to match project's style)
    auto rd_rr_extract = [&](uint16_t w) -> std::pair<uint8_t, uint8_t> {
        uint8_t rd = static_cast<uint8_t>((w >> 4) & 0x1F);
        uint8_t rr = static_cast<uint8_t>((w & 0x0F) | ((w >> 5) & 0x10));
        return { rd, rr };
        };

    auto io_addr_extract = [&](uint16_t w) -> uint8_t {
        // Best-effort extraction of the A field for IN/OUT encodings.
        uint8_t low4 = static_cast<uint8_t>(w & 0x0F);
        uint8_t high2 = static_cast<uint8_t>(((w >> 5) & 0x03) << 4); // place into bits 4..5
        return static_cast<uint8_t>(low4 | high2);
        };

    auto push_byte = [&](uint8_t b) {
        SP_ = static_cast<uint16_t>(SP_ - 1);
        bus_.write(SP_, b);
        std::ostringstream ss;
        ss << "{\"type\":\"mem\",\"op\":\"write\",\"space\":\"sram\",\"addr\":" << SP_ << ",\"value\":" << (int)b << ",\"pc\":" << PC_ << "}";
        emitJson(ss.str());
        };

    auto pop_byte = [&]() -> uint8_t {
        uint8_t v = bus_.read(SP_);
        std::ostringstream ss;
        ss << "{\"type\":\"mem\",\"op\":\"read\",\"space\":\"sram\",\"addr\":" << SP_ << ",\"value\":" << (int)v << ",\"pc\":" << PC_ << "}";
        emitJson(ss.str());
        SP_ = static_cast<uint16_t>(SP_ + 1);
        return v;
        };

    // Quick decoders / masks used below
    uint16_t top4 = instr & 0xF000;
    uint16_t top6 = instr & 0xFC00;

    // Common small extractors used for immediates/displacements
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

    // 1) Simple control and call/return/branch
    if (instr == 0x0000) { // NOP
        emitJson("{\"type\":\"instruction\",\"pc\":" + std::to_string(PC_) + ",\"mnemonic\":\"NOP\",\"cycles\":1}");
        PC_ += 1; return 1;
    }

    // RJMP
    if (top4 == 0xC000) {
        int16_t k = instr & 0x0FFF;
        if (k & 0x0800) k |= 0xF000;
        uint16_t oldPC = PC_;
        PC_ = static_cast<uint16_t>(PC_ + 1 + k);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"RJMP\",\"offset\":" << k << ",\"new_pc\":" << PC_ << ",\"cycles\":2}";
        emitJson(ss.str());
        return 2;
    }

    // RCALL
    if (top4 == 0xD000) {
        int16_t k = instr & 0x0FFF;
        if (k & 0x0800) k |= 0xF000;
        uint16_t returnAddr = static_cast<uint16_t>(PC_ + 1);
        push_byte(static_cast<uint8_t>((returnAddr >> 8) & 0xFF));
        push_byte(static_cast<uint8_t>(returnAddr & 0xFF));
        uint16_t oldPC = PC_;
        PC_ = static_cast<uint16_t>(PC_ + 1 + k);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"RCALL\",\"offset\":" << k << ",\"new_pc\":" << PC_ << ",\"cycles\":3}";
        emitJson(ss.str());
        return 3;
    }

    // CALL (best-effort multiword: 0x940E followed by target word)
    if (instr == 0x940E) {
        uint16_t target = flash_.fetchWord(PC_ + 1);
        uint16_t returnAddr = static_cast<uint16_t>(PC_ + 2);
        push_byte(static_cast<uint8_t>((returnAddr >> 8) & 0xFF));
        push_byte(static_cast<uint8_t>(returnAddr & 0xFF));
        uint16_t oldPC = PC_;
        PC_ = target;
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"CALL\",\"target\":" << target << ",\"new_pc\":" << PC_ << ",\"cycles\":4}";
        emitJson(ss.str());
        return 4;
    }

    // JMP (best-effort multiword: 0x940C followed by target)
    if (instr == 0x940C) {
        uint16_t target = flash_.fetchWord(PC_ + 1);
        uint16_t oldPC = PC_;
        PC_ = target;
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"JMP\",\"target\":" << target << ",\"new_pc\":" << PC_ << ",\"cycles\":3}";
        emitJson(ss.str());
        return 3;
    }

    // RET
    if (instr == 0x9508) {
        uint8_t low = pop_byte();
        uint8_t high = pop_byte();
        uint16_t newPC = static_cast<uint16_t>((high << 8) | low);
        uint16_t oldPC = PC_;
        PC_ = newPC;
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"RET\",\"new_pc\":" << PC_ << ",\"cycles\":4}";
        emitJson(ss.str());
        return 4;
    }

    // BRxx family (use existing wide mapping - best-effort)
    if (top4 == 0xF000) {
        // cond in bits 8..11, k7..k0 in low byte
        uint8_t cond = static_cast<uint8_t>((instr >> 8) & 0x0F);
        int8_t k8 = static_cast<int8_t>(instr & 0x00FF);
        bool take = false;
        // Map cond codes (best-effort mapping)
        uint8_t Z = (SREG_ >> 1) & 1;
        uint8_t C = (SREG_ >> 0) & 1;
        uint8_t N = (SREG_ >> 2) & 1;
        uint8_t V = (SREG_ >> 3) & 1;
        uint8_t S = (SREG_ >> 4) & 1;

        switch (cond) {
        case 0: take = Z; break;
        case 1: take = !Z; break;
        case 2: take = C; break;
        case 3: take = !C; break;
        case 4: take = C; break;
        case 5: take = !C; break;
        case 6: take = N; break;
        case 7: take = !N; break;
        case 8: take = V; break;
        case 9: take = !V; break;
        case 10: take = (S == V); break;
        case 11: take = (S != V); break;
        default:
        {
            std::ostringstream ss; ss << "{\"type\":\"unsupported_branch_cond\",\"pc\":" << PC_ << ",\"cond\":" << (int)cond << "}";
            emitJson(ss.str());
            PC_ += 1;
            return 1;
        }
        }

        uint16_t oldPC = PC_;
        if (take) {
            PC_ = static_cast<uint16_t>(PC_ + 1 + (int)k8);
            std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"BRxx\",\"cond\":" << (int)cond << ",\"taken\":true,\"offset\":" << (int)k8 << ",\"new_pc\":" << PC_ << ",\"cycles\":2}";
            emitJson(ss.str());
            return 2;
        }
        else {
            PC_ += 1;
            std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << oldPC << ",\"mnemonic\":\"BRxx\",\"cond\":" << (int)cond << ",\"taken\":false,\"offset\":" << (int)k8 << ",\"new_pc\":" << PC_ << ",\"cycles\":1}";
            emitJson(ss.str());
            return 1;
        }
    }

    // 2) IN / OUT / LDI (already implemented earlier variants) : leave as before

    // IN
    if ((instr & 0xF800) == 0xB000) {
        uint8_t rd = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t A = io_addr_extract(instr);
        uint16_t memAddr = static_cast<uint16_t>(0x0020 + A);
        uint8_t val = bus_.read(memAddr);
        writeReg(rd, val);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"IN\",\"rd\":\"R" << (int)rd << "\",\"io\":" << (int)A << ",\"value\":" << (int)val << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; return 1;
    }
    // OUT
    if ((instr & 0xF800) == 0xB800) {
        uint8_t rr = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t A = io_addr_extract(instr);
        uint16_t memAddr = static_cast<uint16_t>(0x0020 + A);
        uint8_t val = readReg(rr);
        bus_.write(memAddr, val);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"OUT\",\"rr\":\"R" << (int)rr << "\",\"io\":" << (int)A << ",\"value\":" << (int)val << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; return 1;
    }

    // LDI immediate
    if ((instr & 0xF000) == 0xE000) {
        uint8_t K = static_cast<uint8_t>((instr & 0x000F) | ((instr >> 8) & 0xF0));
        uint8_t d = static_cast<uint8_t>(16 + ((instr >> 4) & 0x0F));
        writeReg(d, K);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LDI\",\"dest\":\"R" << (int)d << "\",\"imm\":" << (int)K << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; return 1;
    }

    // SUBI (immediate subtraction Rd,K) - mask/pattern per provided table 0xF000/0x5000
    if ((instr & 0xF000) == 0x5000) {
        uint8_t K = static_cast<uint8_t>((instr & 0x000F) | ((instr >> 8) & 0xF0));
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
        PC_ += 1; return 1;
    }

    // ANDI (immediate) - mask/pattern 0xF000/0x7000
    if ((instr & 0xF000) == 0x7000) {
        uint8_t K = static_cast<uint8_t>((instr & 0x000F) | ((instr >> 8) & 0xF0));
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
        PC_ += 1; return 1;
    }

    // ORI (immediate) - mask/pattern 0xF000/0x6000
    if ((instr & 0xF000) == 0x6000) {
        uint8_t K = static_cast<uint8_t>((instr & 0x000F) | ((instr >> 8) & 0xF0));
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
        PC_ += 1; return 1;
    }

    // 3) Two-register ALU ops (ADD/ADC/SUB/SBC/AND/EOR/OR/CP/CPSE)
    if (top6 == 0x0C00) { // ADD
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
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ADD\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << ",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; return 1;
    }
    if (top6 == 0x1C00) { // ADC
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
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ADC\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << ",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; return 1;
    }
    if (top6 == 0x1800) { // SUB
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
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"SUB\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << ",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; return 1;
    }

    // SBC (best-effort)
    if (top6 == 0x0800) {
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
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"SBC\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << ",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; return 1;
    }

    // AND/EOR/OR (register)
    if (top6 == 0x2000 || top6 == 0x2400 || top6 == 0x2800) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t a = readReg(rd), b = readReg(rr), res = 0;
        std::string m;
        if (top6 == 0x2000) { res = a & b; m = "AND"; }
        else if (top6 == 0x2400) { res = a ^ b; m = "EOR"; }
        else { res = a | b; m = "OR"; }
        bool Z = (res == 0); bool N = (res & 0x80) != 0; bool V = false; bool S = N ^ V;
        sreg_set_flag(SREG_, 5, false); sreg_set_flag(SREG_, 0, false); sreg_set_flag(SREG_, 1, Z);
        sreg_set_flag(SREG_, 2, N); sreg_set_flag(SREG_, 3, V); sreg_set_flag(SREG_, 4, S);
        writeReg(rd, res);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"" << m << "\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\",\"result\":" << (int)res << ",\"cycles\":1}";
        emitJson(ss.str());
        PC_ += 1; return 1;
    }

    // CP / CPI / CPSE
    if (top6 == 0x1400) { // CP
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
        PC_ += 1; return 1;
    }
    if ((instr & 0xF000) == 0x3000) { // CPI
        uint8_t K = static_cast<uint8_t>((instr & 0x000F) | ((instr >> 8) & 0xF0));
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
        PC_ += 1; return 1;
    }

    // CPSE (compare, skip if equal) : best-effort mask same as CP but has opcode bit pattern difference; we'll try to detect
    // If top6 == 0x1000 (example) treat as CPSE
    if (top6 == 0x1000) {
        auto [rd, rr] = rd_rr_extract(instr);
        uint8_t a = readReg(rd), b = readReg(rr);
        std::ostringstream ss;
        ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"CPSE\",\"rd\":\"R" << (int)rd << "\",\"rr\":\"R" << (int)rr << "\"}";
        emitJson(ss.str());
        if (a == b) {
            // skip next instruction (advance PC by 2 words)
            PC_ += 2;
            std::ostringstream ss2; ss2 << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"CPSE_SKIP\",\"skipped\":1}";
            emitJson(ss2.str());
            return 1; // treat as 1 cycle for this step (skipping will skip next)
        }
        else {
            PC_ += 1;
            return 1;
        }
    }

    // 4) INC / DEC / PUSH / POP (already handled earlier) - keep their masks
    if ((instr & 0xFE0F) == 0x920F) { // PUSH
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t val = readReg(d);
        push_byte(val);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"PUSH\",\"reg\":" << (int)d << ",\"value\":" << (int)val << ",\"cycles\":2}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }
    if ((instr & 0xFE0F) == 0x900F) { // POP
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t v = pop_byte();
        writeReg(d, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"POP\",\"reg\":" << (int)d << ",\"value\":" << (int)v << ",\"cycles\":2}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }

    // 5) Pointer based LD/ST for X/Y/Z (best-effort common forms)
    // X = R27:R26, Y = R29:R28, Z = R31:R30
    auto get_X = [&]() -> uint16_t { return (uint16_t)((R_[27] << 8) | R_[26]); };
    auto get_Y = [&]() -> uint16_t { return (uint16_t)((R_[29] << 8) | R_[28]); };
    auto get_Z = [&]() -> uint16_t { return (uint16_t)((R_[31] << 8) | R_[30]); };
    auto set_X = [&](uint16_t v) { R_[26] = v & 0xFF; R_[27] = (v >> 8) & 0xFF; };
    auto set_Y = [&](uint16_t v) { R_[28] = v & 0xFF; R_[29] = (v >> 8) & 0xFF; };
    auto set_Z = [&](uint16_t v) { R_[30] = v & 0xFF; R_[31] = (v >> 8) & 0xFF; };

    // LD/ST X- (added per provided table)
    if ((instr & 0xFE0F) == 0x900E) { // LD Rd, -X
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = static_cast<uint16_t>(get_X() - 1);
        set_X(addr);
        uint8_t v = bus_.read(addr);
        writeReg(d, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LD\",\"mode\":\"-X\",\"rd\":\"R" << (int)d << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }
    if ((instr & 0xFE0F) == 0x920E) { // ST -X, Rr
        uint8_t r = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = static_cast<uint16_t>(get_X() - 1);
        set_X(addr);
        uint8_t v = readReg(r);
        bus_.write(addr, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ST\",\"mode\":\"-X\",\"rr\":\"R" << (int)r << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }

    // LD Rd, X  (mask best-effort: 0x900C)
    if ((instr & 0xFE0F) == 0x900C) {
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = get_X();
        uint8_t v = bus_.read(addr);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LD\",\"mode\":\"X\",\"rd\":\"R" << (int)d << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        writeReg(d, v);
        PC_ += 1; return 2;
    }
    // LD Rd, X+ (post-inc) (mask best-effort 0x900D)
    if ((instr & 0xFE0F) == 0x900D) {
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = get_X();
        uint8_t v = bus_.read(addr);
        writeReg(d, v);
        set_X(static_cast<uint16_t>(addr + 1));
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LD\",\"mode\":\"X+\",\"rd\":\"R" << (int)d << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }
    // ST X, Rr (mask best-effort 0x920C)
    if ((instr & 0xFE0F) == 0x920C) {
        uint8_t r = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = get_X();
        uint8_t v = readReg(r);
        bus_.write(addr, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ST\",\"mode\":\"X\",\"rr\":\"R" << (int)r << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }
    // ST X+, Rr
    if ((instr & 0xFE0F) == 0x920D) {
        uint8_t r = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = get_X();
        uint8_t v = readReg(r);
        bus_.write(addr, v);
        set_X(static_cast<uint16_t>(addr + 1));
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ST\",\"mode\":\"X+\",\"rr\":\"R" << (int)r << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }

    // Similar Y/Z variants as simple forms (keep after X variants)
    // First handle LDD/STD with displacement for Y/Z (mask/pattern per provided table)
    if ((instr & 0xD208) == 0x8008) { // LDD Y+/Z+ with displacement (best-effort)
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t q = disp6(instr);
        bool useZ = (instr & (1 << 3)) != 0;
        uint16_t base = useZ ? get_Z() : get_Y();
        uint8_t v = bus_.read(static_cast<uint16_t>(base + q));
        writeReg(d, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LDD\",\"mode\":\"" << (useZ ? "Z" : "Y") << "\",\"rd\":\"R" << (int)d << "\",\"addr\":" << (base + q) << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }
    if ((instr & 0xD208) == 0x8208) { // STD Y/Z with displacement (best-effort)
        uint8_t r = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint8_t q = disp6(instr);
        bool useZ = (instr & (1 << 3)) != 0;
        uint16_t base = useZ ? get_Z() : get_Y();
        uint8_t v = readReg(r);
        bus_.write(static_cast<uint16_t>(base + q), v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"STD\",\"mode\":\"" << (useZ ? "Z" : "Y") << "\",\"rr\":\"R" << (int)r << "\",\"addr\":" << (base + q) << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }

    if ((instr & 0xFE0F) == 0x9008) { // LD Rd, Y
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = get_Y();
        uint8_t v = bus_.read(addr);
        writeReg(d, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LD\",\"mode\":\"Y\",\"rd\":\"R" << (int)d << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }
    if ((instr & 0xFE0F) == 0x9208) { // ST Y, Rr
        uint8_t r = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = get_Y();
        uint8_t v = readReg(r);
        bus_.write(addr, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ST\",\"mode\":\"Y\",\"rr\":\"R" << (int)r << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }
    if ((instr & 0xFE0F) == 0x9002) { // LD Rd, Z
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = get_Z();
        uint8_t v = bus_.read(addr);
        writeReg(d, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LD\",\"mode\":\"Z\",\"rd\":\"R" << (int)d << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }
    if ((instr & 0xFE0F) == 0x9202) { // ST Z, Rr
        uint8_t r = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = get_Z();
        uint8_t v = readReg(r);
        bus_.write(addr, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"ST\",\"mode\":\"Z\",\"rr\":\"R" << (int)r << "\",\"addr\":" << addr << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }

    // 6) LDS/STS (direct data memory access, 2-word opcodes best-effort)
    // Detect STS pattern (best-effort): 0x9200 family with next word as address; we'll match 0x92xx where low nibble indicates STS
    if ((instr & 0xFF00) == 0x9200) {
        // best-effort: if low nibble is 0x0 or 0x1 treat as STS, else ignore
        uint8_t low = instr & 0x00FF;
        if ((low & 0x0F) <= 3) {
            // STS: second word contains 16-bit address; register in bits
            uint8_t r = static_cast<uint8_t>((instr >> 4) & 0x1F);
            uint16_t addr = flash_.fetchWord(PC_ + 1); // treat second word as address
            uint8_t val = readReg(r);
            bus_.write(addr, val);
            std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"STS\",\"rr\":\"R" << (int)r << "\",\"addr\":" << addr << ",\"value\":" << (int)val << "}";
            emitJson(ss.str());
            PC_ += 2; return 3;
        }
    }
    // LDS: 0x9000 family best-effort: detect 0x9000 with next word address and destination reg in bits
    if ((instr & 0xFF00) == 0x9000) {
        uint8_t d = static_cast<uint8_t>((instr >> 4) & 0x1F);
        uint16_t addr = flash_.fetchWord(PC_ + 1);
        uint8_t val = bus_.read(addr);
        writeReg(d, val);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LDS\",\"rd\":\"R" << (int)d << "\",\"addr\":" << addr << ",\"value\":" << (int)val << "}";
        emitJson(ss.str());
        PC_ += 2; return 3;
    }

    // 7) Bit test/skip and I/O bit ops (SBRC, SBRS, SBI, CBI) - best-effort masks
    // SBRC/SBRS often start with 1111 11.. pattern; we approximate:
    if ((instr & 0xFC00) == 0xFC00) { // heuristically map many bit test opcodes here
        // Extract bit and reg roughly
        uint8_t bit = static_cast<uint8_t>((instr >> 9) & 0x07);
        uint8_t r = static_cast<uint8_t>((instr >> 4) & 0x1F);
        // Determine opcode sub-kind via some bits
        uint8_t sub = static_cast<uint8_t>((instr >> 8) & 0x01);
        uint8_t val = readReg(r);
        bool bitset = ((val >> bit) & 1) != 0;
        // sub==0 -> SBRC (skip if clear) ; sub==1 -> SBRS (skip if set)  (this is a heuristic)
        bool skip = (sub == 0) ? (!bitset) : bitset;
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"SBx\",\"reg\":\"R" << (int)r << "\",\"bit\":" << (int)bit << ",\"bitset\":" << (int)bitset << "}";
        emitJson(ss.str());
        if (skip) {
            PC_ += 2; // skip next word (heuristic)
            std::ostringstream ss2; ss2 << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"SBx_SKIP\",\"skipped\":1}";
            emitJson(ss2.str());
            return 1;
        }
        else {
            PC_ += 1; return 1;
        }
    }

    // SBI / CBI I/O bit set/clear (best-effort mapping)
    if ((instr & 0xFC00) == 0x9C00) {
        uint8_t A = static_cast<uint8_t>(instr & 0x1F); // io addr low bits
        uint8_t b = static_cast<uint8_t>((instr >> 5) & 0x07);
        uint16_t ioaddr = 0x0020 + A;
        uint8_t v = bus_.read(ioaddr);
        // decide SBI or CBI by a bit in opcode (heuristic)
        bool doSet = ((instr >> 9) & 1) != 0;
        if (doSet) v |= (1 << b); else v &= ~(1 << b);
        bus_.write(ioaddr, v);
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"SBI/CBI\",\"io\":" << (int)A << ",\"bit\":" << (int)b << ",\"value\":" << (int)v << "}";
        emitJson(ss.str());
        PC_ += 1; return 2;
    }

    // SEI / CLI exact encodings (common)
    if (instr == 0x9478) {
        // SEI
        SREG_ |= (1 << 7); // I bit
        emitJson("{\"type\":\"instruction\",\"pc\":" + std::to_string(PC_) + ",\"mnemonic\":\"SEI\",\"cycles\":1}");
        PC_ += 1; return 1;
    }
    if (instr == 0x94F8) {
        // CLI
        SREG_ &= ~(1 << 7);
        emitJson("{\"type\":\"instruction\",\"pc\":" + std::to_string(PC_) + ",\"mnemonic\":\"CLI\",\"cycles\":1}");
        PC_ += 1; return 1;
    }

    // RETI exact match
    if (instr == 0x9518) {
        uint8_t low = pop_byte();
        uint8_t high = pop_byte();
        uint16_t newPC = static_cast<uint16_t>((high << 8) | low);
        SREG_ |= (1 << 7); // set I
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"RETI\",\"new_pc\":" << newPC << ",\"cycles\":4}";
        emitJson(ss.str());
        PC_ = newPC;
        return 4;
    }

    // 8) LPM (read program memory byte into R0) - best-effort simple variant
    // Detect opcode pattern heuristic 0x95C8 or so; we implement for any 0x95C0..0x95CF
    if ((instr & 0xFFC0) == 0x95C0) {
        // simplest: read low byte of flash at Z pointer (word address = Z/1), Z in R31:R30 as byte address
        uint16_t z = get_Z();
        // read from flash: flash is word-addressed; convert byte address to word index
        uint32_t wordAddr = z / 2;
        uint16_t w = flash_.fetchWord(wordAddr);
        uint8_t res = (z & 1) ? static_cast<uint8_t>(w >> 8) : static_cast<uint8_t>(w & 0xFF);
        writeReg(0, res); // LPM puts result in R0
        std::ostringstream ss; ss << "{\"type\":\"instruction\",\"pc\":" << PC_ << ",\"mnemonic\":\"LPM\",\"z\":" << z << ",\"value\":" << (int)res << "}";
        emitJson(ss.str());
        PC_ += 1; return 3;
    }

    // If we reached here, it's unsupported by our heuristics
    {
        std::ostringstream ss;
        ss << "{\"type\":\"unsupported\",\"pc\":" << PC_ << ",\"word\":\"0x" << std::hex << std::setw(4) << std::setfill('0') << instr << std::dec << "\"}";
        emitJson(ss.str());
    }
    return 0;
}

Bus& CPU::bus() { return bus_; }