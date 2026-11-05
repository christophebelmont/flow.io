#pragma once
/**
 * @file Pcf8574Driver.h
 * @brief PCF8574 output-only driver.
 */

#include <stdint.h>
#include "Core/I2cBus.h"
#include "Modules/IOModule/IODrivers/IODriver.h"

class Pcf8574Driver : public IMaskOutputDriver {
public:
    Pcf8574Driver(const char* driverId, I2CBus* bus, uint8_t address);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t) override {}

    bool writeMask(uint8_t mask) override;
    bool readMask(uint8_t& mask) const override;
    bool writePin(uint8_t pin, bool on);
    bool readShadow(uint8_t pin, bool& on) const;

private:
    bool flush_();

    const char* driverId_ = nullptr;
    I2CBus* bus_ = nullptr;
    uint8_t address_ = 0x20;
    uint8_t state_ = 0xFF;
};
