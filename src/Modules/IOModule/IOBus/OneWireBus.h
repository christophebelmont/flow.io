#pragma once
/**
 * @file OneWireBus.h
 * @brief OneWire + DallasTemperature wrapper.
 */

#include <stdint.h>
#include <stddef.h>
#include <DallasTemperature.h>
#include <OneWire.h>

class OneWireBus {
public:
    explicit OneWireBus(int pin);

    void begin();
    void request();
    void setWaitForConversion(bool enabled);
    bool getAddress(uint8_t index, uint8_t out[8]) const;
    bool hasAddress(const uint8_t addr[8]) const;
    float readC(const uint8_t addr[8]) const;
    uint8_t deviceCount() const;
    int pin() const { return pin_; }

private:
    int pin_ = -1;
    OneWire oneWire_;
    mutable DallasTemperature dt_;
    bool started_ = false;
};
