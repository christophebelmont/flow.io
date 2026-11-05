#pragma once
/**
 * @file Sht40Driver.h
 * @brief SHT40 polled temperature/humidity driver.
 */

#include <Adafruit_SHT4x.h>
#include <stdint.h>

#include "Core/I2cBus.h"
#include "Modules/IOModule/IODrivers/IODriver.h"

struct Sht40DriverConfig {
    uint8_t address = SHT4x_DEFAULT_ADDR;
    uint32_t pollMs = 2000;
};

class Sht40Driver : public IAnalogSourceDriver {
public:
    Sht40Driver(const char* driverId, I2CBus* bus, const Sht40DriverConfig& cfg);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t nowMs) override;
    bool readSample(uint8_t channel, IOAnalogSample& out) const override;

private:
    const char* driverId_ = nullptr;
    I2CBus* bus_ = nullptr;
    Sht40DriverConfig cfg_{};

    Adafruit_SHT4x sht_;
    bool ready_ = false;
    bool valid_ = false;
    uint32_t lastPollMs_ = 0;
    uint32_t seq_ = 0;
    float values_[2] = {0.0f, 0.0f};
};
