#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstddef>

class Flash {
public:
    // program memory is word-addressed (16-bit words)
    explicit Flash(size_t words = 0);
    void loadWords(const std::vector<uint16_t>& data);
    uint16_t fetchWord(uint32_t wordAddr) const;
    size_t sizeWords() const;

    // Load Intel HEX file into flash words (returns true on success)
    bool loadFromHex(const std::string& path, std::string& error);

private:
    std::vector<uint16_t> words_;
};