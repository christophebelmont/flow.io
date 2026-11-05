#pragma once
/**
 * @file Ws2812StatusLedDriver.h
 * @brief Single-LED WS2812 HMI condition renderer.
 */

#include <stdint.h>

#include "Core/Services/IHmi.h"

struct Ws2812StatusLedState {
    bool enabled = false;
    bool blinkEnabled = false;
    bool breatheEnabled = false;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 255;
    uint8_t brightness = 96;
    uint16_t blinkOnMs = 250;
    uint16_t blinkOffMs = 250;
    uint16_t breathePeriodMs = 900;
};

struct HmiLedPattern {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint16_t periodMs;
    uint8_t minBrightness;
    uint8_t maxBrightness;
    HmiLedAnimation animation;
};

class HMIWS2812B {
public:
    struct Config {
        int8_t gpio = -1;
        bool enabled = false;
    };

    void setConfig(const Config& cfg);
    bool begin();
    void tick();
    void tick(uint32_t nowMs);

    void setCondition(HmiLedCondition condition, bool active);
    void clearAllConditions();
    void setBootComplete();

    bool setState(const Ws2812StatusLedState& state);
    bool getState(Ws2812StatusLedState& out) const;
    bool setEnabled(bool enabled);
    bool setColor(uint8_t red, uint8_t green, uint8_t blue);
    bool setBrightness(uint8_t brightness);
    bool setBlink(bool enabled, uint16_t onMs, uint16_t offMs);
    bool setBreathe(bool enabled, uint16_t periodMs);

    bool isReady() const { return ready_; }
    bool isConfigured() const { return cfg_.enabled && cfg_.gpio >= 0; }
    bool isEnabled() const { return hmiMode_ ? enabled_ : state_.enabled; }
    bool isTransitionActive() const { return transitionActive_; }
    HmiLedDisplayState currentDisplayState() const { return currentState_; }
    HmiLedDisplayState targetDisplayState() const { return targetState_; }

private:
    static constexpr uint16_t kDefaultTransitionMs = 450U;
    static constexpr uint16_t kAlarmTransitionMs = 120U;

    bool isConditionActive_(HmiLedCondition condition) const;
    HmiLedDisplayState computeDisplayState() const;
    void startTransitionTo(HmiLedDisplayState nextState);
    void render(uint32_t nowMs);
    void renderPattern_(const HmiLedPattern& pattern, uint32_t nowMs, uint8_t& r, uint8_t& g, uint8_t& b) const;
    void firstColorForState_(HmiLedDisplayState state, uint8_t& r, uint8_t& g, uint8_t& b) const;
    void setPixel(uint8_t r, uint8_t g, uint8_t b);
    static uint8_t scale_(uint8_t value, uint8_t brightness);
    static uint8_t smoothstep8_(uint8_t x);
    static uint8_t lerp8_(uint8_t from, uint8_t to, uint16_t step, uint16_t span);

    void sanitizeState_(Ws2812StatusLedState& state) const;
    void applyOutput_(bool force);

    Config cfg_{};
    Ws2812StatusLedState state_{};
    bool ready_ = false;
    bool hmiMode_ = true;
    bool enabled_ = true;
    uint8_t brightness_ = 48U;
    uint32_t conditions_ = 0U;
    HmiLedDisplayState currentState_ = HmiLedDisplayState::Booting;
    HmiLedDisplayState targetState_ = HmiLedDisplayState::Booting;
    bool transitionActive_ = false;
    uint32_t transitionStartMs_ = 0U;
    uint16_t transitionDurationMs_ = kDefaultTransitionMs;
    uint8_t fromR_ = 0U;
    uint8_t fromG_ = 0U;
    uint8_t fromB_ = 0U;
    uint8_t toR_ = 0U;
    uint8_t toG_ = 0U;
    uint8_t toB_ = 0U;
    bool blinkPhaseOn_ = true;
    uint32_t blinkPhaseSinceMs_ = 0U;
    uint32_t breatheSinceMs_ = 0U;
    bool lastWriteValid_ = false;
    uint8_t lastWriteR_ = 0U;
    uint8_t lastWriteG_ = 0U;
    uint8_t lastWriteB_ = 0U;
    uint8_t lastR_ = 0U;
    uint8_t lastG_ = 0U;
    uint8_t lastB_ = 0U;
};

using Ws2812StatusLedDriver = HMIWS2812B;
