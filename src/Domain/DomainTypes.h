#pragma once

#include <stdint.h>

#include "Core/Services/IIO.h"

struct AppContext;

using IoSlotId = uint16_t;
using DomainSlotId = uint16_t;
using PoolDeviceId = uint8_t;

constexpr IoSlotId IO_SLOT_INVALID = 0xFFFFu;
constexpr DomainSlotId DOMAIN_SLOT_INVALID = 0xFFFFu;
constexpr PoolDeviceId POOL_DEVICE_INVALID = 0xFFu;

enum IoSlotKind : uint8_t {
    IO_SLOT_ANALOG_INPUT = 0,
    IO_SLOT_DIGITAL_INPUT = 1,
    IO_SLOT_DIGITAL_OUTPUT = 2
};

constexpr IoSlotId ioSlotId(uint8_t kind, uint8_t index)
{
    return (IoSlotId)(((uint16_t)kind << 8) | index);
}

constexpr uint8_t ioSlotKind(IoSlotId slot)
{
    return (uint8_t)((slot >> 8) & 0xFFu);
}

constexpr uint8_t ioSlotIndex(IoSlotId slot)
{
    return (uint8_t)(slot & 0xFFu);
}

constexpr IoSlotId analogInputSlot(uint8_t index)
{
    return ioSlotId(IO_SLOT_ANALOG_INPUT, index);
}

constexpr IoSlotId digitalInputSlot(uint8_t index)
{
    return ioSlotId(IO_SLOT_DIGITAL_INPUT, index);
}

constexpr IoSlotId digitalOutputSlot(uint8_t index)
{
    return ioSlotId(IO_SLOT_DIGITAL_OUTPUT, index);
}

constexpr IoId ioIdFromSlot(IoSlotId slot)
{
    return (slot == IO_SLOT_INVALID) ? IO_ID_INVALID :
           (ioSlotKind(slot) == IO_SLOT_ANALOG_INPUT) ? (IoId)(IO_ID_AI_BASE + ioSlotIndex(slot)) :
           (ioSlotKind(slot) == IO_SLOT_DIGITAL_INPUT) ? (IoId)(IO_ID_DI_BASE + ioSlotIndex(slot)) :
           (ioSlotKind(slot) == IO_SLOT_DIGITAL_OUTPUT) ? (IoId)(IO_ID_DO_BASE + ioSlotIndex(slot)) :
                                                           IO_ID_INVALID;
}

struct DomainSlotPreset {
    DomainSlotId id = DOMAIN_SLOT_INVALID;
    uint8_t slotKind = IO_SLOT_ANALOG_INPUT;
    const char* endpointId = nullptr;
    const char* displayName = nullptr;
    uint8_t runtimeIndex = 0;
    bool activeHigh = true;
    uint8_t pullMode = 0;
};

struct DomainIoSlotBinding {
    DomainSlotId domainSlot = DOMAIN_SLOT_INVALID;
    IoSlotId ioSlot = IO_SLOT_INVALID;
};

struct PoolDevicePreset {
    PoolDeviceId id = POOL_DEVICE_INVALID;
    DomainSlotId commandSlot = DOMAIN_SLOT_INVALID;
    const char* objectSuffix;
    const char* displayName;
    const char* haIcon;
    uint8_t poolDeviceType;
    float flowLPerHour;
    float tankCapacityMl;
    float tankInitialMl;
    PoolDeviceId dependsOnDevice = POOL_DEVICE_INVALID;
    int32_t maxUptimeDaySec;
};

struct PoolLogicDefaultsSpec {
    float tempLow;
    float tempHigh;
    uint8_t filtrationStartMinHour;
    uint8_t filtrationStopMaxHour;
    float psiLow;
    float psiHigh;
    float winterStartTempC;
    float freezeHoldTempC;
    float secureElectroTempC;
    float phSetpoint;
    float orpSetpoint;
    float phKp;
    float phKi;
    float phKd;
    float orpKp;
    float orpKi;
    float orpKd;
    int32_t pidWindowMs;
    int32_t pidMinOnMs;
    int32_t pidSampleMs;
    uint8_t psiStartupDelaySec;
    uint8_t delayPidsMin;
    uint8_t delayElectroMin;
    uint8_t robotDelayMin;
    uint8_t robotDurationMin;
    uint8_t fillingMinOnSec;
};
