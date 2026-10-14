/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Betaflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Betaflight. If not, see <http://www.gnu.org/licenses/>.
 */

#include <array>
#include <cstring>
#include <vector>

extern "C" {
#include "platform.h"
#include "drivers/accgyro/accgyro_spi_bmi088.h"
#include "drivers/accgyro/gyro_sync.h"
#include "drivers/bus_spi.h"
#include "drivers/io.h"
#include "drivers/system.h"
#include "drivers/time.h"
#include "pg/gyrodev.h"
}

#include "gtest/gtest.h"

namespace {
constexpr ioTag_t gyroTag = 0x45;
constexpr ioTag_t accTag = 0x44;
constexpr ioTag_t drdyTag = 0x3F;

struct Operation {
    char type;
    IO_t cs;
    uint8_t reg;
    uint32_t value;
};

busDevice_t spiBus;
std::array<uint8_t, 128> gyroRegisters;
std::array<uint8_t, 128> accRegisters;
std::vector<Operation> operations;
std::vector<std::vector<uint8_t>> gyroTransfers;
bool accSpiActive;
bool mockBusBusy;
bool invalidPin;
unsigned registeredDevices;
unsigned accOwners;
unsigned extiOwners;
bool extiEnabled;
uint32_t cycleCounter;

IO_t pin(ioTag_t tag)
{
    return reinterpret_cast<IO_t>(static_cast<uintptr_t>(tag));
}

bool isAcc(const extDevice_t *dev)
{
    return dev->busType_u.spi.csnPin == pin(accTag);
}

class Bmi088Test : public testing::Test {
protected:
    gyroDev_t gyro{};
    accDev_t acc{};
    gyroDeviceConfig_t config{};

    void SetUp() override
    {
        spiBus = {};
        spiBus.busType = BUS_TYPE_SPI;
        spiBus.deviceCount = 1;
        gyroRegisters.fill(0);
        accRegisters.fill(0);
        gyroRegisters[0] = 0x0F;
        accRegisters[0] = 0x1E;
        accSpiActive = false;
        mockBusBusy = false;
        invalidPin = false;
        registeredDevices = 0;
        accOwners = 0;
        extiOwners = 0;
        extiEnabled = false;
        cycleCounter = 1000;
        operations.clear();
        gyroTransfers.clear();
        config.busType = BUS_TYPE_SPI;
        config.spiBus = 2;
        config.csnTag = gyroTag;
        config.accCsnTag = accTag;
        gyro.dev.bus = &spiBus;
        gyro.dev.busType_u.spi.csnPin = pin(gyroTag);
        gyro.dev.busType_u.spi.speed = 128;
        gyro.dev.useDMA = true;
        gyro.mpuDetectionResult.sensor = BMI_088_SPI;
        acc.gyro = &gyro;
        acc.mpuDetectionResult.sensor = BMI_088_SPI;
    }

    void prepare()
    {
        ASSERT_TRUE(bmi088SpiAccInitDevice(&gyro, &config));
        ASSERT_TRUE(bmi088SpiGyroDetect(&gyro));
        ASSERT_TRUE(bmi088SpiAccDetect(&acc));
    }

    void setAxes(std::array<uint8_t, 128> &registers, unsigned start, int16_t x, int16_t y, int16_t z)
    {
        const int16_t values[] = {x, y, z};
        for (unsigned axis = 0; axis < 3; axis++) {
            registers[start + 2 * axis] = static_cast<uint16_t>(values[axis]) & 0xFF;
            registers[start + 2 * axis + 1] = static_cast<uint16_t>(values[axis]) >> 8;
        }
    }
};
} // namespace

TEST_F(Bmi088Test, DetectsGyroChipIdWithoutDummyByte)
{
    EXPECT_EQ(BMI_088_SPI, bmi088SpiDetect(&gyro.dev));
    ASSERT_EQ(1U, operations.size());
    EXPECT_EQ(pin(gyroTag), operations.back().cs);
    EXPECT_EQ(0x80, operations.back().reg);
    EXPECT_EQ(1U, operations.back().value);
    gyroRegisters[0] = 0x24;
    EXPECT_EQ(MPU_NONE, bmi088SpiDetect(&gyro.dev));
}

TEST_F(Bmi088Test, ActivatesAccelerometerSpiAndChecksSecondByte)
{
    EXPECT_TRUE(bmi088SpiAccInitDevice(&gyro, &config));
    EXPECT_TRUE(accSpiActive);
    EXPECT_EQ(1U, registeredDevices);
    EXPECT_EQ(1U, accOwners);
    EXPECT_EQ(2, spiBus.deviceCount);
    EXPECT_EQ(gyro.dev.bus, gyro.bmi088AccDev.bus);
    EXPECT_NE(gyro.dev.busType_u.spi.csnPin, gyro.bmi088AccDev.busType_u.spi.csnPin);
    EXPECT_EQ(pin(gyroTag), gyro.dev.busType_u.spi.csnPin);
    EXPECT_FALSE(gyro.dev.useDMA);
    EXPECT_FALSE(gyro.bmi088AccDev.useDMA);
    EXPECT_EQ(gyro.dev.busType_u.spi.speed, gyro.bmi088AccDev.busType_u.spi.speed);
    // High -> output configuration -> dummy read -> wait -> real chip-ID read.
    ASSERT_EQ(5U, operations.size());
    EXPECT_EQ('H', operations[0].type);
    EXPECT_EQ('C', operations[1].type);
    EXPECT_EQ('R', operations[2].type);
    EXPECT_EQ(2U, operations[2].value);
    EXPECT_EQ('D', operations[3].type);
    EXPECT_EQ('R', operations[4].type);
    EXPECT_EQ(2U, operations[4].value);
}

TEST_F(Bmi088Test, RejectsIncorrectAccelerometerChipId)
{
    accRegisters[0] = 0xFA;
    EXPECT_FALSE(bmi088SpiAccInitDevice(&gyro, &config));
    EXPECT_EQ(0U, registeredDevices);
}

TEST_F(Bmi088Test, RejectsMissingOrSharedChipSelect)
{
    config.accCsnTag = IO_TAG_NONE;
    EXPECT_FALSE(bmi088SpiAccInitDevice(&gyro, &config));
    config.accCsnTag = config.csnTag;
    EXPECT_FALSE(bmi088SpiAccInitDevice(&gyro, &config));
    EXPECT_TRUE(operations.empty());
}

TEST_F(Bmi088Test, RejectsInvalidBusOrPin)
{
    config.spiBus = 0;
    EXPECT_FALSE(bmi088SpiAccInitDevice(&gyro, &config));
    config.spiBus = 2;
    invalidPin = true;
    EXPECT_FALSE(bmi088SpiAccInitDevice(&gyro, &config));
    EXPECT_TRUE(operations.empty());
}

TEST_F(Bmi088Test, RejectsOtherDetectedSensors)
{
    gyro.mpuDetectionResult.sensor = BMI_270_SPI;
    acc.mpuDetectionResult.sensor = BMI_270_SPI;
    EXPECT_FALSE(bmi088SpiGyroDetect(&gyro));
    EXPECT_FALSE(bmi088SpiAccDetect(&acc));
    EXPECT_EQ(nullptr, gyro.initFn);
    EXPECT_EQ(nullptr, acc.initFn);
}

TEST_F(Bmi088Test, RejectsAccelerometerWithoutDeviceOrWithWrongId)
{
    EXPECT_FALSE(bmi088SpiAccDetect(&acc));
    ASSERT_TRUE(bmi088SpiAccInitDevice(&gyro, &config));
    accRegisters[0] = 0;
    EXPECT_FALSE(bmi088SpiAccDetect(&acc));
}

TEST_F(Bmi088Test, SetsTwoKilohertzSampleRate)
{
    EXPECT_EQ(2000, gyroSetSampleRate(&gyro));
    EXPECT_EQ(GYRO_RATE_2_kHz, gyro.gyroRateKHz);
    EXPECT_EQ(800, gyro.accSampleRateHz);
    EXPECT_EQ(0, gyro.mpuDividerDrops);
    gyro.mpuDetectionResult.sensor = BMI_270_SPI;
    EXPECT_EQ(3200, gyroSetSampleRate(&gyro));
    EXPECT_EQ(800, gyro.accSampleRateHz);
}

TEST_F(Bmi088Test, GyroInitUsesResetDelayRangeAndPolling)
{
    prepare();
    operations.clear();
    gyro.gyroSampleRateHz = gyroSetSampleRate(&gyro);
    gyro.initFn(&gyro);
    EXPECT_EQ(GYRO_EXTI_NO_INT, gyro.gyroModeSPI);
    EXPECT_EQ(0U, gyro.detectedEXTI);
    EXPECT_EQ(16, gyro.dev.busType_u.spi.speed);
    EXPECT_FLOAT_EQ(GYRO_SCALE_2000DPS, gyro.scale);
    EXPECT_EQ(0, gyroRegisters[0x0F]);
    EXPECT_EQ(1, gyroRegisters[0x10]);
    EXPECT_EQ(0, gyroRegisters[0x15]);
    EXPECT_EQ(0, gyroRegisters[0x18]);
    ASSERT_GE(operations.size(), 2U);
    EXPECT_EQ('W', operations[0].type);
    EXPECT_EQ(0x14, operations[0].reg);
    EXPECT_EQ(0xB6U, operations[0].value);
    EXPECT_EQ('D', operations[1].type);
    EXPECT_GE(operations[1].value, 30U);
}

TEST_F(Bmi088Test, AlternateGyroBandwidthKeepsTwoKilohertzOdr)
{
    prepare();
    gyro.hardware_lpf = GYRO_HARDWARE_LPF_OPTION_1;
    gyro.initFn(&gyro);
    EXPECT_EQ(0, gyroRegisters[0x10]);
    EXPECT_EQ(2000, gyroSetSampleRate(&gyro));
}

TEST_F(Bmi088Test, AccInitRestoresSpiPowerRateAndTwelveGScale)
{
    prepare();
    operations.clear();
    acc.initFn(&acc);
    EXPECT_TRUE(accSpiActive);
    EXPECT_EQ(0x04, accRegisters[0x7D]);
    EXPECT_EQ(0, accRegisters[0x7C]);
    EXPECT_EQ(0xAB, accRegisters[0x40]);
    EXPECT_EQ(2, accRegisters[0x41]);
    EXPECT_EQ(32768 / 12, acc.acc_1G);
    EXPECT_EQ(16, gyro.bmi088AccDev.busType_u.spi.speed);
    ASSERT_GE(operations.size(), 4U);
    EXPECT_EQ(0x7E, operations[0].reg);
    EXPECT_EQ(0xB6U, operations[0].value);
    EXPECT_EQ('D', operations[1].type);
    EXPECT_GE(operations[1].value, 1U);
    EXPECT_EQ('R', operations[2].type); // SPI reactivation after reset.
    EXPECT_EQ(2U, operations[2].value);
}

TEST_F(Bmi088Test, AccHighFsrUsesTwentyFourGScale)
{
    prepare();
    acc.acc_high_fsr = true;
    acc.initFn(&acc);
    EXPECT_EQ(3, accRegisters[0x41]);
    EXPECT_EQ(32768 / 24, acc.acc_1G);
}

TEST_F(Bmi088Test, GyroReadsSignedLittleEndianAxesFromGyroCs)
{
    prepare();
    setAxes(gyroRegisters, 2, 0x1234, -32768, 32767);
    EXPECT_TRUE(gyro.readFn(&gyro));
    EXPECT_EQ(0x1234, gyro.gyroADCRaw[0]);
    EXPECT_EQ(-32768, gyro.gyroADCRaw[1]);
    EXPECT_EQ(32767, gyro.gyroADCRaw[2]);
    EXPECT_TRUE(gyro.dataReady);
    EXPECT_EQ(pin(gyroTag), operations.back().cs);
    EXPECT_EQ(0x82, operations.back().reg);
    EXPECT_EQ(6U, operations.back().value);
}

TEST_F(Bmi088Test, AccReadsSignedAxesAfterDummyByteFromAccCs)
{
    prepare();
    setAxes(accRegisters, 0x12, -1, 0x1234, -32768);
    EXPECT_TRUE(acc.readFn(&acc));
    EXPECT_EQ(-1, acc.ADCRaw[0]);
    EXPECT_EQ(0x1234, acc.ADCRaw[1]);
    EXPECT_EQ(-32768, acc.ADCRaw[2]);
    EXPECT_EQ(pin(accTag), operations.back().cs);
    EXPECT_EQ(0x92, operations.back().reg);
    EXPECT_EQ(7U, operations.back().value);
    EXPECT_EQ(pin(gyroTag), gyro.dev.busType_u.spi.csnPin);
}

TEST_F(Bmi088Test, GyroBurstKeepsCommandAndSixAxisBytesInOneTransfer)
{
    prepare();
    setAxes(gyroRegisters, 2, 0x1234, -32768, 32767);
    ASSERT_TRUE(gyro.readFn(&gyro));
    ASSERT_EQ(1U, gyroTransfers.size());
    ASSERT_EQ(7U, gyroTransfers[0].size());
    EXPECT_EQ(0x82, gyroTransfers[0][0]);
    EXPECT_EQ(0x1234, gyro.gyroADCRaw[0]);
    EXPECT_EQ(-32768, gyro.gyroADCRaw[1]);
    EXPECT_EQ(32767, gyro.gyroADCRaw[2]);
}

TEST_F(Bmi088Test, BusyBusDoesNotPublishOrReplaceSamples)
{
    prepare();
    gyro.gyroADCRaw[0] = 42;
    acc.ADCRaw[0] = 43;
    int16_t temperature = 44;
    mockBusBusy = true;
    EXPECT_FALSE(gyro.readFn(&gyro));
    EXPECT_FALSE(acc.readFn(&acc));
    EXPECT_FALSE(gyro.temperatureFn(&gyro, &temperature));
    EXPECT_EQ(42, gyro.gyroADCRaw[0]);
    EXPECT_EQ(43, acc.ADCRaw[0]);
    EXPECT_EQ(44, temperature);
    EXPECT_FALSE(gyro.dataReady);
}

TEST_F(Bmi088Test, ReadsSignedTemperatureFromAccelerometerCs)
{
    prepare();
    int16_t temperature = 0;
    accRegisters[0x22] = 0x3E;
    accRegisters[0x23] = 0;
    EXPECT_TRUE(gyro.temperatureFn(&gyro, &temperature));
    EXPECT_EQ(85, temperature);
    accRegisters[0x22] = 0xC1;
    EXPECT_TRUE(gyro.temperatureFn(&gyro, &temperature));
    EXPECT_EQ(-40, temperature);
    EXPECT_EQ(pin(accTag), operations.back().cs);
    EXPECT_EQ(0xA2, operations.back().reg);
    EXPECT_EQ(3U, operations.back().value);
}

TEST_F(Bmi088Test, InvalidTemperatureKeepsPreviousValue)
{
    prepare();
    int16_t temperature = 42;
    accRegisters[0x22] = 0x80;
    accRegisters[0x23] = 0;
    EXPECT_FALSE(gyro.temperatureFn(&gyro, &temperature));
    EXPECT_EQ(42, temperature);
}

TEST_F(Bmi088Test, TemperatureRoundsAfterAddingOffset)
{
    prepare();
    const int16_t rawValues[] = { 7, -7, -185, -193 };
    const int16_t expected[] = { 23, 22, 0, -1 };
    for (unsigned i = 0; i < sizeof(rawValues) / sizeof(rawValues[0]); i++) {
        const uint16_t encoded = static_cast<uint16_t>(rawValues[i]) & 0x7FF;
        accRegisters[0x22] = encoded >> 3;
        accRegisters[0x23] = (encoded & 0x07) << 5;
        int16_t temperature = 42;
        ASSERT_TRUE(gyro.temperatureFn(&gyro, &temperature));
        EXPECT_EQ(expected[i], temperature);
    }
}

TEST_F(Bmi088Test, DrdyConfiguresRisingEdgeAndSelectedGyroInterrupt)
{
    prepare();
    gyro.mpuIntExtiTag = drdyTag;
    gyro.initFn(&gyro);
    EXPECT_TRUE(extiEnabled);
    EXPECT_EQ(1U, extiOwners);
    EXPECT_EQ(GYRO_EXTI_INIT, gyro.gyroModeSPI);
    const bool int4 = BMI088_GYRO_INT_PIN == 4;
    EXPECT_EQ(int4 ? 0x80 : 0x01, gyroRegisters[0x18]);
    EXPECT_EQ(int4 ? 0x04 : 0x01, gyroRegisters[0x16]);
    EXPECT_EQ(0x80, gyroRegisters[0x15]);
    ASSERT_NE(nullptr, gyro.exti.fn);
    const size_t operationsBeforeIrq = operations.size();
    for (unsigned i = 0; i < 5; i++) {
        cycleCounter += 240000; // 500 us on a 480 MHz H743.
        gyro.exti.fn(&gyro.exti);
    }
    EXPECT_EQ(5U, gyro.detectedEXTI);
    EXPECT_EQ(cycleCounter, gyro.gyroLastEXTI);
    EXPECT_EQ(cycleCounter, gyro.gyroSyncEXTI);
    EXPECT_EQ(operationsBeforeIrq, operations.size()); // No SPI inside the ISR.
    EXPECT_TRUE(gyro.readFn(&gyro));
    EXPECT_EQ(GYRO_EXTI_INT, gyro.gyroModeSPI);
    EXPECT_FALSE(gyro.dev.useDMA);
}

TEST_F(Bmi088Test, MissingDrdyEdgesFallBackToPolling)
{
    prepare();
    gyro.mpuIntExtiTag = drdyTag;
    gyro.initFn(&gyro);
    ASSERT_EQ(GYRO_EXTI_INIT, gyro.gyroModeSPI);
    EXPECT_TRUE(gyro.readFn(&gyro));
    EXPECT_EQ(GYRO_EXTI_NO_INT, gyro.gyroModeSPI);
}

TEST_F(Bmi088Test, InvalidDrdyPinUsesPollingWithoutExti)
{
    prepare();
    gyro.mpuIntExtiTag = drdyTag;
    invalidPin = true;
    gyro.initFn(&gyro);
    EXPECT_EQ(GYRO_EXTI_NO_INT, gyro.gyroModeSPI);
    EXPECT_FALSE(extiEnabled);
    EXPECT_EQ(0U, extiOwners);
    EXPECT_EQ(0, gyroRegisters[0x15]);
}

// SPI/GPIO mocks exercise the production driver and enforce the dual-die
// protocol. These tests do not emulate electrical timing or physical wiring.
extern "C" {
void delay(timeMs_t ms)
{
    operations.push_back({'D', nullptr, 0, ms});
}

IO_t IOGetByTag(ioTag_t tag)
{
    return invalidPin ? IO_NONE : pin(tag);
}

void IOInit(IO_t io, resourceOwner_e owner, uint8_t index)
{
    if (owner == OWNER_ACC_CS) {
        EXPECT_EQ(pin(accTag), io);
        EXPECT_EQ(1, index);
        accOwners++;
    } else {
        EXPECT_EQ(OWNER_GYRO_EXTI, owner);
        EXPECT_EQ(pin(drdyTag), io);
        EXPECT_EQ(0, index);
        extiOwners++;
    }
}

uint32_t getCycleCounter(void)
{
    return cycleCounter;
}

void EXTIHandlerInit(extiCallbackRec_t *cb, extiHandlerCallback *fn)
{
    cb->fn = fn;
}

void EXTIConfig(IO_t io, extiCallbackRec_t *cb, int irqPriority, ioConfig_t cfg, extiTrigger_t trigger)
{
    EXPECT_EQ(pin(drdyTag), io);
    EXPECT_NE(nullptr, cb->fn);
    EXPECT_GT(irqPriority, 0);
    EXPECT_EQ(IOCFG_IN_FLOATING, cfg);
    EXPECT_EQ(BETAFLIGHT_EXTI_TRIGGER_RISING, trigger);
}

void EXTIEnable(IO_t io)
{
    EXPECT_EQ(pin(drdyTag), io);
    extiEnabled = true;
}

void IOHi(IO_t io)
{
    operations.push_back({'H', io, 0, 0});
}

void IOConfigGPIO(IO_t io, ioConfig_t cfg)
{
    operations.push_back({'C', io, 0, cfg});
}

bool spiSetBusInstance(extDevice_t *dev, uint32_t device)
{
    if (device != 2) {
        return false;
    }
    dev->bus = &spiBus;
    dev->useDMA = true;
    spiBus.deviceCount++;
    return true;
}

void busDeviceRegister(const extDevice_t *dev)
{
    EXPECT_EQ(pin(accTag), dev->busType_u.spi.csnPin);
    registeredDevices++;
}

void spiDmaEnable(const extDevice_t *dev, bool enable)
{
    const_cast<extDevice_t *>(dev)->useDMA = enable;
}

uint16_t spiCalculateDivider(uint32_t frequency)
{
    EXPECT_EQ(10000000U, frequency);
    return 16;
}

void spiSetClkDivisor(const extDevice_t *dev, uint16_t divisor)
{
    const_cast<extDevice_t *>(dev)->busType_u.spi.speed = divisor;
}

void spiSetClkPhasePolarity(const extDevice_t *dev, bool leadingEdge)
{
    EXPECT_FALSE(leadingEdge);
    const_cast<extDevice_t *>(dev)->busType_u.spi.leadingEdge = leadingEdge;
}

void spiReadRegBuf(const extDevice_t *dev, uint8_t reg, uint8_t *data, uint8_t length)
{
    EXPECT_NE(0, reg & 0x80);
    operations.push_back({'R', dev->busType_u.spi.csnPin, reg, length});
    const unsigned address = reg & 0x7F;
    if (isAcc(dev)) {
        // The first transaction after power-up/reset yields invalid data.
        if (!accSpiActive) {
            std::memset(data, 0, length);
            accSpiActive = true;
            return;
        }
        data[0] = 0xD5; // Not the chip ID or axis data: must be discarded.
        for (unsigned i = 1; i < length; i++) {
            data[i] = accRegisters[address + i - 1];
        }
    } else {
        EXPECT_EQ(pin(gyroTag), dev->busType_u.spi.csnPin);
        for (unsigned i = 0; i < length; i++) {
            data[i] = gyroRegisters[address + i];
        }
    }
}

uint8_t spiReadRegMsk(const extDevice_t *dev, uint8_t reg)
{
    uint8_t data;
    spiReadRegBuf(dev, reg | 0x80, &data, 1);
    return data;
}

bool spiReadRegMskBufRB(const extDevice_t *dev, uint8_t reg, uint8_t *data, uint8_t length)
{
    if (mockBusBusy) {
        return false;
    }
    EXPECT_FALSE(dev->useDMA);
    spiReadRegBuf(dev, reg | 0x80, data, length);
    return true;
}

bool spiReadWriteBufRB(const extDevice_t *dev, uint8_t *txData, uint8_t *rxData, int length)
{
    if (mockBusBusy) {
        return false;
    }
    EXPECT_FALSE(dev->useDMA);
    EXPECT_FALSE(isAcc(dev));
    EXPECT_EQ(7, length);
    if (length != 7) {
        return false;
    }
    gyroTransfers.emplace_back(txData, txData + length);
    rxData[0] = 0xA5; // Command-phase response is not an axis byte.
    spiReadRegBuf(dev, txData[0], rxData + 1, length - 1);
    return true;
}

void spiWriteReg(const extDevice_t *dev, uint8_t reg, uint8_t value)
{
    EXPECT_EQ(0, reg & 0x80);
    operations.push_back({'W', dev->busType_u.spi.csnPin, reg, value});
    auto &registers = isAcc(dev) ? accRegisters : gyroRegisters;
    if (value == 0xB6 && reg == (isAcc(dev) ? 0x7E : 0x14)) {
        registers.fill(0);
        registers[0] = isAcc(dev) ? 0x1E : 0x0F;
        if (isAcc(dev)) {
            accSpiActive = false;
        }
    } else {
        registers[reg] = value;
    }
}
} // extern "C"
