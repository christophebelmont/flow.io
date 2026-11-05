#pragma once
/**
 * @file Pcf8574LedPanelDriver.h
 * @brief Dedicated active-low front LED panel on PCF8574A address 0x3C.
 */

#include <stdint.h>

class I2CBus;

class Pcf8574LedPanelDriver {
public:
    static constexpr uint8_t Address = 0x3CU;

    bool begin(I2CBus& bus);
    bool setEnabled(bool enabled);
    bool writeLogicalMask(uint8_t mask);

    bool isReady() const { return ready_; }
    bool isEnabled() const { return enabled_; }
    uint8_t logicalMask() const { return logicalMask_; }

private:
    bool writePhysicalMask_(uint8_t mask);

    I2CBus* bus_ = nullptr;
    uint8_t logicalMask_ = 0U;
    bool ready_ = false;
    bool enabled_ = false;
};
