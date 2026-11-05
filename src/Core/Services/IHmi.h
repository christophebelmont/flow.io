#pragma once
/**
 * @file IHmi.h
 * @brief HMI service interface.
 */

#include <stddef.h>
#include <stdint.h>

enum class HmiLedCondition : uint8_t {
    AlarmActive = 0,
    DomainSlotError,
    CaptivePortalActive,
    OtaInProgress,
    NetworkLost,
    Booting,
    Count
};

enum class HmiLedDisplayState : uint8_t {
    AlarmActive,
    DomainSlotError,
    CaptivePortalActive,
    OtaInProgress,
    NetworkLost,
    Booting,
    Normal
};

enum class HmiLedAnimation : uint8_t {
    Solid,
    Breathe,
    Blink,
    FastBlink,
    Pulse
};

struct HmiRtcDateTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
};

struct HmiStatusLedState {
    bool enabled = true;
    bool blinkEnabled = false;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 255;
    uint8_t brightness = 96;
    uint16_t blinkOnMs = 250;
    uint16_t blinkOffMs = 250;
};

struct HmiService {
    bool (*requestRefresh)(void* ctx);
    bool (*openConfigHome)(void* ctx);
    bool (*openConfigModule)(void* ctx, const char* module);
    bool (*buildConfigMenuJson)(void* ctx, char* out, size_t outLen);
    bool (*setLedPage)(void* ctx, uint8_t page);
    uint8_t (*getLedPage)(void* ctx);
    bool (*setStatusLedState)(void* ctx, const HmiStatusLedState* state);
    bool (*getStatusLedState)(void* ctx, HmiStatusLedState* out);
    bool (*setStatusLedAutoWifiMode)(void* ctx, bool enabled);
    bool (*isStatusLedAutoWifiMode)(void* ctx);
    bool (*setLedCondition)(void* ctx, HmiLedCondition condition, bool active);
    void (*clearLedConditions)(void* ctx);
    void (*setBootComplete)(void* ctx);
    bool (*setLedEnabled)(void* ctx, bool enabled);
    bool (*setLedBrightness)(void* ctx, uint8_t brightness);
    bool (*getDisplayVersion)(void* ctx, char* out, size_t outLen);
    bool (*readRtc)(void* ctx, HmiRtcDateTime* out, uint16_t timeoutMs);
    bool (*writeRtc)(void* ctx, const HmiRtcDateTime* value);
    bool (*isDisplaySleeping)(void* ctx);
    void* ctx;
};
