#pragma once
/**
 * @file Bme680Driver.h
 * @brief BME680 polled async driver wrapper.
 */

#include <Adafruit_BME680.h>
#include <stdint.h>

#include "Core/I2cBus.h"
#include "Modules/IOModule/IODrivers/IODriver.h"

struct Bme680DriverConfig {
    uint8_t address = BME68X_DEFAULT_ADDRESS;
    uint32_t pollMs = 2000;
};

class Bme680Driver : public IAnalogSourceDriver {
public:
    Bme680Driver(const char* driverId, I2CBus* bus, const Bme680DriverConfig& cfg);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t nowMs) override;
    bool readSample(uint8_t channel, IOAnalogSample& out) const override;

private:
    const char* driverId_ = nullptr;
    I2CBus* bus_ = nullptr;
    Bme680DriverConfig cfg_{};

    Adafruit_BME680 bme_;
    bool ready_ = false;
    bool valid_ = false;
    bool reading_ = false;
    uint32_t lastPollMs_ = 0;
    uint32_t readReadyAtMs_ = 0;
    uint32_t seq_ = 0;
    float values_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};
