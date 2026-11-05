/**
 * @file I2cBus.cpp
 * @brief Shared primary I2C bus implementation.
 */

#include "Core/I2cBus.h"
#include "Core/I2cGlobalMutex.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::Core)
#include "Core/ModuleLog.h"
#include <Arduino.h>

namespace {
static bool isValidI2cPinLocal(int pin)
{
#if defined(ARDUINO_ARCH_ESP32)
    return (pin >= 0) && digitalPinIsValid((uint8_t)pin);
#else
    return pin >= 0;
#endif
}
}  // namespace

void I2CBus::begin(int sda, int scl, uint32_t frequencyHz)
{
    lastBeginSda_ = sda;
    lastBeginScl_ = scl;
    lastBeginFrequencyHz_ = frequencyHz;

    const bool sdaValid = isValidI2cPinLocal(sda);
    const bool sclValid = isValidI2cPinLocal(scl);
    LOGI("i2c.begin request sda=%d (%s) scl=%d (%s) freq=%lu",
         sda,
         sdaValid ? "valid" : "invalid",
         scl,
         sclValid ? "valid" : "invalid",
         (unsigned long)frequencyHz);

    lastBeginOk_ = Wire.begin(sda, scl, frequencyHz);
    LOGI("i2c.begin result ok=%s", lastBeginOk_ ? "true" : "false");
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
}

bool I2CBus::lock(uint32_t timeoutMs)
{
    if (!mutex_) return false;
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeoutTicks = pdMS_TO_TICKS(timeoutMs);
    if (xSemaphoreTake(mutex_, timeoutTicks) != pdTRUE) return false;

    const TickType_t elapsed = xTaskGetTickCount() - start;
    uint32_t remainMs = 0U;
    if (elapsed < timeoutTicks) {
        remainMs = (uint32_t)pdTICKS_TO_MS(timeoutTicks - elapsed);
    }
    if (!flowI2cGlobalLock(remainMs)) {
        xSemaphoreGive(mutex_);
        return false;
    }
    return true;
}

void I2CBus::unlock()
{
    if (!mutex_) return;
    flowI2cGlobalUnlock();
    xSemaphoreGive(mutex_);
}

bool I2CBus::probe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

bool I2CBus::writeReg(uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    for (uint16_t i = 0; i < len; ++i) Wire.write(data[i]);
    return Wire.endTransmission() == 0;
}

bool I2CBus::readReg(uint8_t addr, uint8_t reg, uint8_t* data, uint16_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;

    uint16_t read = Wire.requestFrom(addr, (uint8_t)len);
    if (read != len) return false;

    for (uint16_t i = 0; i < len; ++i) data[i] = Wire.read();
    return true;
}

bool I2CBus::writeBytes(uint8_t addr, const uint8_t* data, uint16_t len)
{
    Wire.beginTransmission(addr);
    for (uint16_t i = 0; i < len; ++i) Wire.write(data[i]);
    return Wire.endTransmission() == 0;
}

bool I2CBus::readBytes(uint8_t addr, uint8_t* data, uint16_t len)
{
    uint16_t read = Wire.requestFrom(addr, (uint8_t)len);
    if (read != len) return false;

    for (uint16_t i = 0; i < len; ++i) data[i] = Wire.read();
    return true;
}
