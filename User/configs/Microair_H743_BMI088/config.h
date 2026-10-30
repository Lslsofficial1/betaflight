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
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#define FC_TARGET_MCU   STM32H743
#define SYSTEM_HSE_MHZ  8

#define BOARD_NAME      MICOAIR743V2_BMI088
#define MANUFACTURER_ID MICO

#define USE_ACC
#define USE_GYRO
#define USE_ACCGYRO_BMI088
#define USE_BARO
// SPL06 uses the DPS310-compatible register map in Betaflight 4.5.2.
#define USE_BARO_DPS310
#define USE_MAG
#define USE_MAG_QMC5883
#define USE_SDCARD
#define USE_MAX7456

#define BEEPER_PIN PD15
#define MOTOR1_PIN PE14
#define MOTOR2_PIN PE13
#define MOTOR3_PIN PE11
#define MOTOR4_PIN PE9
#define MOTOR5_PIN PB1
#define MOTOR6_PIN PB0
#define MOTOR7_PIN PD12
#define MOTOR8_PIN PD13
#define LED_STRIP_PIN PD14
#define UART1_TX_PIN PA9
#define UART2_TX_PIN PA2
#define UART3_TX_PIN PD8
#define UART4_TX_PIN PA0
#define UART5_TX_PIN PB6
#define UART6_TX_PIN PC6
#define UART7_TX_PIN PE8
#define UART8_TX_PIN PE1
#define UART1_RX_PIN PA10
#define UART2_RX_PIN PA3
#define UART3_RX_PIN PD9
#define UART4_RX_PIN PA1
#define UART5_RX_PIN PB5
#define UART6_RX_PIN PC7
#define UART7_RX_PIN PE7
#define UART8_RX_PIN PE0
#define I2C1_SCL_PIN PB8
#define I2C2_SCL_PIN PB10
#define I2C1_SDA_PIN PB9
#define I2C2_SDA_PIN PB11
// SPL06 and QMC5883L share the onboard I2C2 bus. Speed is in kHz.
#define I2C2_CLOCKSPEED 400
#define BARO_EOC_PIN PD0 // Wired DRDY; the DPS310 driver polls without EXTI.
#define LED0_PIN PE3
#define LED1_PIN PE2
#define LED2_PIN PE4

// SPI1 remains dedicated to the existing MAX7456 wiring.
#define SPI1_SCK_PIN PA5
#define SPI1_SDI_PIN PA6
#define SPI1_SDO_PIN PA7

// BMI088 gyro and accelerometer share SPI2, but have independent CS pins.
#define SPI2_SCK_PIN PD3
#define SPI2_SDI_PIN PC2
#define SPI2_SDO_PIN PC3
#define GYRO_1_CS_PIN PD5
#define GYRO_1_ACC_CS_PIN PD4
#define GYRO_1_EXTI_PIN PC15
#define BMI088_GYRO_INT_PIN 3 // Gyro INT3 is wired to PC15.
// Stock MicoAir743V2 BMI088 mounting: (x, y, z) -> (-y, -x, -z).
// See the MicoAir743v2 hardware definition referenced in README.md.
#define GYRO_1_ALIGN CW270_DEG_FLIP

#define ADC_VBAT_PIN PC0
#define ADC_CURR_PIN PC1

#define SDIO_CK_PIN PC12
#define SDIO_CMD_PIN PD2
#define SDIO_D0_PIN PC8
#define SDIO_D1_PIN PC9
#define SDIO_D2_PIN PC10
#define SDIO_D3_PIN PC11
#define PINIO1_PIN PE5
#define PINIO2_PIN PE6
#define MAX7456_SPI_CS_PIN PB12

#define TIMER_PIN_MAPPING \
    TIMER_PIN_MAP( 0, MOTOR1_PIN, 1,  0 ) \
    TIMER_PIN_MAP( 1, MOTOR2_PIN, 1,  1 ) \
    TIMER_PIN_MAP( 2, MOTOR3_PIN, 1,  2 ) \
    TIMER_PIN_MAP( 3, MOTOR4_PIN, 1,  3 ) \
    TIMER_PIN_MAP( 4, MOTOR5_PIN, 2,  4 ) \
    TIMER_PIN_MAP( 5, MOTOR6_PIN, 2,  5 ) \
    TIMER_PIN_MAP( 6, MOTOR7_PIN, 1,  6 ) \
    TIMER_PIN_MAP( 7, MOTOR8_PIN, 1,  7 ) \
    TIMER_PIN_MAP( 8, LED_STRIP_PIN, 1, 12 ) \
    TIMER_PIN_MAP( 9, BEEPER_PIN, 1, -1 )

#define ADC1_DMA_OPT 8
#define ADC3_DMA_OPT 9
#define TIMUP1_DMA_OPT 10
#define TIMUP3_DMA_OPT 11

#ifdef USE_OSD_HD
#define MSP_DISPLAYPORT_UART SERIAL_PORT_USART2
#endif
#ifdef USE_GPS
#define GPS_UART             SERIAL_PORT_USART3
#endif
#define MSP_UART             SERIAL_PORT_USART1
#define ESC_SENSOR_UART      SERIAL_PORT_USART7
#define SERIALRX_UART        SERIAL_PORT_USART6

#define MAG_I2C_INSTANCE I2CDEV_2
#define MAG_I2C_ADDRESS 0x0D
#define BARO_I2C_INSTANCE I2CDEV_2
#define DEFAULT_BARO_I2C_ADDRESS 0x77
#define ADC_INSTANCE ADC1
#define DEFAULT_BLACKBOX_DEVICE BLACKBOX_DEVICE_SDCARD
#define DEFAULT_CURRENT_METER_SOURCE CURRENT_METER_ADC
#define DEFAULT_VOLTAGE_METER_SOURCE VOLTAGE_METER_ADC
#define DEFAULT_VOLTAGE_METER_SCALE 211
#define DEFAULT_CURRENT_METER_SCALE 707
#define BEEPER_INVERTED
#define MAX7456_SPI_INSTANCE SPI1
#define PINIO1_BOX 40
#define PINIO2_BOX 41
#define PINIO1_CONFIG 129
#define PINIO2_CONFIG 129
#define USE_SPI_GYRO
#define GYRO_1_SPI_INSTANCE SPI2
#define SDIO_USE_4BIT        1
#define SDIO_DEVICE          SDIODEV_1
