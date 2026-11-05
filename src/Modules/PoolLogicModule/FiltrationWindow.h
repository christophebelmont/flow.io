#pragma once
/**
 * @file FiltrationWindow.h
 * @brief Deterministic filtration window computation helper.
 */

#include <stdint.h>

struct FiltrationWindowInput {
    float waterTemp = 0.0f;
    float lowThreshold = 12.0f;
    float setpoint = 24.0f;
    uint8_t startMinHour = 8;
    uint8_t stopMaxHour = 23;
};

struct FiltrationWindowOutput {
    uint8_t startHour = 0;
    uint8_t stopHour = 0;
    uint8_t durationHours = 0;
};

bool computeFiltrationWindowDeterministic(const FiltrationWindowInput& in, FiltrationWindowOutput& out);
bool isFiltrationWindowActiveAtMinute(uint8_t startHour, uint8_t stopHour, uint16_t minuteOfDay);
