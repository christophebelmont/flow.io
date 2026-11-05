/**
 * @file Mcp23017Driver.cpp
 * @brief Implementation file.
 */

#include "Mcp23017Driver.h"

Mcp23017Driver::Mcp23017Driver(const char* driverId, I2CBus* bus, uint8_t address)
    : driverId_(driverId), bus_(bus), address_(address)
{
}

bool Mcp23017Driver::begin()
{
    if (!writeReg_(kRegIpola, 0x00)) return false;
    if (!writeReg_(kRegIpolb, 0x00)) return false;
    if (!writeReg16_(kRegOlatA, state_)) return false;
    if (!writeReg16_(kRegGppuA, pullupMask_)) return false;
    if (!writeReg16_(kRegIodirA, directionMask_)) return false;
    return true;
}

bool Mcp23017Driver::configurePin(uint8_t pin, bool output, uint8_t inputPullMode)
{
    if (pin > 15) return false;

    const uint16_t bit = (uint16_t)(1u << pin);
    if (output) {
        directionMask_ &= (uint16_t)~bit;
        pullupMask_ &= (uint16_t)~bit;
    } else {
        directionMask_ |= bit;
        if (inputPullMode == 1U) pullupMask_ |= bit;
        else pullupMask_ &= (uint16_t)~bit;
    }

    if (!writeReg16_(kRegOlatA, state_)) return false;
    if (!writeReg16_(kRegGppuA, pullupMask_)) return false;
    return writeReg16_(kRegIodirA, directionMask_);
}

bool Mcp23017Driver::writeMask(uint16_t mask)
{
    state_ = mask;
    return writeReg16_(kRegOlatA, state_);
}

bool Mcp23017Driver::readMask(uint16_t& mask) const
{
    mask = state_;
    return true;
}

bool Mcp23017Driver::writePin(uint8_t pin, bool on)
{
    if (pin > 15) return false;
    if ((directionMask_ & (uint16_t)(1u << pin)) != 0) return false;

    if (on) state_ |= (uint16_t)(1u << pin);
    else state_ &= (uint16_t)~(uint16_t)(1u << pin);

    return writeReg16_(kRegOlatA, state_);
}

bool Mcp23017Driver::readPin(uint8_t pin, bool& on) const
{
    if (pin > 15) return false;
    uint16_t gpio = 0;
    if (!readReg16_(kRegGpioA, gpio)) return false;
    on = (gpio & (uint16_t)(1u << pin)) != 0;
    return true;
}

bool Mcp23017Driver::readShadow(uint8_t pin, bool& on) const
{
    if (pin > 15) return false;
    on = (state_ & (uint16_t)(1u << pin)) != 0;
    return true;
}

bool Mcp23017Driver::writeReg_(uint8_t reg, uint8_t value)
{
    if (!bus_) return false;
    if (!bus_->lock(20)) return false;
    const bool ok = bus_->writeReg(address_, reg, &value, 1);
    bus_->unlock();
    return ok;
}

bool Mcp23017Driver::readReg_(uint8_t reg, uint8_t& value) const
{
    if (!bus_) return false;
    if (!bus_->lock(20)) return false;
    const bool ok = bus_->readReg(address_, reg, &value, 1);
    bus_->unlock();
    return ok;
}

bool Mcp23017Driver::writeReg16_(uint8_t reg, uint16_t value)
{
    uint8_t data[2] = {
        (uint8_t)(value & 0xFFu),
        (uint8_t)((value >> 8) & 0xFFu)
    };
    if (!bus_) return false;
    if (!bus_->lock(20)) return false;
    const bool ok = bus_->writeReg(address_, reg, data, sizeof(data));
    bus_->unlock();
    return ok;
}

bool Mcp23017Driver::readReg16_(uint8_t reg, uint16_t& value) const
{
    uint8_t data[2] = {0, 0};
    if (!bus_) return false;
    if (!bus_->lock(20)) return false;
    const bool ok = bus_->readReg(address_, reg, data, sizeof(data));
    bus_->unlock();
    if (!ok) return false;
    value = (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
    return true;
}
