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

#pragma once

#include "drivers/accgyro/accgyro.h"

struct gyroDeviceConfig_s;

uint8_t bmi088SpiDetect(const extDevice_t *dev);
bool bmi088SpiAccInitDevice(gyroDev_t *gyro, const struct gyroDeviceConfig_s *config);
bool bmi088SpiGyroDetect(gyroDev_t *gyro);
bool bmi088SpiAccDetect(accDev_t *acc);
