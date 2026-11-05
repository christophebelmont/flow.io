/**
 * @file OneWireBus.cpp
 * @brief Implementation file.
 */

#include "OneWireBus.h"

#include <string.h>

OneWireBus::OneWireBus(int pin) : pin_(pin), oneWire_(pin), dt_(&oneWire_) {}

void OneWireBus::begin() {
    if (pin_ < 0) return;
    if (started_) return;
    dt_.begin();
    dt_.setWaitForConversion(false);
    started_ = true;
}

void OneWireBus::request() {
    if (!started_) return;
    dt_.requestTemperatures();
}

void OneWireBus::setWaitForConversion(bool enabled) {
    if (!started_) return;
    dt_.setWaitForConversion(enabled);
}

bool OneWireBus::getAddress(uint8_t index, uint8_t out[8]) const {
    if (!started_ || !out) return false;
    return dt_.getAddress(out, index);
}

bool OneWireBus::hasAddress(const uint8_t addr[8]) const {
    if (!started_ || !addr) return false;
    uint8_t found[8]{};
    const uint8_t count = deviceCount();
    for (uint8_t i = 0; i < count; ++i) {
        if (dt_.getAddress(found, i) && memcmp(found, addr, sizeof(found)) == 0) {
            return true;
        }
    }
    return false;
}

float OneWireBus::readC(const uint8_t addr[8]) const {
    if (!started_ || !addr) return DEVICE_DISCONNECTED_C;
    return dt_.getTempC(addr);
}

uint8_t OneWireBus::deviceCount() const {
    if (!started_) return 0;
    return (uint8_t)dt_.getDeviceCount();
}
