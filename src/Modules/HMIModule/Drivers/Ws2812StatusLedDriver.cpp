/**
 * @file Ws2812StatusLedDriver.cpp
 * @brief Implementation for single-LED WS2812 HMI output.
 */

#include "Modules/HMIModule/Drivers/Ws2812StatusLedDriver.h"

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <esp32-hal-rgb-led.h>
#include <string.h>

namespace {

constexpr HmiLedPattern kPatterns[] = {
    {255U, 0U, 0U, 300U, 0U, 210U, HmiLedAnimation::FastBlink},
    {255U, 96U, 0U, 1100U, 12U, 150U, HmiLedAnimation::Breathe},
    {0U, 255U, 190U, 2600U, 45U, 170U, HmiLedAnimation::Breathe},
    {150U, 0U, 255U, 850U, 8U, 150U, HmiLedAnimation::Breathe},
    {0U, 64U, 255U, 650U, 0U, 170U, HmiLedAnimation::Pulse},
    {255U, 255U, 255U, 2200U, 8U, 100U, HmiLedAnimation::Breathe},
    {0U, 255U, 32U, 6500U, 30U, 54U, HmiLedAnimation::Breathe},
};

constexpr uint8_t kPatternCount = (uint8_t)(sizeof(kPatterns) / sizeof(kPatterns[0]));

static_assert(kPatternCount == 7U, "HMI LED pattern table must match HmiLedDisplayState");

}  // namespace

void HMIWS2812B::setConfig(const Config& cfg)
{
    cfg_ = cfg;
    ready_ = false;
    lastWriteValid_ = false;
}

bool HMIWS2812B::begin()
{
    if (!cfg_.enabled || cfg_.gpio < 0) {
        ready_ = false;
        return false;
    }
    if (!digitalPinIsValid((uint8_t)cfg_.gpio)) {
        ready_ = false;
        return false;
    }

    pinMode((uint8_t)cfg_.gpio, OUTPUT);
    blinkPhaseOn_ = true;
    blinkPhaseSinceMs_ = millis();
    breatheSinceMs_ = blinkPhaseSinceMs_;
    hmiMode_ = true;
    enabled_ = true;
    conditions_ = (1UL << (uint8_t)HmiLedCondition::Booting);
    currentState_ = HmiLedDisplayState::Booting;
    targetState_ = HmiLedDisplayState::Booting;
    transitionActive_ = false;
    firstColorForState_(currentState_, lastR_, lastG_, lastB_);
    ready_ = true;
    render(blinkPhaseSinceMs_);
    return true;
}

void HMIWS2812B::tick()
{
    tick(millis());
}

void HMIWS2812B::tick(uint32_t nowMs)
{
    if (!ready_) return;
    if (hmiMode_) {
        const HmiLedDisplayState next = computeDisplayState();
        if (next != targetState_) {
            startTransitionTo(next);
        }
        render(nowMs);
        return;
    }

    if (!state_.enabled || (!state_.blinkEnabled && !state_.breatheEnabled)) {
        if (!blinkPhaseOn_) {
            blinkPhaseOn_ = true;
            blinkPhaseSinceMs_ = nowMs;
            applyOutput_(true);
        } else {
            applyOutput_(false);
        }
        return;
    }
    if (state_.breatheEnabled) {
        applyOutput_(false);
        return;
    }

    const uint16_t phaseMs = blinkPhaseOn_ ? state_.blinkOnMs : state_.blinkOffMs;
    if ((uint32_t)(nowMs - blinkPhaseSinceMs_) >= (uint32_t)phaseMs) {
        blinkPhaseOn_ = !blinkPhaseOn_;
        blinkPhaseSinceMs_ = nowMs;
        applyOutput_(true);
    } else {
        applyOutput_(false);
    }
}

void HMIWS2812B::setCondition(HmiLedCondition condition, bool active)
{
    hmiMode_ = true;
    const uint8_t bit = static_cast<uint8_t>(condition);
    if (bit >= static_cast<uint8_t>(HmiLedCondition::Count)) {
        return;
    }

    if (active) {
        conditions_ |= (1UL << bit);
    } else {
        conditions_ &= ~(1UL << bit);
    }
}

void HMIWS2812B::clearAllConditions()
{
    hmiMode_ = true;
    conditions_ = 0U;
}

void HMIWS2812B::setBootComplete()
{
    setCondition(HmiLedCondition::Booting, false);
}

bool HMIWS2812B::setState(const Ws2812StatusLedState& state)
{
    Ws2812StatusLedState normalized = state;
    sanitizeState_(normalized);

    const bool changed = memcmp(&state_, &normalized, sizeof(Ws2812StatusLedState)) != 0;
    state_ = normalized;
    hmiMode_ = false;
    if (state_.blinkEnabled) {
        blinkPhaseOn_ = true;
        blinkPhaseSinceMs_ = millis();
    }
    if (state_.breatheEnabled) {
        breatheSinceMs_ = millis();
    }
    if (changed) applyOutput_(true);
    return true;
}

bool HMIWS2812B::getState(Ws2812StatusLedState& out) const
{
    out = state_;
    return true;
}

bool HMIWS2812B::setEnabled(bool enabled)
{
    if (hmiMode_) {
        enabled_ = enabled;
        if (!enabled_) setPixel(0U, 0U, 0U);
        return true;
    }
    if (state_.enabled == enabled) return true;
    state_.enabled = enabled;
    applyOutput_(true);
    return true;
}

bool HMIWS2812B::setColor(uint8_t red, uint8_t green, uint8_t blue)
{
    hmiMode_ = false;
    if (state_.red == red && state_.green == green && state_.blue == blue) return true;
    state_.red = red;
    state_.green = green;
    state_.blue = blue;
    applyOutput_(true);
    return true;
}

bool HMIWS2812B::setBrightness(uint8_t brightness)
{
    if (hmiMode_) {
        brightness_ = brightness;
        return true;
    }
    if (state_.brightness == brightness) return true;
    state_.brightness = brightness;
    applyOutput_(true);
    return true;
}

bool HMIWS2812B::setBlink(bool enabled, uint16_t onMs, uint16_t offMs)
{
    hmiMode_ = false;
    if (enabled) {
        if (onMs == 0U) onMs = 250U;
        if (offMs == 0U) offMs = 250U;
    }

    if (state_.blinkEnabled == enabled &&
        state_.blinkOnMs == onMs &&
        state_.blinkOffMs == offMs) {
        return true;
    }

    state_.blinkEnabled = enabled;
    if (enabled) state_.breatheEnabled = false;
    state_.blinkOnMs = onMs;
    state_.blinkOffMs = offMs;
    blinkPhaseOn_ = true;
    blinkPhaseSinceMs_ = millis();
    applyOutput_(true);
    return true;
}

bool HMIWS2812B::setBreathe(bool enabled, uint16_t periodMs)
{
    hmiMode_ = false;
    if (enabled && periodMs < 200U) periodMs = 200U;
    if (state_.breatheEnabled == enabled && state_.breathePeriodMs == periodMs) {
        return true;
    }

    state_.breatheEnabled = enabled;
    if (enabled) state_.blinkEnabled = false;
    state_.breathePeriodMs = periodMs;
    breatheSinceMs_ = millis();
    applyOutput_(true);
    return true;
}

bool HMIWS2812B::isConditionActive_(HmiLedCondition condition) const
{
    const uint8_t bit = static_cast<uint8_t>(condition);
    if (bit >= static_cast<uint8_t>(HmiLedCondition::Count)) return false;
    return (conditions_ & (1UL << bit)) != 0U;
}

HmiLedDisplayState HMIWS2812B::computeDisplayState() const
{
    if (isConditionActive_(HmiLedCondition::AlarmActive)) return HmiLedDisplayState::AlarmActive;
    if (isConditionActive_(HmiLedCondition::DomainSlotError)) return HmiLedDisplayState::DomainSlotError;
    if (isConditionActive_(HmiLedCondition::CaptivePortalActive)) return HmiLedDisplayState::CaptivePortalActive;
    if (isConditionActive_(HmiLedCondition::OtaInProgress)) return HmiLedDisplayState::OtaInProgress;
    if (isConditionActive_(HmiLedCondition::NetworkLost)) return HmiLedDisplayState::NetworkLost;
    if (isConditionActive_(HmiLedCondition::Booting)) return HmiLedDisplayState::Booting;
    return HmiLedDisplayState::Normal;
}

void HMIWS2812B::startTransitionTo(HmiLedDisplayState nextState)
{
    targetState_ = nextState;
    transitionActive_ = true;
    transitionStartMs_ = millis();
    transitionDurationMs_ = (nextState == HmiLedDisplayState::AlarmActive) ? kAlarmTransitionMs : kDefaultTransitionMs;
    fromR_ = lastR_;
    fromG_ = lastG_;
    fromB_ = lastB_;
    firstColorForState_(nextState, toR_, toG_, toB_);
}

void HMIWS2812B::render(uint32_t nowMs)
{
    if (!enabled_) {
        setPixel(0U, 0U, 0U);
        return;
    }

    uint8_t r = 0U;
    uint8_t g = 0U;
    uint8_t b = 0U;

    if (transitionActive_) {
        const uint32_t elapsed = nowMs - transitionStartMs_;
        if (elapsed >= transitionDurationMs_) {
            transitionActive_ = false;
            currentState_ = targetState_;
            breatheSinceMs_ = nowMs;
            blinkPhaseSinceMs_ = nowMs;
            blinkPhaseOn_ = true;
        } else {
            const uint16_t step = (uint16_t)elapsed;
            r = lerp8_(fromR_, toR_, step, transitionDurationMs_);
            g = lerp8_(fromG_, toG_, step, transitionDurationMs_);
            b = lerp8_(fromB_, toB_, step, transitionDurationMs_);
            setPixel(r, g, b);
            lastR_ = r;
            lastG_ = g;
            lastB_ = b;
            return;
        }
    }

    const uint8_t stateIdx = (uint8_t)currentState_;
    if (stateIdx >= kPatternCount) return;
    renderPattern_(kPatterns[stateIdx], nowMs, r, g, b);
    setPixel(r, g, b);
    lastR_ = r;
    lastG_ = g;
    lastB_ = b;
}

void HMIWS2812B::renderPattern_(const HmiLedPattern& pattern, uint32_t nowMs, uint8_t& r, uint8_t& g, uint8_t& b) const
{
    uint8_t level = pattern.maxBrightness;
    const uint16_t period = (pattern.periodMs == 0U) ? 1000U : pattern.periodMs;
    const uint16_t phase = (uint16_t)((nowMs - breatheSinceMs_) % period);

    switch (pattern.animation) {
        case HmiLedAnimation::Solid:
            level = pattern.maxBrightness;
            break;
        case HmiLedAnimation::Breathe: {
            const uint16_t half = (uint16_t)(period / 2U);
            const uint16_t ramp = (phase < half) ? phase : (uint16_t)(period - phase);
            const uint16_t span = (pattern.maxBrightness > pattern.minBrightness)
                                      ? (uint16_t)(pattern.maxBrightness - pattern.minBrightness)
                                      : 0U;
            const uint8_t normalized = (uint8_t)(((uint32_t)ramp * 255UL) / (half == 0U ? 1U : half));
            const uint8_t eased = smoothstep8_(normalized);
            level = (uint8_t)(pattern.minBrightness + ((span * eased) / 255U));
            break;
        }
        case HmiLedAnimation::Blink:
        case HmiLedAnimation::FastBlink:
            level = (phase < (period / 2U)) ? pattern.maxBrightness : pattern.minBrightness;
            break;
        case HmiLedAnimation::Pulse: {
            const uint16_t active = (uint16_t)(period / 4U);
            if (phase < active) {
                level = pattern.maxBrightness;
            } else {
                const uint16_t tail = (uint16_t)(period - active);
                const uint16_t elapsed = (uint16_t)(phase - active);
                const uint16_t span = (pattern.maxBrightness > pattern.minBrightness)
                                          ? (uint16_t)(pattern.maxBrightness - pattern.minBrightness)
                                          : 0U;
                level = (uint8_t)(pattern.maxBrightness - ((span * elapsed) / (tail == 0U ? 1U : tail)));
            }
            break;
        }
    }

    r = scale_(pattern.r, level);
    g = scale_(pattern.g, level);
    b = scale_(pattern.b, level);
}

void HMIWS2812B::firstColorForState_(HmiLedDisplayState state, uint8_t& r, uint8_t& g, uint8_t& b) const
{
    const uint8_t stateIdx = (uint8_t)state;
    if (stateIdx >= kPatternCount) {
        r = 0U;
        g = 0U;
        b = 0U;
        return;
    }
    const HmiLedPattern& pattern = kPatterns[stateIdx];
    r = scale_(pattern.r, pattern.maxBrightness);
    g = scale_(pattern.g, pattern.maxBrightness);
    b = scale_(pattern.b, pattern.maxBrightness);
}

uint8_t HMIWS2812B::scale_(uint8_t value, uint8_t brightness)
{
    return (uint8_t)(((uint16_t)value * (uint16_t)brightness + 127U) / 255U);
}

uint8_t HMIWS2812B::smoothstep8_(uint8_t x)
{
    const uint32_t xx = (uint32_t)x * (uint32_t)x;
    const uint32_t shaped = xx * (uint32_t)(765U - (2U * (uint32_t)x));
    return (uint8_t)((shaped + 32512U) / 65025U);
}

uint8_t HMIWS2812B::lerp8_(uint8_t from, uint8_t to, uint16_t step, uint16_t span)
{
    if (span == 0U || step >= span) return to;
    const int16_t delta = (int16_t)to - (int16_t)from;
    return (uint8_t)((int16_t)from + (int16_t)((delta * (int32_t)step) / span));
}

void HMIWS2812B::sanitizeState_(Ws2812StatusLedState& state) const
{
    if (state.blinkEnabled) {
        state.breatheEnabled = false;
        if (state.blinkOnMs == 0U) state.blinkOnMs = 250U;
        if (state.blinkOffMs == 0U) state.blinkOffMs = 250U;
    }
    if (state.breatheEnabled && state.breathePeriodMs < 200U) {
        state.breathePeriodMs = 200U;
    }
}

void HMIWS2812B::setPixel(uint8_t r, uint8_t g, uint8_t b)
{
    if (!ready_) return;

    const uint8_t outR = scale_(r, brightness_);
    const uint8_t outG = scale_(g, brightness_);
    const uint8_t outB = scale_(b, brightness_);

    if (lastWriteValid_ &&
        lastWriteR_ == outR &&
        lastWriteG_ == outG &&
        lastWriteB_ == outB) {
        return;
    }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    rgbLedWriteOrdered((uint8_t)cfg_.gpio, LED_COLOR_ORDER_RGB, outR, outG, outB);
#else
    neopixelWrite((uint8_t)cfg_.gpio, outG, outR, outB);
#endif
    lastWriteR_ = outR;
    lastWriteG_ = outG;
    lastWriteB_ = outB;
    lastWriteValid_ = true;
}

void HMIWS2812B::applyOutput_(bool force)
{
    if (!ready_) return;

    uint8_t outR = 0U;
    uint8_t outG = 0U;
    uint8_t outB = 0U;
    const bool outputEnabled = state_.enabled && (!state_.blinkEnabled || blinkPhaseOn_);
    if (outputEnabled) {
        uint8_t brightness = state_.brightness;
        if (state_.breatheEnabled) {
            const uint16_t period = (state_.breathePeriodMs < 200U) ? 200U : state_.breathePeriodMs;
            const uint16_t phase = (uint16_t)((millis() - breatheSinceMs_) % period);
            const uint16_t half = (uint16_t)(period / 2U);
            const uint16_t ramp = (phase < half) ? phase : (uint16_t)(period - phase);
            const uint8_t minBrightness = 12U;
            const uint16_t span = (brightness > minBrightness) ? (uint16_t)(brightness - minBrightness) : 0U;
            brightness = (uint8_t)(minBrightness + ((span * ramp) / (half == 0U ? 1U : half)));
        }
        outR = (uint8_t)(((uint16_t)state_.red * (uint16_t)brightness + 127U) / 255U);
        outG = (uint8_t)(((uint16_t)state_.green * (uint16_t)brightness + 127U) / 255U);
        outB = (uint8_t)(((uint16_t)state_.blue * (uint16_t)brightness + 127U) / 255U);
    }

    if (!force &&
        lastWriteValid_ &&
        lastWriteR_ == outR &&
        lastWriteG_ == outG &&
        lastWriteB_ == outB) {
        return;
    }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    rgbLedWriteOrdered((uint8_t)cfg_.gpio, LED_COLOR_ORDER_RGB, outR, outG, outB);
#else
    neopixelWrite((uint8_t)cfg_.gpio, outG, outR, outB);
#endif
    lastWriteR_ = outR;
    lastWriteG_ = outG;
    lastWriteB_ = outB;
    lastWriteValid_ = true;
    lastR_ = outR;
    lastG_ = outG;
    lastB_ = outB;
}
