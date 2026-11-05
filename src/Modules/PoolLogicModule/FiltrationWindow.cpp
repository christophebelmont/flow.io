/**
 * @file FiltrationWindow.cpp
 * @brief Deterministic filtration window computation helper implementation.
 */

#include "Modules/PoolLogicModule/FiltrationWindow.h"
#include "Domain/Pool/PoolDefaults.h"
#include <math.h>

static int clampClockHour_(int hour)
{
    if (hour < 0) return 0;
    if (hour > PoolDefaults::MaxClockHour) return PoolDefaults::MaxClockHour;
    return hour;
}

static void setWidestAvailableWindow_(const FiltrationWindowInput& in, FiltrationWindowOutput& out)
{
    int start = clampClockHour_((int)in.startMinHour);
    int stop = clampClockHour_((int)in.stopMaxHour);

    if (stop <= start) {
        if (start < PoolDefaults::MaxClockHour) {
            stop = start + PoolDefaults::MinEmergencyDurationHours;
        } else {
            start = PoolDefaults::FallbackStartHour;
            stop = PoolDefaults::MaxClockHour;
        }
    }

    out.durationHours = (uint8_t)(stop - start);
    out.startHour = (uint8_t)start;
    out.stopHour = (uint8_t)stop;
}

bool computeFiltrationWindowDeterministic(const FiltrationWindowInput& in, FiltrationWindowOutput& out)
{
    if (!isfinite(in.waterTemp)) {
        setWidestAvailableWindow_(in, out);
        return true;
    }

    int duration = PoolDefaults::MinDurationHours;
    if (in.waterTemp < in.lowThreshold) {
        duration = PoolDefaults::MinDurationHours;
    } else if (in.waterTemp < in.setpoint) {
        duration = (int)lroundf(in.waterTemp * PoolDefaults::FactorLow);
    } else {
        duration = (int)lroundf(in.waterTemp * PoolDefaults::FactorHigh);
    }

    if (duration < PoolDefaults::MinDurationHours) duration = PoolDefaults::MinDurationHours;
    if (duration > PoolDefaults::MaxDurationHours) duration = PoolDefaults::MaxDurationHours;

    int startMin = clampClockHour_((int)in.startMinHour);
    int stopMax = clampClockHour_((int)in.stopMaxHour);

    int start = PoolDefaults::FiltrationPivotHour - (int)lroundf((float)duration * PoolDefaults::FactorHigh);
    if (start < startMin) start = startMin;

    int stop = start + duration;
    if (stop > stopMax) stop = stopMax;

    if (stop <= start) {
        if (start < PoolDefaults::MaxClockHour) {
            stop = start + PoolDefaults::MinEmergencyDurationHours;
        } else {
            start = PoolDefaults::FallbackStartHour;
            stop = PoolDefaults::MaxClockHour;
        }
    }

    out.durationHours = (uint8_t)(stop - start);
    out.startHour = (uint8_t)start;
    out.stopHour = (uint8_t)stop;
    return true;
}

bool isFiltrationWindowActiveAtMinute(uint8_t startHour, uint8_t stopHour, uint16_t minuteOfDay)
{
    const uint16_t clampedMinute = (minuteOfDay < 1440U) ? minuteOfDay : 1439U;
    const uint16_t startMinute = (uint16_t)(((startHour < 24U) ? startHour : 23U) * 60U);
    const uint16_t stopMinute = (uint16_t)(((stopHour < 24U) ? stopHour : 23U) * 60U);

    if (startMinute == stopMinute) return false;

    return (stopMinute <= startMinute)
        ? (clampedMinute >= startMinute || clampedMinute < stopMinute)
        : (clampedMinute >= startMinute && clampedMinute < stopMinute);
}
