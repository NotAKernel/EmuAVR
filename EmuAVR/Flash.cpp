#include "Flash.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

Flash::Flash(size_t words) : words_(words, 0) {}

void Flash::loadWords(const std::vector<uint16_t>& data) {
    words_ = data;
}

uint16_t Flash::fetchWord(uint32_t wordAddr) const {
    if (wordAddr >= words_.size()) {
        std::cout << "[Flash] Fetch out of range wordAddr=" << wordAddr << "\n";
        return 0x0000;
    }
    uint16_t w = words_[wordAddr];
    return w;
}

size_t Flash::sizeWords() const { return words_.size(); }

// Simple Intel HEX loader supporting data records (00) and extended linear address (04)
bool Flash::loadFromHex(const std::string& path, std::string& error) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        error = "Failed to open HEX file: " + path;
        return false;
    }

    std::vector<uint8_t> bytes;
    uint32_t upperAddr = 0;
    uint32_t maxAddr = 0;

    std::string line;
    int lineno = 0;
    while (std::getline(ifs, line)) {
        ++lineno;
        if (line.empty()) continue;
        if (line[0] != ':') {
            error = "Invalid HEX line (missing ':') at line " + std::to_string(lineno);
            return false;
        }
        if (line.size() < 11) {
            error = "Invalid HEX line (too short) at line " + std::to_string(lineno);
            return false;
        }
        auto hexByte = [&](size_t pos) -> int {
            int hi = std::isdigit((unsigned char)line[pos]) ? line[pos] - '0' : std::toupper((unsigned char)line[pos]) - 'A' + 10;
            int lo = std::isdigit((unsigned char)line[pos+1]) ? line[pos+1] - '0' : std::toupper((unsigned char)line[pos+1]) - 'A' + 10;
            return (hi << 4) | lo;
        };
        int count = hexByte(1);
        int addrHi = hexByte(3);
        int addrLo = hexByte(5);
        int rectype = hexByte(7);
        uint32_t addr = (uint32_t)((addrHi << 8) | addrLo);

        if (line.size() < 11 + count*2) {
            error = "HEX line too short for data at line " + std::to_string(lineno);
            return false;
        }

        if (rectype == 0x00) { // data
            uint32_t absAddr = (upperAddr << 16) | addr;
            if (absAddr + count > (1u<<24)) {
                error = "HEX address overflow at line " + std::to_string(lineno);
                return false;
            }
            if (bytes.size() < absAddr + count) bytes.resize(absAddr + count, 0xFF);
            for (int i = 0; i < count; ++i) {
                int val = hexByte(9 + i*2);
                bytes[absAddr + i] = static_cast<uint8_t>(val);
            }
            maxAddr = std::max<uint32_t>(maxAddr, absAddr + count - 1);
        } else if (rectype == 0x01) { // EOF
            break;
        } else if (rectype == 0x04) { // Extended linear address
            if (count != 2) {
                error = "Invalid extended linear address record length at line " + std::to_string(lineno);
                return false;
            }
            int hi = hexByte(9);
            int lo = hexByte(11);
            upperAddr = ((hi << 8) | lo);
        } else {
            // ignore other record types for now
        }
    }

    // Convert to word-addressable flash (AVR words are little-endian 16-bit)
    size_t byteCount = bytes.size();
    size_t wordCount = (byteCount + 1) / 2;
    words_.assign(wordCount, 0x0000);
    for (size_t i = 0; i < wordCount; ++i) {
        uint8_t lo = (i*2 < byteCount) ? bytes[i*2] : 0xFF;
        uint8_t hi = (i*2+1 < byteCount) ? bytes[i*2+1] : 0xFF;
        words_[i] = static_cast<uint16_t>((hi << 8) | lo);
    }

    std::cout << "[Flash] Loaded " << wordCount << " words (" << byteCount << " bytes) from " << path << "\n";
    return true;
}