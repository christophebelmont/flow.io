#pragma once
/**
 * @file Ina226Driver.h
 * @brief INA226 polled current/power monitor driver.
 */

#include <INA226_WE.h>
#include <stdint.h>

#include "Core/I2cBus.h"
#include "Modules/IOModule/IODrivers/IODriver.h"

struct Ina226DriverConfig {
    uint8_t address = 0x40;
    uint32_t pollMs = 500;
    float shuntOhms = 0.1f;
};

class Ina226Driver : public IAnalogSourceDriver {
public:
    Ina226Driver(const char* driverId, I2CBus* bus, const Ina226DriverConfig& cfg);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t nowMs) override;
    bool readSample(uint8_t channel, IOAnalogSample& out) const override;

private:
    const char* driverId_ = nullptr;
    I2CBus* bus_ = nullptr;
    Ina226DriverConfig cfg_{};

    INA226_WE ina_;
    bool ready_ = false;
    bool valid_ = false;
    uint32_t lastPollMs_ = 0;
    uint32_t seq_ = 0;
    float values_[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};
