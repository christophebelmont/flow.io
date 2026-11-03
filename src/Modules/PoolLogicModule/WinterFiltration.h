#pragma once

#include <stdint.h>

// Monotonic cycle, independent of wall-clock/NTP. Starts immediately on entry.
class WinterFiltrationCycle {
public:
    bool update(uint32_t nowMs, bool enabled, uint16_t durationMin, uint8_t intervalHours)
    {
        if (!enabled) {
            active_ = false;
            return false;
        }
        const uint32_t hours = intervalHours < 1U ? 1U : (intervalHours > 24U ? 24U : intervalHours);
        const uint32_t periodMs = hours * 3600000UL;
        const uint32_t minutes = durationMin < 1U ? 1U : durationMin;
        const uint32_t durationMs = minutes > hours * 60U ? periodMs : minutes * 60000UL;
        if (!active_ || periodMs != periodMs_) {
            phaseMs_ = 0U;
            active_ = true;
        } else {
            // Unsigned subtraction handles millis() rollover; uint64 prevents sum overflow.
            phaseMs_ = (uint32_t)(((uint64_t)phaseMs_ + (uint32_t)(nowMs - lastMs_)) % periodMs);
        }
        lastMs_ = nowMs;
        periodMs_ = periodMs;
        return phaseMs_ < durationMs;
    }

private:
    bool active_ = false;
    uint32_t lastMs_ = 0U;
    uint32_t phaseMs_ = 0U;
    uint32_t periodMs_ = 0U;
};

// Pressure safety and manual control retain priority. Freeze demand is never
// limited by the periodic cycle. The daily window only applies outside winter.
inline bool poolFiltrationDemand(bool pressureError, bool automatic, bool pumpOn,
                                 bool dailyWindow, bool winterMode, bool winterCycle,
                                 bool haveAirTemp, float airTemp, float freezeStart, float freezeHold)
{
    if (pressureError) return false;
    if (!automatic) return pumpOn;
    const bool freezeDemand = haveAirTemp &&
        ((pumpOn && airTemp <= freezeHold) || (winterMode && airTemp < freezeStart));
    return freezeDemand || (winterMode ? winterCycle : dailyWindow);
}
