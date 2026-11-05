#pragma once
/**
 * @file IActivityLog.h
 * @brief User-facing activity journal service.
 */

#include <stdint.h>
#include <stddef.h>

enum class ActivityDomain : uint8_t {
    System = 0,
    PoolLogic = 1,
    PoolDevice = 2,
    Alarm = 3,
};

enum class ActivitySource : uint8_t {
    System = 0,
    Auto = 1,
    Manual = 2,
    Scheduler = 3,
    Safety = 4,
    Pid = 5,
    Boot = 6,
};

enum class ActivitySeverity : uint8_t {
    Info = 0,
    Success = 1,
    Warning = 2,
    Alarm = 3,
};

enum class ActivityRole : uint8_t {
    None = 0,
    Filtration = 1,
    Swg = 2,
    Robot = 3,
    Filling = 4,
    Ph = 5,
    Disinfection = 6,
    Heater = 7,
};

enum class ActivityState : uint8_t {
    None = 0,
    RequestedOn = 1,
    RequestedOff = 2,
    On = 3,
    Off = 4,
};

enum class ActivityReason : uint8_t {
    None = 0,
    Boot = 1,
    BootAdoption = 2,
    Scheduler = 3,
    Manual = 4,
    Auto = 5,
    Pid = 6,
    Safety = 7,
    O2 = 8,
    Dependency = 9,
};

enum class ActivityCode : uint16_t {
    SystemBoot = 1,
    SystemConfigChanged = 2,
    PoolLogicReady = 100,
    PoolLogicDisabled = 101,
    PoolLogicDeviceStartRequested = 120,
    PoolLogicDeviceStopRequested = 121,
    PoolLogicDeviceStarted = 122,
    PoolLogicDeviceStopped = 123,
    PoolLogicFiltrationWindowCalculated = 130,
    PoolLogicPhRegulationEnabled = 140,
    PoolLogicOrpRegulationEnabled = 141,
    PoolLogicO2StateChanged = 150,
    PoolLogicSafetyPressureLatched = 160,
    PoolDeviceManualStart = 220,
    PoolDeviceManualStop = 221,
    PoolDeviceTankRefill = 230,
    PoolDeviceUptimeReset = 240,
    AlarmRaised = 300,
    AlarmConditionOk = 301,
    AlarmReset = 302,
};

constexpr uint8_t ACTIVITY_TARGET_NONE = 0xFF;
constexpr size_t ACTIVITY_TITLE_MAX = 48;
constexpr size_t ACTIVITY_DETAIL_MAX = 128;
constexpr size_t ACTIVITY_ICON_MAX = 24;

struct ActivityEvent {
    uint32_t seq = 0;
    uint32_t ts_ms = 0;
    uint32_t epoch_s = 0;
    uint16_t code = 0;
    uint16_t alarmId = 0;
    uint8_t domain = (uint8_t)ActivityDomain::System;
    uint8_t source = (uint8_t)ActivitySource::System;
    uint8_t severity = (uint8_t)ActivitySeverity::Info;
    uint8_t role = (uint8_t)ActivityRole::None;
    uint8_t state = (uint8_t)ActivityState::None;
    uint8_t reason = (uint8_t)ActivityReason::None;
    uint8_t targetSlot = ACTIVITY_TARGET_NONE;
    char title[ACTIVITY_TITLE_MAX] = {0};
    char detail[ACTIVITY_DETAIL_MAX] = {0};
    char icon[ACTIVITY_ICON_MAX] = {0};
};

struct ActivityLogStats {
    uint16_t capacity = 0;
    uint16_t count = 0;
    uint32_t droppedCount = 0;
    uint32_t persistedCount = 0;
    uint32_t persistDropCount = 0;
    uint32_t seqNext = 0;
    bool psram = false;
    bool spiffs = false;
};

using ActivityLogReplayWriter = bool (*)(void* writerCtx,
                                         const ActivityEvent& event,
                                         uint16_t index,
                                         uint16_t total);

struct ActivityLogService {
    bool (*emit)(void* ctx, const ActivityEvent* event);
    void (*getStats)(void* ctx, ActivityLogStats* out);
    uint16_t (*readPage)(void* ctx,
                         uint16_t offset,
                         uint16_t limit,
                         ActivityLogReplayWriter writer,
                         void* writerCtx);
    bool (*clear)(void* ctx);
    void* ctx;
};
