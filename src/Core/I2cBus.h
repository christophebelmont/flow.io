#pragma once
/**
 * @file I2cBus.h
 * @brief Shared access wrapper for the primary I2C controller.
 */

#include <stdint.h>
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Process-wide primary I2C bus shared by hardware-facing modules.
 *
 * The bus uses the global Arduino `Wire` controller. `AppContext` owns the
 * single instance and publishes it through `I2cBusService`; modules must not
 * create private instances for the same controller. Callers must hold the bus
 * lock while performing a transaction that can run concurrently with another
 * module.
 */
class I2CBus {
public:
    void begin(int sda, int scl, uint32_t frequencyHz = 100000U);
    bool beginOk() const { return lastBeginOk_; }
    int beginSda() const { return lastBeginSda_; }
    int beginScl() const { return lastBeginScl_; }
    uint32_t beginFrequencyHz() const { return lastBeginFrequencyHz_; }

    bool lock(uint32_t timeoutMs);
    void unlock();

    bool probe(uint8_t addr);

    bool writeReg(uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t len);
    bool readReg(uint8_t addr, uint8_t reg, uint8_t* data, uint16_t len);

    bool writeBytes(uint8_t addr, const uint8_t* data, uint16_t len);
    bool readBytes(uint8_t addr, uint8_t* data, uint16_t len);

    TwoWire* wire() { return &Wire; }

private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool lastBeginOk_ = false;
    int lastBeginSda_ = -1;
    int lastBeginScl_ = -1;
    uint32_t lastBeginFrequencyHz_ = 0;
};
