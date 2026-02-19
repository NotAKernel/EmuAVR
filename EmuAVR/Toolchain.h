#pragma once
#include <string>

struct Toolchain {
    // Try to compile C source to Intel HEX using avr-gcc + avr-objcopy.
    // Returns true on success. errorOut will contain diagnostics if false.
    static bool compileToHex(const std::string& srcPath, const std::string& hexOut, std::string& errorOut);
};