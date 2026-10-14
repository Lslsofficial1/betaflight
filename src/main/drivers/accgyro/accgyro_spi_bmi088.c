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

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef USE_ACCGYRO_BMI088

#include "common/utils.h"

#include "drivers/accgyro/accgyro.h"
#include "drivers/accgyro/accgyro_spi_bmi088.h"
#include "drivers/bus.h"
#include "drivers/bus_spi.h"
#include "drivers/exti.h"
#include "drivers/io.h"
#include "drivers/nvic.h"
#include "drivers/system.h"
#include "drivers/time.h"

#include "pg/gyrodev.h"

// Bosch BMI088 datasheet BST-BMI088-DS000-19, sections 3, 5 and 6.
// https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi088-ds001.pdf
#define BMI088_MAX_SPI_CLK_HZ 10000000
#define BMI088_GYRO_CHIP_ID 0x0F
#define BMI088_ACC_CHIP_ID 0x1E
#define BMI088_SOFTRESET 0xB6
#define BMI088_EXTI_DETECT_THRESHOLD 5

// This selects the gyro die's interrupt output, not the STM32 EXTI GPIO.
#ifndef BMI088_GYRO_INT_PIN
#define BMI088_GYRO_INT_PIN 3
#endif
#if BMI088_GYRO_INT_PIN != 3 && BMI088_GYRO_INT_PIN != 4
#error BMI088_GYRO_INT_PIN must be 3 or 4
#endif

typedef enum {
    BMI088_GYRO_REG_CHIP_ID = 0x00,
    BMI088_GYRO_REG_X_LSB = 0x02,
    BMI088_GYRO_REG_RANGE = 0x0F,
    BMI088_GYRO_REG_BANDWIDTH = 0x10,
    BMI088_GYRO_REG_LPM1 = 0x11,
    BMI088_GYRO_REG_SOFTRESET = 0x14,
    BMI088_GYRO_REG_INT_CTRL = 0x15,
    BMI088_GYRO_REG_INT_IO_CONF = 0x16,
    BMI088_GYRO_REG_INT_MAP = 0x18,
} bmi088GyroRegister_e;

typedef enum {
    BMI088_ACC_REG_CHIP_ID = 0x00,
    BMI088_ACC_REG_X_LSB = 0x12,
    BMI088_ACC_REG_TEMP_MSB = 0x22,
    BMI088_ACC_REG_CONF = 0x40,
    BMI088_ACC_REG_RANGE = 0x41,
    BMI088_ACC_REG_PWR_CONF = 0x7C,
    BMI088_ACC_REG_PWR_CTRL = 0x7D,
    BMI088_ACC_REG_SOFTRESET = 0x7E,
} bmi088AccRegister_e;

typedef enum {
    BMI088_GYRO_RANGE_2000_DPS = 0x00,
    BMI088_GYRO_BW_2000_HZ_532_HZ = 0x00,
    BMI088_GYRO_BW_2000_HZ_230_HZ = 0x01,
    BMI088_ACC_CONF_800_HZ_NORMAL = 0xAB,
    BMI088_ACC_RANGE_12G = 0x02,
    BMI088_ACC_RANGE_24G = 0x03,
    BMI088_ACC_PWR_ACTIVE = 0x00,
    BMI088_ACC_PWR_ON = 0x04,
} bmi088Config_e;

static void bmi088WriteRegister(const extDevice_t *dev, uint8_t reg, uint8_t value, uint32_t delayMs)
{
    spiWriteReg(dev, reg & 0x7F, value);
    // Also satisfies the minimum interval between successive writes.
    delay(delayMs);
}

static uint8_t bmi088AccReadRegister(const extDevice_t *dev, uint8_t reg)
{
    uint8_t data[2];
    // The accelerometer inserts one dummy byte AFTER the register address.
    spiReadRegBuf(dev, reg | 0x80, data, sizeof(data));
    return data[1];
}

static void bmi088AccActivateSPI(const extDevice_t *dev)
{
    // At power-up the accelerometer is in I2C mode. This first transaction
    // supplies the CS rising edge which selects SPI; discard its result.
    bmi088AccReadRegister(dev, BMI088_ACC_REG_CHIP_ID);
    delay(1);
}

uint8_t bmi088SpiDetect(const extDevice_t *dev)
{
    return spiReadRegMsk(dev, BMI088_GYRO_REG_CHIP_ID) == BMI088_GYRO_CHIP_ID ? BMI_088_SPI : MPU_NONE;
}

bool bmi088SpiAccInitDevice(gyroDev_t *gyro, const gyroDeviceConfig_t *config)
{
    if (!config->accCsnTag || config->accCsnTag == config->csnTag) {
        return false;
    }

    extDevice_t *dev = &gyro->bmi088AccDev;
    dev->busType_u.spi.csnPin = IOGetByTag(config->accCsnTag);
    if (!dev->busType_u.spi.csnPin || !spiSetBusInstance(dev, config->spiBus)) {
        return false;
    }

    // Keep independent device descriptors; never switch the gyro's CS in place.
    IOInit(dev->busType_u.spi.csnPin, OWNER_ACC_CS, RESOURCE_INDEX(config->index));
    IOHi(dev->busType_u.spi.csnPin);
    IOConfigGPIO(dev->busType_u.spi.csnPin, SPI_IO_CS_CFG);
    spiSetClkDivisor(dev, gyro->dev.busType_u.spi.speed);
    spiSetClkPhasePolarity(dev, false); // SPI mode 3, as used by the gyro bus.

    // Polling transactions use stack buffers, which must not be sent through
    // SPI DMA on STM32H7 (the stack can be in non-DMA-accessible DTCM).
    spiDmaEnable(dev, false);
    spiDmaEnable(&gyro->dev, false);
    bmi088AccActivateSPI(dev);
    if (bmi088AccReadRegister(dev, BMI088_ACC_REG_CHIP_ID) != BMI088_ACC_CHIP_ID) {
        return false;
    }

    busDeviceRegister(dev);
    return true;
}

static void bmi088IntExtiHandler(extiCallbackRec_t *cb)
{
    gyroDev_t *gyro = container_of(cb, gyroDev_t, exti);
    const uint32_t nowCycles = getCycleCounter();
    gyro->gyroLastEXTI = nowCycles;
    gyro->gyroSyncEXTI = nowCycles;
    gyro->detectedEXTI++;
}

static bool bmi088IntExtiInit(gyroDev_t *gyro)
{
    if (gyro->mpuIntExtiTag == IO_TAG_NONE) {
        return false;
    }
    const IO_t intIO = IOGetByTag(gyro->mpuIntExtiTag);
    if (!intIO) {
        return false;
    }
    IOInit(intIO, OWNER_GYRO_EXTI, 0);
    EXTIHandlerInit(&gyro->exti, bmi088IntExtiHandler);
    EXTIConfig(intIO, &gyro->exti, NVIC_PRIO_MPU_INT_EXTI, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING);
    EXTIEnable(intIO);
    return true;
}

static void bmi088SpiGyroInit(gyroDev_t *gyro)
{
    extDevice_t *dev = &gyro->dev;
    spiDmaEnable(dev, false);
    spiSetClkDivisor(dev, spiCalculateDivider(BMI088_MAX_SPI_CLK_HZ));
    spiSetClkPhasePolarity(dev, false);

    bmi088WriteRegister(dev, BMI088_GYRO_REG_SOFTRESET, BMI088_SOFTRESET, 30);
    bmi088WriteRegister(dev, BMI088_GYRO_REG_LPM1, 0x00, 30);
    bmi088WriteRegister(dev, BMI088_GYRO_REG_RANGE, BMI088_GYRO_RANGE_2000_DPS, 1);
    // NORMAL follows Betaflight's usual ~250 Hz hardware filtering. The
    // higher-bandwidth OPTION_1 still uses the same 2 kHz output data rate.
    const uint8_t bandwidth = gyro->hardware_lpf == GYRO_HARDWARE_LPF_OPTION_1
        ? BMI088_GYRO_BW_2000_HZ_532_HZ : BMI088_GYRO_BW_2000_HZ_230_HZ;
    bmi088WriteRegister(dev, BMI088_GYRO_REG_BANDWIDTH, bandwidth, 1);
    if (bmi088IntExtiInit(gyro)) {
        // Active-high, push-pull output. SPI reads stay in task context; the
        // interrupt only timestamps DRDY for the scheduler's phase lock.
        const uint8_t intMap = BMI088_GYRO_INT_PIN == 3 ? 0x01 : 0x80;
        const uint8_t intConf = BMI088_GYRO_INT_PIN == 3 ? 0x01 : 0x04;
        bmi088WriteRegister(dev, BMI088_GYRO_REG_INT_IO_CONF, intConf, 1);
        bmi088WriteRegister(dev, BMI088_GYRO_REG_INT_MAP, intMap, 1);
        bmi088WriteRegister(dev, BMI088_GYRO_REG_INT_CTRL, 0x80, 1);
        gyro->gyroModeSPI = GYRO_EXTI_INIT;
    } else {
        bmi088WriteRegister(dev, BMI088_GYRO_REG_INT_CTRL, 0x00, 1);
        bmi088WriteRegister(dev, BMI088_GYRO_REG_INT_MAP, 0x00, 1);
        gyro->gyroModeSPI = GYRO_EXTI_NO_INT;
    }
}

static void bmi088SpiAccInit(accDev_t *acc)
{
    extDevice_t *dev = &acc->gyro->bmi088AccDev;
    spiSetClkDivisor(dev, spiCalculateDivider(BMI088_MAX_SPI_CLK_HZ));
    bmi088WriteRegister(dev, BMI088_ACC_REG_SOFTRESET, BMI088_SOFTRESET, 2);
    bmi088AccActivateSPI(dev);
    bmi088WriteRegister(dev, BMI088_ACC_REG_PWR_CTRL, BMI088_ACC_PWR_ON, 5);
    bmi088WriteRegister(dev, BMI088_ACC_REG_PWR_CONF, BMI088_ACC_PWR_ACTIVE, 5);
    bmi088WriteRegister(dev, BMI088_ACC_REG_CONF, BMI088_ACC_CONF_800_HZ_NORMAL, 1);
    bmi088WriteRegister(dev, BMI088_ACC_REG_RANGE, acc->acc_high_fsr ? BMI088_ACC_RANGE_24G : BMI088_ACC_RANGE_12G, 1);
    acc->acc_1G = 32768 / (acc->acc_high_fsr ? 24 : 12);
}

static bool bmi088GyroRead(gyroDev_t *gyro)
{
    if (gyro->gyroModeSPI == GYRO_EXTI_INIT) {
        // Five edges require only 2.5 ms at 2 kHz. Acc initialisation and the
        // remaining boot sequence provide time to confirm the DRDY wiring.
        gyro->gyroModeSPI = gyro->detectedEXTI >= BMI088_EXTI_DETECT_THRESHOLD ? GYRO_EXTI_INT : GYRO_EXTI_NO_INT;
    }
    // Keep the command and data in one hardware transfer. In particular, do
    // not stop/restart the H7 SPI peripheral between them while CS is low.
    uint8_t txData[7] = { BMI088_GYRO_REG_X_LSB | 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t rxData[7];
    if (!spiReadWriteBufRB(&gyro->dev, txData, rxData, sizeof(rxData))) {
        return false;
    }
    for (unsigned axis = 0; axis < XYZ_AXIS_COUNT; axis++) {
        // Only the command-phase response is discarded; the gyro has no
        // additional dummy byte (unlike the accelerometer).
        gyro->gyroADCRaw[axis] = (int16_t)((uint16_t)rxData[axis * 2 + 2] << 8 | rxData[axis * 2 + 1]);
    }
    gyro->dataReady = true;
    return true;
}

static bool bmi088AccRead(accDev_t *acc)
{
    uint8_t data[7];
    if (!spiReadRegMskBufRB(&acc->gyro->bmi088AccDev, BMI088_ACC_REG_X_LSB, data, sizeof(data))) {
        return false;
    }
    for (unsigned axis = 0; axis < XYZ_AXIS_COUNT; axis++) {
        acc->ADCRaw[axis] = (int16_t)((uint16_t)data[axis * 2 + 2] << 8 | data[axis * 2 + 1]);
    }
    return true;
}

static bool bmi088ReadTemperature(gyroDev_t *gyro, int16_t *temperature)
{
    uint8_t data[3];
    if (!spiReadRegMskBufRB(&gyro->bmi088AccDev, BMI088_ACC_REG_TEMP_MSB, data, sizeof(data))) {
        return false;
    }
    int16_t raw = (uint16_t)data[1] << 3 | data[2] >> 5;
    if (raw == 0x400) { // Datasheet's invalid temperature value (-128 MSB).
        return false;
    }
    if (raw > 1023) {
        raw -= 2048;
    }
    // Combine the 23 C offset before integer division so negative fractional
    // raw values round the final temperature, not the intermediate offset.
    *temperature = (23 * 8 + raw) / 8;
    return true;
}

bool bmi088SpiGyroDetect(gyroDev_t *gyro)
{
    if (gyro->mpuDetectionResult.sensor != BMI_088_SPI) {
        return false;
    }
    gyro->initFn = bmi088SpiGyroInit;
    gyro->readFn = bmi088GyroRead;
    gyro->temperatureFn = bmi088ReadTemperature;
    gyro->scale = GYRO_SCALE_2000DPS;
    return true;
}

bool bmi088SpiAccDetect(accDev_t *acc)
{
    if (acc->mpuDetectionResult.sensor != BMI_088_SPI || !acc->gyro->bmi088AccDev.bus
        || bmi088AccReadRegister(&acc->gyro->bmi088AccDev, BMI088_ACC_REG_CHIP_ID) != BMI088_ACC_CHIP_ID) {
        return false;
    }
    acc->initFn = bmi088SpiAccInit;
    acc->readFn = bmi088AccRead;
    return true;
}

#endif // USE_ACCGYRO_BMI088
