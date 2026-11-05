#pragma once
/**
 * @file II2cBus.h
 * @brief Service exposing the process-wide primary I2C bus.
 */

class I2CBus;

struct I2cBusService {
    I2CBus* bus;
};
