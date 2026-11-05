#pragma once

#include <stdint.h>

/**
 * @brief Stable identifiers for all services reachable through ServiceRegistry.
 *
 * Keep this file as the single source of truth for service wiring.
 */
enum class ServiceId : uint8_t {
    LogHub = 0,
    LogSinks,
    EventBus,
    ConfigStore,
    DataStore,
    Command,
    Alarm,
    Hmi,
    Wifi,
    Time,
    TimeScheduler,
    Mqtt,
    Ha,
    Io,
    PoolDevice,
    WebInterface,
    FirmwareUpdate,
    NetworkAccess,
    FlowCfg,
    Locale,
    ActivityLog,
    I2cBus,
    DomainStatus,
    Count
};

constexpr uint8_t kServiceIdCount = static_cast<uint8_t>(ServiceId::Count);

constexpr bool isValidServiceId(ServiceId id)
{
    return static_cast<uint8_t>(id) < kServiceIdCount;
}

constexpr uint8_t serviceIdIndex(ServiceId id)
{
    return static_cast<uint8_t>(id);
}

constexpr const char* toString(ServiceId id)
{
    switch (id) {
        case ServiceId::LogHub: return "loghub";
        case ServiceId::LogSinks: return "logsinks";
        case ServiceId::EventBus: return "eventbus";
        case ServiceId::ConfigStore: return "config";
        case ServiceId::DataStore: return "datastore";
        case ServiceId::Command: return "cmd";
        case ServiceId::Alarm: return "alarms";
        case ServiceId::Hmi: return "hmi";
        case ServiceId::Wifi: return "wifi";
        case ServiceId::Time: return "time";
        case ServiceId::TimeScheduler: return "time.scheduler";
        case ServiceId::Mqtt: return "mqtt";
        case ServiceId::Ha: return "ha";
        case ServiceId::Io: return "io";
        case ServiceId::PoolDevice: return "pooldev";
        case ServiceId::WebInterface: return "webinterface";
        case ServiceId::FirmwareUpdate: return "fwupdate";
        case ServiceId::NetworkAccess: return "network_access";
        case ServiceId::FlowCfg: return "flowcfg";
        case ServiceId::Locale: return "locale";
        case ServiceId::ActivityLog: return "activitylog";
        case ServiceId::I2cBus: return "i2c_bus";
        case ServiceId::DomainStatus: return "domain_status";
        case ServiceId::Count: return "count";
    }
    return "unknown";
}
