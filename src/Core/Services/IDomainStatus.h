#pragma once
/**
 * @file IDomainStatus.h
 * @brief Shared domain-slot runtime health service.
 */

#include <stdint.h>

#include "Domain/DomainTypes.h"
#include "Core/Services/IIO.h"
#include "Core/Services/IPoolDevice.h"

enum class DomainSlotRuntimeState : uint8_t {
    Sleeping = 0,
    Active,
    ManuallyDisabled,
    Error
};

enum class DomainSlotStatusReason : uint8_t {
    None = 0,
    Unbound,
    NotConfigured,
    NoBinding,
    IoModuleDisabled,
    DriverDisabled,
    ExpanderDisabled,
    SlotDisabled,
    HardwareNotDetected,
    DriverInitFailed,
    NoValidValue,
    PoolDeviceBlocked,
    ReadFailed
};

struct DomainSlotStatus {
    DomainSlotId domainSlot = DOMAIN_SLOT_INVALID;
    IoSlotId ioSlot = IO_SLOT_INVALID;
    IoId ioId = IO_ID_INVALID;
    DomainSlotRuntimeState state = DomainSlotRuntimeState::Sleeping;
    DomainSlotStatusReason reason = DomainSlotStatusReason::None;
    uint8_t active = 0U;
    uint8_t error = 0U;
    uint8_t hasMeta = 0U;
    uint8_t hasValue = 0U;
    uint8_t hasBindingPort = 0U;
    uint8_t hasPoolDevice = 0U;
    IoEndpointMeta meta{};
    IoValue value{};
    PoolDeviceSvcMeta poolMeta{};
    uint8_t poolActualOn = 0U;
    uint32_t poolActualTsMs = 0U;
};

struct DomainStatusSummary {
    uint16_t total = 0U;
    uint16_t active = 0U;
    uint16_t manuallyDisabled = 0U;
    uint16_t sleeping = 0U;
    uint16_t error = 0U;
};

struct DomainStatusService {
    bool (*slotStatus)(void* ctx, DomainSlotId domainSlot, DomainSlotStatus* outStatus);
    bool (*summary)(void* ctx, DomainStatusSummary* outSummary);
    bool (*hasDomainSlotError)(void* ctx);
    bool (*firstError)(void* ctx, DomainSlotStatus* outStatus);
    void* ctx;
};

constexpr const char* domainSlotRuntimeStateName(DomainSlotRuntimeState state)
{
    switch (state) {
        case DomainSlotRuntimeState::Sleeping: return "sleeping";
        case DomainSlotRuntimeState::Active: return "active";
        case DomainSlotRuntimeState::ManuallyDisabled: return "manually_disabled";
        case DomainSlotRuntimeState::Error: return "error";
    }
    return "sleeping";
}

constexpr const char* domainSlotStatusReasonName(DomainSlotStatusReason reason)
{
    switch (reason) {
        case DomainSlotStatusReason::None: return "";
        case DomainSlotStatusReason::Unbound: return "unbound";
        case DomainSlotStatusReason::NotConfigured: return "not_configured";
        case DomainSlotStatusReason::NoBinding: return "no_binding";
        case DomainSlotStatusReason::IoModuleDisabled: return "io_module_disabled";
        case DomainSlotStatusReason::DriverDisabled: return "driver_disabled";
        case DomainSlotStatusReason::ExpanderDisabled: return "expander_disabled";
        case DomainSlotStatusReason::SlotDisabled: return "slot_disabled";
        case DomainSlotStatusReason::HardwareNotDetected: return "hardware_not_detected";
        case DomainSlotStatusReason::DriverInitFailed: return "driver_init_failed";
        case DomainSlotStatusReason::NoValidValue: return "no_valid_value";
        case DomainSlotStatusReason::PoolDeviceBlocked: return "pool_device_blocked";
        case DomainSlotStatusReason::ReadFailed: return "read_failed";
    }
    return "unknown";
}
