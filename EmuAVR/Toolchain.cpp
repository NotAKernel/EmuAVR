#include "Toolchain.h"
#include <cstdlib>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>

bool Toolchain::compileToHex(const std::string& srcPath, const std::string& hexOut, std::string& errorOut) {
    // Temporary object and elf names
    std::string elf = "temp_emavr.elf";
    // Command: avr-gcc -mmcu=atmega328p -Os -o temp.elf src.c && avr-objcopy -O ihex temp.elf out.hex
    std::ostringstream cmd;
    cmd << "avr-gcc -mmcu=atmega328p -Os -o \"" << elf << "\" \"" << srcPath << "\" 2> compile_err.txt";
    int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        std::ifstream ifs("compile_err.txt");
        std::string err;
        if (ifs.is_open()) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            err = ss.str();
        }
        errorOut = "avr-gcc failed. Return code: " + std::to_string(rc) + "\n" + err;
        return false;
    }
    std::ostringstream objcmd;
    objcmd << "avr-objcopy -O ihex \"" << elf << "\" \"" << hexOut << "\" 2> objcopy_err.txt";
    rc = std::system(objcmd.str().c_str());
    if (rc != 0) {
        std::ifstream ifs("objcopy_err.txt");
        std::string err;
        if (ifs.is_open()) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            err = ss.str();
        }
        errorOut = "avr-objcopy failed. Return code: " + std::to_string(rc) + "\n" + err;
        return false;
    }
    // Success
    std::cout << "[Toolchain] Compiled " << srcPath << " -> " << hexOut << "\n";
    return true;
}