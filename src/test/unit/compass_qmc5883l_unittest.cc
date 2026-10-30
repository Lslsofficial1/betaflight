/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Betaflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <array>
#include <cstring>
#include <vector>

extern "C" {
#include "platform.h"
#include "drivers/compass/compass.h"
#include "drivers/compass/compass_qmc5883l.h"
#include "drivers/time.h"
}

#include "gtest/gtest.h"

namespace {
std::array<uint8_t, 256> registers;
std::vector<uint8_t> asyncRegisters;
bool readFails;
bool writeFails;
bool asyncReadFails;
bool preserveConfigOnReset;
unsigned registeredDevices;
uint32_t elapsedMs;

class Qmc5883lTest : public testing::Test {
protected:
    magDev_t mag{};

    void SetUp() override
    {
        registers.fill(0);
        registers[0x0D] = 0xFF;
        asyncRegisters.clear();
        readFails = false;
        writeFails = false;
        asyncReadFails = false;
        preserveConfigOnReset = false;
        registeredDevices = 0;
        elapsedMs = 0;
        mag.bus.busType = BUS_TYPE_I2C;
        mag.bus.busType_u.i2c.device = I2CDEV_2;
        mag.dev.bus = &mag.bus;
        mag.dev.busType_u.i2c.address = 0x0D;
    }

    void TearDown() override
    {
        // Complete any outstanding read so function-local driver state cannot
        // leak into the next test, including with shuffled test order.
        if (mag.read) {
            readFails = false;
            asyncReadFails = false;
            registers[0x06] = 0x01;
            int16_t data[3];
            for (unsigned i = 0; i < 4; i++) {
                if (mag.read(&mag, data)) {
                    break;
                }
            }
        }
    }

    void setAxes(int16_t x, int16_t y, int16_t z)
    {
        const int16_t axes[] = {x, y, z};
        for (unsigned i = 0; i < 3; i++) {
            registers[2 * i] = static_cast<uint16_t>(axes[i]);
            registers[2 * i + 1] = static_cast<uint16_t>(axes[i]) >> 8;
        }
    }
};
} // namespace

TEST_F(Qmc5883lTest, DetectsDeviceOnI2c2AtExplicitAddress)
{
    ASSERT_TRUE(qmc5883lDetect(&mag));
    EXPECT_EQ(0x0D, mag.dev.busType_u.i2c.address);
    EXPECT_EQ(I2CDEV_2, mag.dev.bus->busType_u.i2c.device);
    EXPECT_EQ(20U, elapsedMs);
    EXPECT_NE(nullptr, mag.init);
    EXPECT_NE(nullptr, mag.read);
}

TEST_F(Qmc5883lTest, AppliesDefaultAddressWhenUnspecified)
{
    mag.dev.busType_u.i2c.address = 0;
    EXPECT_TRUE(qmc5883lDetect(&mag));
    EXPECT_EQ(0x0D, mag.dev.busType_u.i2c.address);
}

TEST_F(Qmc5883lTest, RejectsWrongChipIdAndReadFailure)
{
    registers[0x0D] = 0;
    EXPECT_FALSE(qmc5883lDetect(&mag));
    registers[0x0D] = 0xFF;
    readFails = true;
    EXPECT_FALSE(qmc5883lDetect(&mag));
    EXPECT_EQ(nullptr, mag.init);
}

TEST_F(Qmc5883lTest, RejectsFloatingBusSignatureInsteadOfAcceptingIdAlone)
{
    registers[0x09] = 0xFF;
    preserveConfigOnReset = true;
    EXPECT_FALSE(qmc5883lDetect(&mag));
    EXPECT_EQ(nullptr, mag.init);
}

TEST_F(Qmc5883lTest, ConfiguresContinuous200HzEightGaussWithoutInterruptPin)
{
    ASSERT_TRUE(qmc5883lDetect(&mag));
    ASSERT_TRUE(mag.init(&mag));
    EXPECT_EQ(0x01, registers[0x0B]);
    EXPECT_EQ(0x1D, registers[0x09]);
    EXPECT_EQ(200, mag.magOdrHz);
    EXPECT_EQ(1U, registeredDevices);
    EXPECT_EQ(0, mag.magIntExtiTag);
}

TEST_F(Qmc5883lTest, ReportsConfigurationWriteFailure)
{
    ASSERT_TRUE(qmc5883lDetect(&mag));
    writeFails = true;
    EXPECT_FALSE(mag.init(&mag));
    EXPECT_EQ(0, mag.magOdrHz);
}

TEST_F(Qmc5883lTest, WaitsForDataReadyAndDecodesSignedLittleEndianAxes)
{
    ASSERT_TRUE(qmc5883lDetect(&mag));
    int16_t data[] = {1, 2, 3};
    EXPECT_FALSE(mag.read(&mag, data));
    EXPECT_EQ(1, data[0]);
    registers[0x06] = 0x01;
    setAxes(-32768, 0x1234, 32767);
    EXPECT_FALSE(mag.read(&mag, data)); // Request DRDY status.
    EXPECT_FALSE(mag.read(&mag, data)); // Request six data bytes.
    EXPECT_EQ(1, data[0]);
    ASSERT_TRUE(mag.read(&mag, data));
    EXPECT_EQ(-32768, data[0]);
    EXPECT_EQ(0x1234, data[1]);
    EXPECT_EQ(32767, data[2]);
    registers[0x06] = 0;
    EXPECT_FALSE(mag.read(&mag, data)); // New status is required.
}

TEST_F(Qmc5883lTest, DoesNotPublishWhenAsyncTransferCannotStart)
{
    ASSERT_TRUE(qmc5883lDetect(&mag));
    registers[0x06] = 0x01;
    int16_t data[] = {10, 20, 30};
    ASSERT_FALSE(mag.read(&mag, data)); // Cache DRDY status.
    asyncReadFails = true;
    EXPECT_FALSE(mag.read(&mag, data));
    EXPECT_EQ(10, data[0]);
    EXPECT_EQ(20, data[1]);
    EXPECT_EQ(30, data[2]);
}

TEST_F(Qmc5883lTest, UnlocksOverrunDataBeforeRecheckingStatus)
{
    ASSERT_TRUE(qmc5883lDetect(&mag));
    registers[0x06] = 0x04;
    int16_t data[] = {10, 20, 30};
    EXPECT_FALSE(mag.read(&mag, data)); // Cache overrun status.
    EXPECT_FALSE(mag.read(&mag, data)); // Read Z MSB to unlock.
    ASSERT_EQ(2U, asyncRegisters.size());
    EXPECT_EQ(0x06, asyncRegisters[0]);
    EXPECT_EQ(0x05, asyncRegisters[1]);
    EXPECT_EQ(10, data[0]);
}

extern "C" {
void delay(uint32_t ms) { elapsedMs += ms; }
void busDeviceRegister(const extDevice_t *) { registeredDevices++; }

bool busReadRegisterBuffer(const extDevice_t *dev, uint8_t reg, uint8_t *data, uint8_t length)
{
    if (readFails || dev->busType_u.i2c.address != 0x0D) {
        return false;
    }
    std::memcpy(data, registers.data() + reg, length);
    return true;
}

bool busReadRegisterBufferStart(const extDevice_t *dev, uint8_t reg, uint8_t *data, uint8_t length)
{
    asyncRegisters.push_back(reg);
    return !asyncReadFails && busReadRegisterBuffer(dev, reg, data, length);
}

bool busWriteRegister(const extDevice_t *dev, uint8_t reg, uint8_t value)
{
    if (writeFails || dev->busType_u.i2c.address != 0x0D) {
        return false;
    }
    registers[reg] = value;
    if (reg == 0x0A && value == 0x80 && !preserveConfigOnReset) {
        registers[0x09] = 0;
    }
    return true;
}
}
