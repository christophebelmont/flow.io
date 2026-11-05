#pragma once
/**
 * @file Bmp280Driver.h
 * @brief BMP280 polled temperature/pressure driver.
 */

#include <Adafruit_BMP280.h>
#include <stdint.h>

#include "Core/I2cBus.h"
#include "Modules/IOModule/IODrivers/IODriver.h"

struct Bmp280DriverConfig {
    uint8_t address = BMP280_ADDRESS_ALT;
    uint32_t pollMs = 1000;
};

class Bmp280Driver : public IAnalogSourceDriver {
public:
    Bmp280Driver(const char* driverId, I2CBus* bus, const Bmp280DriverConfig& cfg);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t nowMs) override;
    bool readSample(uint8_t channel, IOAnalogSample& out) const override;

private:
    const char* driverId_ = nullptr;
    I2CBus* bus_ = nullptr;
    Bmp280DriverConfig cfg_{};

    Adafruit_BMP280 bmp_;
    bool ready_ = false;
    bool valid_ = false;
    uint32_t lastPollMs_ = 0;
    uint32_t seq_ = 0;
    float values_[2] = {0.0f, 0.0f};
};
