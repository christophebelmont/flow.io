/**
 * @file Mcp23017BitDriver.cpp
 * @brief Implementation file.
 */

#include "Mcp23017BitDriver.h"

Mcp23017BitDriver::Mcp23017BitDriver(const char* driverId,
                                     Mcp23017Driver* parent,
                                     uint8_t bit,
                                     bool activeHigh,
                                     bool output,
                                     uint8_t inputPullMode)
    : driverId_(driverId),
      parent_(parent),
      bit_(bit),
      activeHigh_(activeHigh),
      output_(output),
      inputPullMode_(inputPullMode)
{
}

bool Mcp23017BitDriver::begin()
{
    return parent_ && parent_->configurePin(bit_, output_, inputPullMode_);
}

bool Mcp23017BitDriver::write(bool on)
{
    if (!parent_ || !output_) return false;
    const bool rawOn = activeHigh_ ? on : !on;
    return parent_->writePin(bit_, rawOn);
}

bool Mcp23017BitDriver::read(bool& on) const
{
    if (!parent_) return false;
    bool rawOn = false;
    const bool ok = output_ ? parent_->readShadow(bit_, rawOn) : parent_->readPin(bit_, rawOn);
    if (!ok) return false;
    on = activeHigh_ ? rawOn : !rawOn;
    return true;
}
