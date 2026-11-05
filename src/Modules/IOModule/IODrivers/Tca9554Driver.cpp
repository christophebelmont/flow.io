/**
 * @file Tca9554Driver.cpp
 * @brief Implementation file.
 */

#include "Tca9554Driver.h"

Tca9554Driver::Tca9554Driver(const char* driverId, I2CBus* bus, uint8_t address)
    : driverId_(driverId), bus_(bus), address_(address)
{
}

bool Tca9554Driver::begin()
{
    uint8_t config = 0xFF;
    uint8_t output = 0xFF;
    if (!readReg_(kRegConfiguration, config)) return false;
    if (!readReg_(kRegOutputPort, output)) return false;

    bootWasColdPowerOn_ = (config == 0xFF);
    if (!writeReg_(kRegPolarityInversion, 0x00)) return false;

    if (bootWasColdPowerOn_) {
        state_ = 0x00;
        if (!writeReg_(kRegOutputPort, state_)) return false;
    } else {
        state_ = output;
    }

    if (!writeReg_(kRegConfiguration, 0x00)) return false;
    return true;
}

bool Tca9554Driver::beginPreserveHardwareState()
{
    uint8_t config = 0xFF;
    uint8_t output = 0xFF;
    if (!readReg_(kRegConfiguration, config)) return false;
    if (!readReg_(kRegOutputPort, output)) return false;

    bootWasColdPowerOn_ = (config == 0xFF);
    if (!writeReg_(kRegPolarityInversion, 0x00)) return false;

    if (bootWasColdPowerOn_) {
        // After a TCA9554 electrical reset, the output register default is not
        // a retained latch. Keep cold boot safe even in preserve mode.
        state_ = 0x00;
        if (!writeReg_(kRegOutputPort, state_)) return false;
    } else {
        state_ = output;
    }

    if (!writeReg_(kRegConfiguration, 0x00)) return false;
    return true;
}

bool Tca9554Driver::writeMask(uint8_t mask)
{
    state_ = mask;
    return writeReg_(kRegOutputPort, state_);
}

bool Tca9554Driver::readMask(uint8_t& mask) const
{
    mask = state_;
    return true;
}

bool Tca9554Driver::writePin(uint8_t pin, bool on)
{
    if (pin > 7) return false;

    if (on) state_ |= (uint8_t)(1u << pin);
    else state_ &= (uint8_t)~(1u << pin);

    return writeReg_(kRegOutputPort, state_);
}

bool Tca9554Driver::readShadow(uint8_t pin, bool& on) const
{
    if (pin > 7) return false;
    on = (state_ & (uint8_t)(1u << pin)) != 0;
    return true;
}

bool Tca9554Driver::readOutputPort(uint8_t& value) const
{
    if (!readReg_(kRegOutputPort, value)) return false;
    return true;
}

bool Tca9554Driver::writeReg_(uint8_t reg, uint8_t value)
{
    if (!bus_) return false;
    if (!bus_->lock(20)) return false;
    const bool ok = bus_->writeReg(address_, reg, &value, 1);
    bus_->unlock();
    return ok;
}

bool Tca9554Driver::readReg_(uint8_t reg, uint8_t& value) const
{
    if (!bus_) return false;
    if (!bus_->lock(20)) return false;
    const bool ok = bus_->readReg(address_, reg, &value, 1);
    bus_->unlock();
    return ok;
}
