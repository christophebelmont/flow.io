#pragma once
/**
 * @file EventId.h
 * @brief Enumerates event identifiers used by EventBus.
 */
#include <stdint.h>

/** @brief Known event identifiers. */
enum class EventId : uint16_t {
    None = 0,

    // System lifecycle
    SystemStarted = 1,
    NetworkShutdownPending = 2,

    // ✅ DataStore (runtime model changes)
    DataChanged = 50,

    // Configuration
    ConfigChanged = 100,

    // Sensors / runtime data
    SensorsUpdated = 200,

    // Actuators
    RelayChanged = 300,

    // Domain events
    PoolModeChanged = 400,
    AlarmRaised = 410,
    AlarmCleared = 411,
    AlarmReset = 412,
    AlarmSilenceChanged = 413,
    AlarmConditionChanged = 414,
    SchedulerEventTriggered = 420,

    MicronovaValueUpdated = 500,
    MicronovaOnlineChanged = 501,
    MicronovaCommandPower = 510,
    MicronovaCommandPowerLevel = 511,
    MicronovaCommandFanSpeed = 512,
    MicronovaCommandTargetTemperature = 513,
    MicronovaCommandRefresh = 514,
    MicronovaCommandRawWrite = 515,
    MicronovaCommandSweepReadAll = 516,
    MicronovaCommandPowerPlus = 517,
    MicronovaCommandPowerMinus = 518,
};
