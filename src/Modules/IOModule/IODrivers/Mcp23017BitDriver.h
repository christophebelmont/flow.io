#pragma once
/**
 * @file Mcp23017BitDriver.h
 * @brief Logical single-bit digital adapter on top of Mcp23017Driver.
 */

#include "Modules/IOModule/IODrivers/Mcp23017Driver.h"

class Mcp23017BitDriver : public IDigitalPinDriver {
public:
    Mcp23017BitDriver(const char* driverId,
                      Mcp23017Driver* parent,
                      uint8_t bit,
                      bool activeHigh,
                      bool output,
                      uint8_t inputPullMode = 0);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t) override {}

    bool write(bool on) override;
    bool read(bool& on) const override;

private:
    const char* driverId_ = nullptr;
    Mcp23017Driver* parent_ = nullptr;
    uint8_t bit_ = 0;
    bool activeHigh_ = true;
    bool output_ = true;
    uint8_t inputPullMode_ = 0;
};
