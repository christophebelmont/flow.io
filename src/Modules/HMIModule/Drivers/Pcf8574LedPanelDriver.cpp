/**
 * @file Pcf8574LedPanelDriver.cpp
 * @brief Dedicated PCF8574A front LED panel implementation.
 */

#include "Modules/HMIModule/Drivers/Pcf8574LedPanelDriver.h"

#include "Core/I2cBus.h"

bool Pcf8574LedPanelDriver::begin(I2CBus& bus)
{
    bus_ = &bus;
    enabled_ = false;
    logicalMask_ = 0U;

    // PCF8574A outputs source weak current when high. The LED panel sinks
    // current, so 0xFF is the fixed all-off physical state.
    ready_ = writePhysicalMask_(0xFFU);
    return ready_;
}

bool Pcf8574LedPanelDriver::setEnabled(bool enabled)
{
    if (!ready_) return false;
    if (enabled_ == enabled) return true;

    if (!enabled) {
        if (!writePhysicalMask_(0xFFU)) return false;
        logicalMask_ = 0U;
    }
    enabled_ = enabled;
    return true;
}

bool Pcf8574LedPanelDriver::writeLogicalMask(uint8_t mask)
{
    if (!ready_ || !enabled_) return false;
    if (!writePhysicalMask_((uint8_t)~mask)) return false;
    logicalMask_ = mask;
    return true;
}

bool Pcf8574LedPanelDriver::writePhysicalMask_(uint8_t mask)
{
    if (!bus_ || !bus_->beginOk()) return false;
    if (!bus_->lock(20U)) return false;
    const bool ok = bus_->writeBytes(Address, &mask, 1U);
    bus_->unlock();
    return ok;
}
