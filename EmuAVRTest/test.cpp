#include "pch.h"
#include <gtest/gtest.h>
#include "Bus.h"
#include "SRAM.h"
#include "Flash.h"
#include "CPU.h"
#include <vector>
#include <memory>

// ============================================================================
// SRAM UNIT TESTS
// ============================================================================

class SRAMTest : public ::testing::Test {
protected:
    SRAM sram{ 256 };
};

TEST_F(SRAMTest, WriteAndReadSingleByte) {
    sram.write(0x00, 0xAB);
    EXPECT_EQ(sram.read(0x00), 0xAB);
}

TEST_F(SRAMTest, WriteAndReadMultipleBytes) {
    sram.write(0x00, 0x12);
    sram.write(0x01, 0x34);
    sram.write(0x02, 0x56);
    EXPECT_EQ(sram.read(0x00), 0x12);
    EXPECT_EQ(sram.read(0x01), 0x34);
    EXPECT_EQ(sram.read(0x02), 0x56);
}

TEST_F(SRAMTest, InitializedToZero) {
    EXPECT_EQ(sram.read(0x00), 0x00);
    EXPECT_EQ(sram.read(0x50), 0x00);
    EXPECT_EQ(sram.read(0xFF), 0x00);
}

TEST_F(SRAMTest, OverwriteValue) {
    sram.write(0x10, 0xFF);
    EXPECT_EQ(sram.read(0x10), 0xFF);
    sram.write(0x10, 0x00);
    EXPECT_EQ(sram.read(0x10), 0x00);
}

TEST_F(SRAMTest, BoundaryAddresses) {
    sram.write(0x00, 0x11);
    sram.write(0xFF, 0x22);
    EXPECT_EQ(sram.read(0x00), 0x11);
    EXPECT_EQ(sram.read(0xFF), 0x22);
}

TEST_F(SRAMTest, ReadOutOfRange) {
    uint8_t result = sram.read(0x100);
    EXPECT_EQ(result, 0xFF);
}

TEST_F(SRAMTest, WriteOutOfRange) {
    sram.write(0x100, 0xAA);
    sram.write(0x1FF, 0xBB);
    // Should not crash or affect valid memory
    EXPECT_EQ(sram.read(0x00), 0x00);
}

TEST_F(SRAMTest, WriteAllBytes) {
    for (int i = 0; i < 256; ++i) {
        sram.write(i, i & 0xFF);
    }
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(sram.read(i), i & 0xFF);
    }
}

// ============================================================================
// FLASH UNIT TESTS
// ============================================================================

class FlashTest : public ::testing::Test {
protected:
    Flash flash{ 256 };  // 256 words
};

TEST_F(FlashTest, LoadWordsAndFetch) {
    std::vector<uint16_t> program = { 0x1234, 0x5678, 0x9ABC };
    flash.loadWords(program);
    EXPECT_EQ(flash.fetchWord(0), 0x1234);
    EXPECT_EQ(flash.fetchWord(1), 0x5678);
    EXPECT_EQ(flash.fetchWord(2), 0x9ABC);
}

TEST_F(FlashTest, FetchOutOfRange) {
    std::vector<uint16_t> program = { 0x1234 };
    flash.loadWords(program);
    EXPECT_EQ(flash.fetchWord(100), 0x0000);
}

TEST_F(FlashTest, InitializedToZero) {
    EXPECT_EQ(flash.fetchWord(0), 0x0000);
    EXPECT_EQ(flash.fetchWord(50), 0x0000);
}

TEST_F(FlashTest, SizeWords) {
    EXPECT_EQ(flash.sizeWords(), 256);
    std::vector<uint16_t> program = { 0x1111, 0x2222, 0x3333, 0x4444 };
    flash.loadWords(program);
    EXPECT_EQ(flash.sizeWords(), 4);
}

TEST_F(FlashTest, LoadEmptyProgram) {
    std::vector<uint16_t> empty;
    flash.loadWords(empty);
    EXPECT_EQ(flash.sizeWords(), 0);
    EXPECT_EQ(flash.fetchWord(0), 0x0000);
}

TEST_F(FlashTest, LoadLargeProgram) {
    std::vector<uint16_t> program(1024);
    for (int i = 0; i < 1024; ++i) {
        program[i] = 0x1000 + i;
    }
    flash.loadWords(program);
    EXPECT_EQ(flash.sizeWords(), 1024);
    EXPECT_EQ(flash.fetchWord(0), 0x1000);
    EXPECT_EQ(flash.fetchWord(500), 0x11F4);
    EXPECT_EQ(flash.fetchWord(1023), 0x13FF);
}

TEST_F(FlashTest, FetchWordDataIntegrity) {
    std::vector<uint16_t> program = { 0xFFFF, 0x0000, 0xAAAA, 0x5555 };
    flash.loadWords(program);
    EXPECT_EQ(flash.fetchWord(0), 0xFFFF);
    EXPECT_EQ(flash.fetchWord(1), 0x0000);
    EXPECT_EQ(flash.fetchWord(2), 0xAAAA);
    EXPECT_EQ(flash.fetchWord(3), 0x5555);
}

// ============================================================================
// BUS UNIT TESTS
// ============================================================================

class BusTest : public ::testing::Test {
protected:
    Bus bus;
    std::shared_ptr<SRAM> sram1 = std::make_shared<SRAM>(256);
    std::shared_ptr<SRAM> sram2 = std::make_shared<SRAM>(128);
};

TEST_F(BusTest, ReadWriteToMappedDevice) {
    bus.map(0x0000, 256, sram1);
    bus.write(0x00, 0xAB);
    EXPECT_EQ(bus.read(0x00), 0xAB);
}

TEST_F(BusTest, MultipleDeviceMapping) {
    bus.map(0x0000, 256, sram1);   // 0x0000 - 0x00FF
    bus.map(0x0100, 128, sram2);   // 0x0100 - 0x017F

    bus.write(0x00, 0x11);
    bus.write(0xFF, 0x22);
    bus.write(0x0100, 0x33);
    bus.write(0x017F, 0x44);

    EXPECT_EQ(bus.read(0x00), 0x11);
    EXPECT_EQ(bus.read(0xFF), 0x22);
    EXPECT_EQ(bus.read(0x0100), 0x33);
    EXPECT_EQ(bus.read(0x017F), 0x44);
}

TEST_F(BusTest, DeviceIsolation) {
    bus.map(0x0000, 256, sram1);
    bus.map(0x0100, 128, sram2);

    bus.write(0x10, 0xAA);
    bus.write(0x0110, 0xBB);

    // sram1 at 0x10 should have 0xAA, not affected by sram2 write
    EXPECT_EQ(bus.read(0x10), 0xAA);
    EXPECT_EQ(bus.read(0x0110), 0xBB);
}

TEST_F(BusTest, UnmappedAddress) {
    bus.map(0x0000, 256, sram1);
    uint8_t result = bus.read(0x0200);
    EXPECT_EQ(result, 0xFF);
}

TEST_F(BusTest, WriteToUnmappedAddress) {
    bus.map(0x0000, 256, sram1);
    bus.write(0x0200, 0xCC);
    // Should not crash; write is ignored
    EXPECT_EQ(bus.read(0x00), 0x00);
}

TEST_F(BusTest, LocalAddressTranslation) {
    bus.map(0x0100, 128, sram2);

    bus.write(0x0100, 0x11);  // First address of sram2 (local addr 0)
    bus.write(0x0150, 0x22);  // Somewhere in the middle
    bus.write(0x017F, 0x33);  // Last address of sram2 (local addr 127)

    EXPECT_EQ(bus.read(0x0100), 0x11);
    EXPECT_EQ(bus.read(0x0150), 0x22);
    EXPECT_EQ(bus.read(0x017F), 0x33);
}

TEST_F(BusTest, SequentialReadsAndWrites) {
    bus.map(0x0000, 256, sram1);

    for (int i = 0; i < 256; ++i) {
        bus.write(i, i & 0xFF);
    }

    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(bus.read(i), i & 0xFF);
    }
}

// ============================================================================
// CPU UNIT TESTS
// ============================================================================

class CPUTest : public ::testing::Test {
protected:
    std::shared_ptr<SRAM> sram = std::make_shared<SRAM>(8192);
    Flash flash{ 8192 };
    Bus bus;
    CPU cpu{ bus, flash };

    void SetUp() override {
        bus.map(0x0000, 8192, sram);
    }
};

TEST_F(CPUTest, ResetInitialState) {
    cpu.reset();
    EXPECT_EQ(cpu.readReg(0), 0x00);
    EXPECT_EQ(cpu.readReg(31), 0x00);
}

TEST_F(CPUTest, WriteAndReadRegister) {
    cpu.writeReg(5, 0xAB);
    EXPECT_EQ(cpu.readReg(5), 0xAB);
}

TEST_F(CPUTest, MultipleRegisterOperations) {
    cpu.writeReg(0, 0x11);
    cpu.writeReg(15, 0x22);
    cpu.writeReg(31, 0x33);

    EXPECT_EQ(cpu.readReg(0), 0x11);
    EXPECT_EQ(cpu.readReg(15), 0x22);
    EXPECT_EQ(cpu.readReg(31), 0x33);
}

TEST_F(CPUTest, RegisterIndependence) {
    cpu.writeReg(0, 0xFF);
    cpu.writeReg(1, 0x00);

    EXPECT_EQ(cpu.readReg(0), 0xFF);
    EXPECT_EQ(cpu.readReg(1), 0x00);
}

TEST_F(CPUTest, BusAccess) {
    Bus& accessedBus = cpu.bus();
    accessedBus.write(0x1000, 0x55);
    EXPECT_EQ(accessedBus.read(0x1000), 0x55);
}

TEST_F(CPUTest, RegisterOverwrite) {
    cpu.writeReg(10, 0x33);
    EXPECT_EQ(cpu.readReg(10), 0x33);
    cpu.writeReg(10, 0x99);
    EXPECT_EQ(cpu.readReg(10), 0x99);
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

class CPUBusIntegrationTest : public ::testing::Test {
protected:
    std::shared_ptr<SRAM> sram = std::make_shared<SRAM>(8192);
    Flash flash{ 8192 };
    Bus bus;
    CPU cpu{ bus, flash };

    void SetUp() override {
        bus.map(0x0000, 8192, sram);
    }
};

TEST_F(CPUBusIntegrationTest, CPUWritesToBus) {
    cpu.writeReg(0, 0x42);
    bus.write(0x0100, cpu.readReg(0));
    EXPECT_EQ(bus.read(0x0100), 0x42);
}

TEST_F(CPUBusIntegrationTest, CPUReadFromBusData) {
    bus.write(0x0200, 0x78);
    uint8_t value = bus.read(0x0200);
    cpu.writeReg(1, value);
    EXPECT_EQ(cpu.readReg(1), 0x78);
}

TEST_F(CPUBusIntegrationTest, RegisterToMemoryTransfer) {
    // Simulate register-to-memory transfer
    cpu.writeReg(0, 0x11);
    cpu.writeReg(1, 0x22);
    cpu.writeReg(2, 0x33);

    bus.write(0x0000, cpu.readReg(0));
    bus.write(0x0001, cpu.readReg(1));
    bus.write(0x0002, cpu.readReg(2));

    EXPECT_EQ(bus.read(0x0000), 0x11);
    EXPECT_EQ(bus.read(0x0001), 0x22);
    EXPECT_EQ(bus.read(0x0002), 0x33);
}

TEST_F(CPUBusIntegrationTest, MemoryToRegisterTransfer) {
    bus.write(0x1000, 0xAA);
    bus.write(0x1001, 0xBB);
    bus.write(0x1002, 0xCC);

    cpu.writeReg(5, bus.read(0x1000));
    cpu.writeReg(6, bus.read(0x1001));
    cpu.writeReg(7, bus.read(0x1002));

    EXPECT_EQ(cpu.readReg(5), 0xAA);
    EXPECT_EQ(cpu.readReg(6), 0xBB);
    EXPECT_EQ(cpu.readReg(7), 0xCC);
}

TEST_F(CPUBusIntegrationTest, MultipleBusAccessPatterns) {
    // Write pattern
    for (int i = 0; i < 16; ++i) {
        cpu.writeReg(i % 32, i);
        bus.write(0x0100 + i, cpu.readReg(i % 32));
    }

    // Verify pattern
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(bus.read(0x0100 + i), i);
    }
}

class FlashAndCPUIntegrationTest : public ::testing::Test {
protected:
    std::shared_ptr<SRAM> sram = std::make_shared<SRAM>(8192);
    Flash flash{ 8192 };
    Bus bus;
    CPU cpu{ bus, flash };

    void SetUp() override {
        bus.map(0x0000, 8192, sram);
    }
};

TEST_F(FlashAndCPUIntegrationTest, LoadAndFetchProgram) {
    std::vector<uint16_t> program = { 0x1234, 0x5678, 0x9ABC, 0xDEF0 };
    flash.loadWords(program);

    EXPECT_EQ(flash.fetchWord(0), 0x1234);
    EXPECT_EQ(flash.fetchWord(1), 0x5678);
    EXPECT_EQ(flash.fetchWord(2), 0x9ABC);
    EXPECT_EQ(flash.fetchWord(3), 0xDEF0);
}

TEST_F(FlashAndCPUIntegrationTest, CPUAccessFlash) {
    std::vector<uint16_t> program = { 0x2000, 0x4000, 0x6000 };
    flash.loadWords(program);

    EXPECT_EQ(flash.fetchWord(0), 0x2000);
    EXPECT_EQ(flash.fetchWord(1), 0x4000);
    EXPECT_EQ(flash.fetchWord(2), 0x6000);
}

TEST_F(FlashAndCPUIntegrationTest, LargeProgram) {
    std::vector<uint16_t> program(256);
    for (int i = 0; i < 256; ++i) {
        program[i] = 0x1000 + i;
    }
    flash.loadWords(program);

    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(flash.fetchWord(i), 0x1000 + i);
    }
}

// ============================================================================
// SYSTEM TESTS
// ============================================================================

class CPUSystemTest : public ::testing::Test {
protected:
    std::shared_ptr<SRAM> sram = std::make_shared<SRAM>(8192);
    Flash flash{ 8192 };
    Bus bus;
    CPU cpu{ bus, flash };

    void SetUp() override {
        bus.map(0x0000, 8192, sram);
    }
};

TEST_F(CPUSystemTest, CompleteRegisterTransferSequence) {
    // Scenario: Initialize all registers with a pattern, store to memory, reload and verify
    for (int i = 0; i < 32; ++i) {
        cpu.writeReg(i, (i * 7) & 0xFF);
    }

    // Transfer registers to memory
    for (int i = 0; i < 32; ++i) {
        bus.write(0x0000 + i, cpu.readReg(i));
    }

    // Clear registers
    for (int i = 0; i < 32; ++i) {
        cpu.writeReg(i, 0x00);
    }

    // Verify cleared
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(cpu.readReg(i), 0x00);
    }

    // Reload from memory
    for (int i = 0; i < 32; ++i) {
        cpu.writeReg(i, bus.read(0x0000 + i));
    }

    // Verify restored
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(cpu.readReg(i), (i * 7) & 0xFF);
    }
}

TEST_F(CPUSystemTest, StackLikeOperations) {
    // Simulate stack-like push/pop behavior using SRAM
    uint16_t stackPtr = 0x100;

    // "Push" values
    for (int i = 0; i < 8; ++i) {
        cpu.writeReg(i, (0x10 * (i + 1)));
        bus.write(stackPtr + i, cpu.readReg(i));
    }

    // "Pop" and verify in reverse order
    for (int i = 7; i >= 0; --i) {
        uint8_t value = bus.read(stackPtr + i);
        EXPECT_EQ(value, (0x10 * (i + 1)));
    }
}

TEST_F(CPUSystemTest, MultipleBusDeviceCooperation) {
    // Create multiple memory regions
    auto sram2 = std::make_shared<SRAM>(1024);
    bus.map(0x2000, 1024, sram2);

    // Write to region 1
    for (int i = 0; i < 16; ++i) {
        bus.write(0x0000 + i, 0xAA + i);
    }

    // Write to region 2
    for (int i = 0; i < 16; ++i) {
        bus.write(0x2000 + i, 0xBB + i);
    }

    // Verify region 1
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(bus.read(0x0000 + i), 0xAA + i);
    }

    // Verify region 2
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(bus.read(0x2000 + i), 0xBB + i);
    }
}

TEST_F(CPUSystemTest, ComplexMemoryPattern) {
    // Create a complex pattern using registers and memory
    std::vector<uint8_t> pattern = { 0x00, 0x55, 0xAA, 0xFF, 0x0F, 0xF0, 0x33, 0xCC };

    for (size_t i = 0; i < pattern.size(); ++i) {
        cpu.writeReg(i, pattern[i]);
    }

    // Store to memory with interleaving
    for (size_t i = 0; i < pattern.size(); ++i) {
        bus.write(0x0200 + i * 2, cpu.readReg(i));
        bus.write(0x0200 + i * 2 + 1, cpu.readReg(i) ^ 0xFF);
    }

    // Verify pattern
    for (size_t i = 0; i < pattern.size(); ++i) {
        EXPECT_EQ(bus.read(0x0200 + i * 2), pattern[i]);
        EXPECT_EQ(bus.read(0x0200 + i * 2 + 1), pattern[i] ^ 0xFF);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
