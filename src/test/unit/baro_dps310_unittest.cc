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
#include "drivers/barometer/barometer.h"
#include "drivers/barometer/barometer_dps310.h"
#include "drivers/time.h"
}

#include "gtest/gtest.h"

namespace {
struct Read {
    uint8_t reg;
    uint8_t length;
};

std::array<uint8_t, 256> registers;
std::vector<Read> reads;
uint8_t connectedAddress;
int failedRegister;
bool busy;
bool asyncReadFails;
unsigned registeredDevices;
unsigned asyncReads;
uint32_t elapsedMs;

class Dps310Test : public testing::Test {
protected:
    busDevice_t bus{};
    baroDev_t baro{};

    void SetUp() override
    {
        registers.fill(0);
        registers[0x0D] = 0x10; // SPL06-compatible ID, also used by DPS310.
        registers[0x08] = 0xC0; // Sensor and calibration coefficients ready.
        registers[0x28] = 0x80; // External temperature coefficients.
        reads.clear();
        connectedAddress = 0x77;
        failedRegister = -1;
        busy = false;
        asyncReadFails = false;
        registeredDevices = 0;
        asyncReads = 0;
        elapsedMs = 0;
        bus.busType = BUS_TYPE_I2C;
        bus.busType_u.i2c.device = I2CDEV_2;
        baro.dev.bus = &bus;
        baro.dev.busType_u.i2c.address = 0x77;
        setCoefficients(50, 10, 100000, 1000, 100);
    }

    void setCoefficients(int c0, int c1, int c00, int c10, int c01)
    {
        const uint32_t t0 = static_cast<uint32_t>(c0) & 0xFFF;
        const uint32_t t1 = static_cast<uint32_t>(c1) & 0xFFF;
        const uint32_t p0 = static_cast<uint32_t>(c00) & 0xFFFFF;
        const uint32_t p1 = static_cast<uint32_t>(c10) & 0xFFFFF;
        registers[0x10] = t0 >> 4;
        registers[0x11] = (t0 << 4) | (t1 >> 8);
        registers[0x12] = t1;
        registers[0x13] = p0 >> 12;
        registers[0x14] = p0 >> 4;
        registers[0x15] = (p0 << 4) | (p1 >> 16);
        registers[0x16] = p1 >> 8;
        registers[0x17] = p1;
        registers[0x18] = static_cast<uint16_t>(c01) >> 8;
        registers[0x19] = c01;
    }

    void setRaw(int32_t pressure, int32_t temperature)
    {
        const int32_t values[] = {pressure, temperature};
        for (unsigned i = 0; i < 2; i++) {
            const uint32_t raw = static_cast<uint32_t>(values[i]) & 0xFFFFFF;
            registers[i * 3] = raw >> 16;
            registers[i * 3 + 1] = raw >> 8;
            registers[i * 3 + 2] = raw;
        }
    }
};
} // namespace

TEST_F(Dps310Test, DetectsSpl06CompatibleDeviceAtExplicitOnboardAddress)
{
    ASSERT_TRUE(baroDPS310Detect(&baro));
    EXPECT_EQ(0x77, baro.dev.busType_u.i2c.address);
    EXPECT_EQ(I2CDEV_2, baro.dev.bus->busType_u.i2c.device);
    EXPECT_EQ(1U, registeredDevices);
    EXPECT_NE(nullptr, baro.read_up);
    EXPECT_NE(nullptr, baro.get_up);
    EXPECT_NE(nullptr, baro.calculate);
    EXPECT_EQ(45000, baro.up_delay);
    EXPECT_GE(elapsedMs, 140U);
}

TEST_F(Dps310Test, DriverDefaultAddressDoesNotFindOnboardDeviceAt77)
{
    baro.dev.busType_u.i2c.address = 0;
    EXPECT_FALSE(baroDPS310Detect(&baro));
    EXPECT_EQ(0, baro.dev.busType_u.i2c.address);
    EXPECT_EQ(0U, registeredDevices);
    EXPECT_EQ(nullptr, baro.calculate);
}

TEST_F(Dps310Test, PreservesExistingDefaultAddressForOtherBoards)
{
    connectedAddress = 0x76;
    baro.dev.busType_u.i2c.address = 0;
    EXPECT_TRUE(baroDPS310Detect(&baro));
    EXPECT_EQ(0x76, baro.dev.busType_u.i2c.address);
}

TEST_F(Dps310Test, RejectsWrongChipIdAndReadFailure)
{
    registers[0x0D] = 0xFF;
    EXPECT_FALSE(baroDPS310Detect(&baro));
    registers[0x0D] = 0x10;
    failedRegister = 0x0D;
    EXPECT_FALSE(baroDPS310Detect(&baro));
    EXPECT_EQ(0U, registeredDevices);
}

TEST_F(Dps310Test, RequiresSensorAndCoefficientReadiness)
{
    registers[0x08] = 0x40;
    EXPECT_FALSE(baroDPS310Detect(&baro));
    registers[0x08] = 0x80;
    EXPECT_FALSE(baroDPS310Detect(&baro));
    EXPECT_EQ(0U, registeredDevices);
}

TEST_F(Dps310Test, ReadsCalibrationInTwoNineByteChunks)
{
    ASSERT_TRUE(baroDPS310Detect(&baro));
    unsigned coefficientReads = 0;
    for (const auto &read : reads) {
        if (read.reg >= 0x10 && read.reg < 0x22) {
            EXPECT_EQ(9, read.length);
            EXPECT_EQ(0x10 + coefficientReads * 9, read.reg);
            coefficientReads++;
        }
    }
    EXPECT_EQ(2U, coefficientReads);
}

TEST_F(Dps310Test, RejectsIncompleteCalibrationRead)
{
    failedRegister = 0x19;
    EXPECT_FALSE(baroDPS310Detect(&baro));
    EXPECT_EQ(0U, registeredDevices);
    EXPECT_EQ(nullptr, baro.calculate);
}

TEST_F(Dps310Test, ConfiguresContinuous32HzMeasurementsAndOversampling)
{
    ASSERT_TRUE(baroDPS310Detect(&baro));
    EXPECT_EQ(0x09, registers[0x0C]);
    EXPECT_EQ(0x54, registers[0x06]);
    EXPECT_EQ(0xD4, registers[0x07]);
    EXPECT_EQ(0x0C, registers[0x09]);
    EXPECT_EQ(0x07, registers[0x08] & 0x07);
}

TEST_F(Dps310Test, CompensatesPositiveAndNegativeRawSamples)
{
    ASSERT_TRUE(baroDPS310Detect(&baro));
    int32_t pressure = 0;
    int32_t temperature = 0;
    setRaw(253952, 253952);
    ASSERT_TRUE(baro.read_up(&baro));
    ASSERT_TRUE(baro.get_up(&baro));
    baro.calculate(&pressure, &temperature);
    EXPECT_EQ(101100, pressure);
    EXPECT_EQ(3500, temperature);
    setRaw(-253952, -253952);
    ASSERT_TRUE(baro.read_up(&baro));
    ASSERT_TRUE(baro.get_up(&baro));
    baro.calculate(&pressure, &temperature);
    EXPECT_EQ(98900, pressure);
    EXPECT_EQ(1500, temperature);
}

TEST_F(Dps310Test, DecodesSignedCalibrationCoefficients)
{
    setCoefficients(-40, -10, 100000, -1000, -100);
    ASSERT_TRUE(baroDPS310Detect(&baro));
    setRaw(253952, 253952);
    ASSERT_TRUE(baro.read_up(&baro));
    ASSERT_TRUE(baro.get_up(&baro));
    int32_t pressure = 0;
    int32_t temperature = 0;
    baro.calculate(&pressure, &temperature);
    EXPECT_EQ(98900, pressure);
    EXPECT_EQ(-3000, temperature);
}

TEST_F(Dps310Test, DoesNotStartReadWhileBusBusyAndReportsTransferFailure)
{
    ASSERT_TRUE(baroDPS310Detect(&baro));
    busy = true;
    EXPECT_FALSE(baro.read_up(&baro));
    EXPECT_EQ(0U, asyncReads);
    busy = false;
    asyncReadFails = true;
    EXPECT_FALSE(baro.read_up(&baro));
    EXPECT_EQ(1U, asyncReads);
}

extern "C" {
void delay(uint32_t ms) { elapsedMs += ms; }
void busDeviceRegister(const extDevice_t *) { registeredDevices++; }
bool busBusy(const extDevice_t *, bool *) { return busy; }

bool busReadRegisterBuffer(const extDevice_t *dev, uint8_t reg, uint8_t *data, uint8_t length)
{
    reads.push_back({reg, length});
    if (dev->busType_u.i2c.address != connectedAddress || reg == failedRegister) {
        return false;
    }
    std::memcpy(data, registers.data() + reg, length);
    return true;
}

uint8_t busReadRegister(const extDevice_t *dev, uint8_t reg)
{
    uint8_t value = 0;
    busReadRegisterBuffer(dev, reg, &value, 1);
    return value;
}

bool busReadRegisterBufferStart(const extDevice_t *dev, uint8_t reg, uint8_t *data, uint8_t length)
{
    asyncReads++;
    return !asyncReadFails && busReadRegisterBuffer(dev, reg, data, length);
}

bool busWriteRegister(const extDevice_t *dev, uint8_t reg, uint8_t value)
{
    if (dev->busType_u.i2c.address != connectedAddress) {
        return false;
    }
    registers[reg] = value;
    return true;
}
}
