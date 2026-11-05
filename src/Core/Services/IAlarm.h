#pragma once
/**
 * @file IAlarm.h
 * @brief Alarm engine service interface.
 */

#include <stddef.h>
#include <stdint.h>

#include "Core/AlarmIds.h"

/** Tri-state condition result returned by alarm callbacks. */
enum class AlarmCondState : uint8_t {
    False = 0,
    True = 1,
    Unknown = 2,
};

/** Callback used by modules to evaluate alarm conditions. */
using AlarmCondFn = AlarmCondState (*)(void* ctx, uint32_t nowMs);

/** Alarm definition registered by owner modules during init. */
struct AlarmRegistration {
    AlarmId id = AlarmId::None;
    AlarmSeverity severity = AlarmSeverity::Info;
    bool latched = false;
    uint32_t onDelayMs = 0;
    uint32_t offDelayMs = 0;
    uint32_t minRepeatMs = 0;
    char code[24] = {0};
    char title[48] = {0};
    char sourceModule[16] = {0};
    bool activityLogEnabled = true;
};

/** Service contract exposed by AlarmModule. */
struct AlarmService {
    bool (*registerAlarm)(void* ctx, const AlarmRegistration* def, AlarmCondFn condFn, void* condCtx);
    bool (*reset)(void* ctx, AlarmId id);
    uint8_t (*resetAll)(void* ctx);
    bool (*isActive)(void* ctx, AlarmId id);
    bool (*isResettable)(void* ctx, AlarmId id);
    uint8_t (*activeCount)(void* ctx);
    AlarmSeverity (*highestSeverity)(void* ctx);
    bool (*buildSnapshot)(void* ctx, char* out, size_t len);
    uint8_t (*listIds)(void* ctx, AlarmId* out, uint8_t max);
    bool (*buildAlarmState)(void* ctx, AlarmId id, char* out, size_t len);
    /** Builds compact per-slot packed state (5 bits/slot) used by dense UIs. */
    bool (*buildPacked)(void* ctx, char* out, size_t len, uint8_t slotCount);
    void* ctx;
};
