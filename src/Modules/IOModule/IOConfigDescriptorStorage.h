#pragma once
/**
 * @file IOConfigDescriptorStorage.h
 * @brief Uniform PSRAM-backed configuration descriptors for all configurable I/O slots.
 */

#include "Core/ConfigTypes.h"
#include "Core/SystemLimits.h"
#include "Modules/IOModule/IOModuleTypes.h"
#include <stdio.h>

/**
 * The strings referenced by ConfigVariable must remain alive as long as the
 * ConfigStore metadata. Keeping them next to the descriptors also moves the
 * complete per-slot descriptor state out of IOModule's internal-RAM object.
 */
struct IOConfigDescriptorStorage {
    static constexpr size_t NVS_KEY_CAPACITY = 10U;

    struct AnalogSlot {
        char nameKey[NVS_KEY_CAPACITY]{};
        char bindingKey[NVS_KEY_CAPACITY]{};
        char c0Key[NVS_KEY_CAPACITY]{};
        char c1Key[NVS_KEY_CAPACITY]{};
        char precisionKey[NVS_KEY_CAPACITY]{};
        char nameJson[9]{};
        char c0Json[8]{};
        char c1Json[8]{};
        char precisionJson[10]{};
        char moduleName[13]{};
        ConfigVariable<char, 0> nameVar{};
        ConfigVariable<PhysicalPortId, 0> bindingVar{};
        ConfigVariable<float, 0> c0Var{};
        ConfigVariable<float, 0> c1Var{};
        ConfigVariable<int32_t, 0> precisionVar{};
    };

    struct DigitalInputSlot {
        char nameKey[NVS_KEY_CAPACITY]{};
        char bindingKey[NVS_KEY_CAPACITY]{};
        char activeHighKey[NVS_KEY_CAPACITY]{};
        char pullModeKey[NVS_KEY_CAPACITY]{};
        char edgeModeKey[NVS_KEY_CAPACITY]{};
        char counterDebounceKey[NVS_KEY_CAPACITY]{};
        char c0Key[NVS_KEY_CAPACITY]{};
        char precisionKey[NVS_KEY_CAPACITY]{};
        char modeKey[NVS_KEY_CAPACITY]{};
        char counterTotalKey[NVS_KEY_CAPACITY]{};
        char nameJson[9]{};
        char activeHighJson[17]{};
        char pullModeJson[15]{};
        char c0Json[8]{};
        char precisionJson[10]{};
        char moduleName[13]{};
        ConfigVariable<char, 0> nameVar{};
        ConfigVariable<PhysicalPortId, 0> bindingVar{};
        ConfigVariable<bool, 0> activeHighVar{};
        ConfigVariable<uint8_t, 0> pullModeVar{};
        ConfigVariable<uint8_t, 0> edgeModeVar{};
        ConfigVariable<int32_t, 0> counterDebounceVar{};
        ConfigVariable<float, 0> c0Var{};
        ConfigVariable<int32_t, 0> precisionVar{};
        ConfigVariable<uint8_t, 0> modeVar{};
        ConfigVariable<float, 0> counterTotalVar{};
    };

    struct DigitalOutputSlot {
        char nameKey[NVS_KEY_CAPACITY]{};
        char bindingKey[NVS_KEY_CAPACITY]{};
        char activeHighKey[NVS_KEY_CAPACITY]{};
        char initialOnKey[NVS_KEY_CAPACITY]{};
        char retainWarmKey[NVS_KEY_CAPACITY]{};
        char momentaryKey[NVS_KEY_CAPACITY]{};
        char pulseKey[NVS_KEY_CAPACITY]{};
        char nameJson[9]{};
        char activeHighJson[17]{};
        char initialOnJson[15]{};
        char momentaryJson[14]{};
        char pulseJson[13]{};
        char moduleName[14]{};
        ConfigVariable<char, 0> nameVar{};
        ConfigVariable<PhysicalPortId, 0> bindingVar{};
        ConfigVariable<bool, 0> activeHighVar{};
        ConfigVariable<bool, 0> initialOnVar{};
        ConfigVariable<bool, 0> retainWarmVar{};
        ConfigVariable<bool, 0> momentaryVar{};
        ConfigVariable<int32_t, 0> pulseVar{};
    };

    AnalogSlot analog[Limits::Io::AnalogConfigSlots]{};
    DigitalInputSlot digitalInputs[Limits::Io::DigitalInputConfigSlots]{};
    DigitalOutputSlot digitalOutputs[Limits::Io::DigitalOutputConfigSlots]{};

    IOConfigDescriptorStorage(IOAnalogSlotConfig* analogCfg,
                              IODigitalInputSlotConfig* digitalInputCfg,
                              IODigitalOutputSlotConfig* digitalOutputCfg)
    {
        for (uint8_t slot = 0; slot < Limits::Io::AnalogConfigSlots; ++slot) {
            AnalogSlot& vars = analog[slot];
            snprintf(vars.nameKey, sizeof(vars.nameKey), "io_a%02unm", (unsigned)slot);
            snprintf(vars.bindingKey, sizeof(vars.bindingKey), "io_a%02ubp", (unsigned)slot);
            snprintf(vars.c0Key, sizeof(vars.c0Key), "io_a%02u0", (unsigned)slot);
            snprintf(vars.c1Key, sizeof(vars.c1Key), "io_a%02u1", (unsigned)slot);
            snprintf(vars.precisionKey, sizeof(vars.precisionKey), "io_a%02up", (unsigned)slot);
            snprintf(vars.nameJson, sizeof(vars.nameJson), "a%02u_name", (unsigned)slot);
            snprintf(vars.c0Json, sizeof(vars.c0Json), "a%02u_c0", (unsigned)slot);
            snprintf(vars.c1Json, sizeof(vars.c1Json), "a%02u_c1", (unsigned)slot);
            snprintf(vars.precisionJson, sizeof(vars.precisionJson), "a%02u_prec", (unsigned)slot);
            snprintf(vars.moduleName, sizeof(vars.moduleName), "io/input/a%02u", (unsigned)slot);

            vars.nameVar = {vars.nameKey, vars.nameJson, vars.moduleName, ConfigType::CharArray,
                            analogCfg[slot].name, ConfigPersistence::Persistent,
                            sizeof(analogCfg[slot].name)};
            vars.bindingVar = {vars.bindingKey, "binding_port", vars.moduleName, ConfigType::UInt16,
                               &analogCfg[slot].bindingPort, ConfigPersistence::Persistent, 0};
            vars.c0Var = {vars.c0Key, vars.c0Json, vars.moduleName, ConfigType::Float,
                          &analogCfg[slot].c0, ConfigPersistence::Persistent, 0};
            vars.c1Var = {vars.c1Key, vars.c1Json, vars.moduleName, ConfigType::Float,
                          &analogCfg[slot].c1, ConfigPersistence::Persistent, 0};
            vars.precisionVar = {vars.precisionKey, vars.precisionJson, vars.moduleName,
                                 ConfigType::Int32, &analogCfg[slot].precision,
                                 ConfigPersistence::Persistent, 0};
        }

        for (uint8_t slot = 0; slot < Limits::Io::DigitalInputConfigSlots; ++slot) {
            DigitalInputSlot& vars = digitalInputs[slot];
            snprintf(vars.nameKey, sizeof(vars.nameKey), "io_i%02unm", (unsigned)slot);
            snprintf(vars.bindingKey, sizeof(vars.bindingKey), "io_i%02ubp", (unsigned)slot);
            snprintf(vars.activeHighKey, sizeof(vars.activeHighKey), "io_i%02uah", (unsigned)slot);
            snprintf(vars.pullModeKey, sizeof(vars.pullModeKey), "io_i%02upu", (unsigned)slot);
            snprintf(vars.edgeModeKey, sizeof(vars.edgeModeKey), "io_i%02ued", (unsigned)slot);
            snprintf(vars.counterDebounceKey, sizeof(vars.counterDebounceKey), "io_i%02udb", (unsigned)slot);
            snprintf(vars.c0Key, sizeof(vars.c0Key), "io_i%02uc0", (unsigned)slot);
            snprintf(vars.precisionKey, sizeof(vars.precisionKey), "io_i%02up", (unsigned)slot);
            snprintf(vars.modeKey, sizeof(vars.modeKey), "io_i%02umd", (unsigned)slot);
            snprintf(vars.counterTotalKey, sizeof(vars.counterTotalKey), "io_i%02uct", (unsigned)slot);
            snprintf(vars.nameJson, sizeof(vars.nameJson), "i%02u_name", (unsigned)slot);
            snprintf(vars.activeHighJson, sizeof(vars.activeHighJson), "i%02u_active_high", (unsigned)slot);
            snprintf(vars.pullModeJson, sizeof(vars.pullModeJson), "i%02u_pull_mode", (unsigned)slot);
            snprintf(vars.c0Json, sizeof(vars.c0Json), "i%02u_c0", (unsigned)slot);
            snprintf(vars.precisionJson, sizeof(vars.precisionJson), "i%02u_prec", (unsigned)slot);
            snprintf(vars.moduleName, sizeof(vars.moduleName), "io/input/i%02u", (unsigned)slot);

            vars.nameVar = {vars.nameKey, vars.nameJson, vars.moduleName, ConfigType::CharArray,
                            digitalInputCfg[slot].name, ConfigPersistence::Persistent,
                            sizeof(digitalInputCfg[slot].name)};
            vars.bindingVar = {vars.bindingKey, "binding_port", vars.moduleName, ConfigType::UInt16,
                               &digitalInputCfg[slot].bindingPort, ConfigPersistence::Persistent, 0};
            vars.activeHighVar = {vars.activeHighKey, vars.activeHighJson, vars.moduleName,
                                  ConfigType::Bool, &digitalInputCfg[slot].activeHigh,
                                  ConfigPersistence::Persistent, 0};
            vars.pullModeVar = {vars.pullModeKey, vars.pullModeJson, vars.moduleName,
                                ConfigType::UInt8, &digitalInputCfg[slot].pullMode,
                                ConfigPersistence::Persistent, 0};
            vars.edgeModeVar = {vars.edgeModeKey, "edge_mode", vars.moduleName, ConfigType::UInt8,
                                &digitalInputCfg[slot].edgeMode, ConfigPersistence::Persistent, 0};
            vars.counterDebounceVar = {
                vars.counterDebounceKey, "counter_debounce_us", vars.moduleName, ConfigType::Int32,
                &digitalInputCfg[slot].counterDebounceUs, ConfigPersistence::Persistent, 0};
            vars.c0Var = {vars.c0Key, vars.c0Json, vars.moduleName, ConfigType::Float,
                          &digitalInputCfg[slot].c0, ConfigPersistence::Persistent, 0};
            vars.precisionVar = {vars.precisionKey, vars.precisionJson, vars.moduleName,
                                 ConfigType::Int32, &digitalInputCfg[slot].precision,
                                 ConfigPersistence::Persistent, 0};
            vars.modeVar = {vars.modeKey, "mode", vars.moduleName, ConfigType::UInt8,
                            &digitalInputCfg[slot].mode, ConfigPersistence::Persistent, 0};
            vars.counterTotalVar = {vars.counterTotalKey, "counter_total", vars.moduleName,
                                    ConfigType::Float, &digitalInputCfg[slot].counterTotal,
                                    ConfigPersistence::Persistent, 0};
        }

        for (uint8_t slot = 0; slot < Limits::Io::DigitalOutputConfigSlots; ++slot) {
            DigitalOutputSlot& vars = digitalOutputs[slot];
            snprintf(vars.nameKey, sizeof(vars.nameKey), "io_d%02unm", (unsigned)slot);
            snprintf(vars.bindingKey, sizeof(vars.bindingKey), "io_d%02ubp", (unsigned)slot);
            snprintf(vars.activeHighKey, sizeof(vars.activeHighKey), "io_d%02uah", (unsigned)slot);
            snprintf(vars.initialOnKey, sizeof(vars.initialOnKey), "io_d%02uin", (unsigned)slot);
            snprintf(vars.retainWarmKey, sizeof(vars.retainWarmKey), "io_d%02urt", (unsigned)slot);
            snprintf(vars.momentaryKey, sizeof(vars.momentaryKey), "io_d%02umo", (unsigned)slot);
            snprintf(vars.pulseKey, sizeof(vars.pulseKey), "io_d%02upm", (unsigned)slot);
            snprintf(vars.nameJson, sizeof(vars.nameJson), "d%02u_name", (unsigned)slot);
            snprintf(vars.activeHighJson, sizeof(vars.activeHighJson), "d%02u_active_high", (unsigned)slot);
            snprintf(vars.initialOnJson, sizeof(vars.initialOnJson), "d%02u_initial_on", (unsigned)slot);
            snprintf(vars.momentaryJson, sizeof(vars.momentaryJson), "d%02u_momentary", (unsigned)slot);
            snprintf(vars.pulseJson, sizeof(vars.pulseJson), "d%02u_pulse_ms", (unsigned)slot);
            snprintf(vars.moduleName, sizeof(vars.moduleName), "io/output/d%02u", (unsigned)slot);

            vars.nameVar = {vars.nameKey, vars.nameJson, vars.moduleName, ConfigType::CharArray,
                            digitalOutputCfg[slot].name, ConfigPersistence::Persistent,
                            sizeof(digitalOutputCfg[slot].name)};
            vars.bindingVar = {vars.bindingKey, "binding_port", vars.moduleName, ConfigType::UInt16,
                               &digitalOutputCfg[slot].bindingPort, ConfigPersistence::Persistent, 0};
            vars.activeHighVar = {vars.activeHighKey, vars.activeHighJson, vars.moduleName,
                                  ConfigType::Bool, &digitalOutputCfg[slot].activeHigh,
                                  ConfigPersistence::Persistent, 0};
            vars.initialOnVar = {vars.initialOnKey, vars.initialOnJson, vars.moduleName,
                                 ConfigType::Bool, &digitalOutputCfg[slot].initialOn,
                                 ConfigPersistence::Persistent, 0};
            vars.retainWarmVar = {vars.retainWarmKey, "retain_on_warm_reboot", vars.moduleName,
                                  ConfigType::Bool, &digitalOutputCfg[slot].retainOnWarmReboot,
                                  ConfigPersistence::Persistent, 0};
            vars.momentaryVar = {vars.momentaryKey, vars.momentaryJson, vars.moduleName,
                                 ConfigType::Bool, &digitalOutputCfg[slot].momentary,
                                 ConfigPersistence::Persistent, 0};
            vars.pulseVar = {vars.pulseKey, vars.pulseJson, vars.moduleName, ConfigType::Int32,
                             &digitalOutputCfg[slot].pulseMs, ConfigPersistence::Persistent, 0};
        }
    }
};

static_assert(IOConfigDescriptorStorage::NVS_KEY_CAPACITY >= 10U,
              "I/O descriptor key buffers are too small");
