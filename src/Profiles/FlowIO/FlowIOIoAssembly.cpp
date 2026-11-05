#include "Profiles/FlowIO/FlowIOIoAssembly.h"
#include "Profiles/FlowIO/FlowIOIoLayout.h"

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <esp_heap_caps.h>

#include "App/AppContext.h"
#include "Board/BoardSpec.h"
#include "Board/BoardSerialMap.h"
#include "Core/MqttTopics.h"
#include "Core/Log.h"
#include "Core/LogModuleIds.h"
#include "Core/Services/Services.h"
#include "Domain/Pool/PoolBehaviors.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/Network/HAModule/HARuntime.h"
#include "Profiles/FlowIO/FlowIOProfile.h"

#ifndef FLOW_HA_BOOT_TRACE
#define FLOW_HA_BOOT_TRACE 0
#endif

#if FLOW_HA_BOOT_TRACE
#define FLOWIO_HA_BOOT_TRACE(FMT, ...) Board::SerialMap::logSerial().printf("[HA-BOOT] " FMT "\r\n", ##__VA_ARGS__)
#else
#define FLOWIO_HA_BOOT_TRACE(FMT, ...) do {} while (0)
#endif

namespace {

using Profiles::FlowIO::ModuleInstances;
namespace FlowIoLayout = Profiles::FlowIO::IoLayout;
static constexpr uint8_t kFlowIoAnalogHaSlots = 17;

struct FlowIoAnalogHaSpec {
    const char* objectSuffix = nullptr;
    const char* name = nullptr;
    const char* icon = nullptr;
    const char* unit = nullptr;
};

struct FlowIoDigitalHaSpec {
    uint8_t logicalIdx = 0;
    const char* objectSuffix = nullptr;
    const char* name = nullptr;
    const char* icon = nullptr;
    const char* unit = nullptr;
};

constexpr FlowIoAnalogHaSpec kAnalogHaSpecs[kFlowIoAnalogHaSlots] = {
    {"io_orp", "ORP", "mdi:flash", "mV"},
    {"io_ph", "pH", "mdi:ph", ""},
    {"io_psi", "PSI", "mdi:gauge", "PSI"},
    {"io_spare", "Spare", "mdi:sine-wave", nullptr},
    {"io_wat_tmp", "Water Temperature", "mdi:water-thermometer", "\xC2\xB0""C"},
    {"io_air_tmp", "Air Temperature", "mdi:thermometer", "\xC2\xB0""C"},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
    {nullptr, nullptr, "mdi:sine-wave", nullptr},
};

constexpr FlowIoDigitalHaSpec kDigitalHaSpecs[] = {
    {0, "io_pool_lvl", "Pool Level", "mdi:waves-arrow-up", nullptr},
    {1, "io_ph_lvl", "pH Level", "mdi:flask-outline", nullptr},
    {2, "io_chl_lvl", "Chlorine Level", "mdi:test-tube", nullptr},
    {3, "io_wat_cnt", "Water Counter", "mdi:water-sync", "L"},
};

struct FlowIoDiscoveryHeap {
    char analogObjectSuffix[kFlowIoAnalogHaSlots][24]{};
    char analogFallbackName[kFlowIoAnalogHaSlots][24]{};
    char analogValueTpl[kFlowIoAnalogHaSlots][128]{};
    char analogStateSuffix[kFlowIoAnalogHaSlots][24]{};
    char digitalStateSuffix[sizeof(kDigitalHaSpecs) / sizeof(kDigitalHaSpecs[0])][24]{};
    char switchStateSuffix[Limits::Io::MaxPoolDevices][24]{};
    char switchPayloadOn[Limits::Io::MaxPoolDevices][Limits::IoHaSwitchPayloadBuf]{};
    char switchPayloadOff[Limits::Io::MaxPoolDevices][Limits::IoHaSwitchPayloadBuf]{};
};

FlowIoDiscoveryHeap* gDiscoveryHeap = nullptr;
bool gDiscoveryHeapReleaseWaitLogged = false;
bool gOneShotRefreshBypassedLogged = false;

bool ensureDiscoveryHeap()
{
    if (gDiscoveryHeap) return true;
    gDiscoveryHeap = static_cast<FlowIoDiscoveryHeap*>(
        heap_caps_calloc(1, sizeof(FlowIoDiscoveryHeap), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (!gDiscoveryHeap) {
        gDiscoveryHeap = static_cast<FlowIoDiscoveryHeap*>(
            heap_caps_calloc(1, sizeof(FlowIoDiscoveryHeap), MALLOC_CAP_8BIT)
        );
    }
    if (gDiscoveryHeap) {
        FLOWIO_HA_BOOT_TRACE("flow.io discovery heap allocated (%u bytes)", (unsigned)sizeof(FlowIoDiscoveryHeap));
    } else {
        FLOWIO_HA_BOOT_TRACE("flow.io discovery heap allocation failed (%u bytes)", (unsigned)sizeof(FlowIoDiscoveryHeap));
    }
    return gDiscoveryHeap != nullptr;
}

void releaseDiscoveryHeapIfReady(ModuleInstances& modules)
{
#if FLOW_HA_ONESHOT_DISCOVERY
    if (!gDiscoveryHeap || !modules.ioDataStore) return;
    if (!haAutoconfigPublished(*modules.ioDataStore)) {
        if (!gDiscoveryHeapReleaseWaitLogged) {
            FLOWIO_HA_BOOT_TRACE("flow.io discovery heap waiting for HA publish completion");
            gDiscoveryHeapReleaseWaitLogged = true;
        }
        return;
    }
    heap_caps_free(gDiscoveryHeap);
    gDiscoveryHeap = nullptr;
    gDiscoveryHeapReleaseWaitLogged = false;
    FLOWIO_HA_BOOT_TRACE("flow.io discovery heap released after HA one-shot publish");
#else
    (void)modules;
#endif
}

const DomainSlotPreset* findDomainSlotById(const DomainSpec& domain, DomainSlotId id)
{
    for (uint8_t i = 0; i < domain.domainSlotCount; ++i) {
        const DomainSlotPreset& slot = domain.domainSlots[i];
        if (slot.id == id) return &slot;
    }
    return nullptr;
}

IoSlotId findIoSlotForDomainSlot(const DomainSpec& domain, DomainSlotId id)
{
    for (uint8_t i = 0; i < domain.domainIoSlotBindingCount; ++i) {
        const DomainIoSlotBinding& binding = domain.domainIoSlotBindings[i];
        if (binding.domainSlot == id) return binding.ioSlot;
    }
    return IO_SLOT_INVALID;
}

const PoolDevicePreset* findPoolPresetById(const DomainSpec& domain, PoolDeviceId id)
{
    for (uint8_t i = 0; i < domain.poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = domain.poolDevices[i];
        if (preset.id == id) return &preset;
    }
    return nullptr;
}

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Log::error((LogModuleId)LogModuleIdValue::Core, "setup failure: %s", step ? step : "unknown");
    if (!Log::hub()) {
        Board::SerialMap::logSerial().printf("Setup failure: %s\r\n", step ? step : "unknown");
    }
    while (true) delay(1000);
}

void applyAnalogDefaultsForDomainSlot(DomainSlotId domainSlot, IOAnalogDefinition& def)
{
    const FlowIoLayout::AnalogRoleDefault* spec = FlowIoLayout::analogDefaultForDomainSlot(domainSlot);
    requireSetup(spec != nullptr, "unsupported analog domain slot");
    def.bindingPort = spec->bindingPort;
    def.c0 = spec->c0;
    def.c1 = spec->c1;
    def.precision = spec->precision;
}

void applyDigitalDefaultsForDomainSlot(DomainSlotId domainSlot, IODigitalInputDefinition& def)
{
    const FlowIoLayout::DigitalInputRoleDefault* spec = FlowIoLayout::digitalInputDefaultForDomainSlot(domainSlot);
    requireSetup(spec != nullptr, "unsupported digital input domain slot");
    def.bindingPort = spec->bindingPort;
    def.mode = spec->mode;
    def.edgeMode = spec->edgeMode;
    def.counterDebounceUs = spec->debounceUs;
}

void buildAnalogValueTemplate(const IOModule& ioModule, uint8_t analogIdx, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    const int32_t precision = ioModule.analogPrecision(analogIdx);
    snprintf(
        out,
        outLen,
        "{%% if value_json.value is number %%}{{ value_json.value | float | round(%ld) }}{%% else %%}unavailable{%% endif %%}",
        (long)precision
    );
}

void syncAnalogSensors(ModuleInstances& modules)
{
    if (!modules.haService || !modules.haService->addSensor) return;
    requireSetup(ensureDiscoveryHeap(), "ha discovery heap");
    static constexpr const char* kAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";

    for (uint8_t i = 0; i < kFlowIoAnalogHaSlots; ++i) {
        if (!modules.ioModule.analogSlotPublished(i)) continue;
        const FlowIoAnalogHaSpec& spec = kAnalogHaSpecs[i];

        buildAnalogValueTemplate(
            modules.ioModule,
            i,
            gDiscoveryHeap->analogValueTpl[i],
            sizeof(gDiscoveryHeap->analogValueTpl[i])
        );
        snprintf(
            gDiscoveryHeap->analogStateSuffix[i],
            sizeof(gDiscoveryHeap->analogStateSuffix[i]),
            "rt/io/input/a%02u",
            (unsigned)i
        );
        if (spec.objectSuffix) {
            snprintf(
                gDiscoveryHeap->analogObjectSuffix[i],
                sizeof(gDiscoveryHeap->analogObjectSuffix[i]),
                "%s",
                spec.objectSuffix
            );
        } else {
            snprintf(
                gDiscoveryHeap->analogObjectSuffix[i],
                sizeof(gDiscoveryHeap->analogObjectSuffix[i]),
                "io_a%02u",
                (unsigned)i
            );
        }
        snprintf(
            gDiscoveryHeap->analogFallbackName[i],
            sizeof(gDiscoveryHeap->analogFallbackName[i]),
            "A%02u",
            (unsigned)i
        );
        char endpointId[8] = {0};
        snprintf(endpointId, sizeof(endpointId), "a%02u", (unsigned)i);
        const char* label = spec.name;
        if (!label || label[0] == '\0') {
            label = modules.ioModule.endpointLabel(endpointId);
        }
        if (!label || label[0] == '\0') {
            label = gDiscoveryHeap->analogFallbackName[i];
        }
        const HASensorEntry entry{
            "io",
            gDiscoveryHeap->analogObjectSuffix[i],
            label,
            gDiscoveryHeap->analogStateSuffix[i],
            gDiscoveryHeap->analogValueTpl[i],
            nullptr,
            spec.icon,
            spec.unit,
            false,
            kAvailabilityTpl
        };
        (void)modules.haService->addSensor(modules.haService->ctx, &entry);
    }
}

void syncDigitalInputBinarySensors(ModuleInstances& modules)
{
    if (!modules.haService || !modules.haService->addBinarySensor || !modules.haService->addSensor) return;
    requireSetup(ensureDiscoveryHeap(), "ha discovery heap");
    static constexpr const char* kBoolTpl = "{{ 'True' if value_json.value else 'False' }}";
    static constexpr const char* kAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";
    static constexpr const char* kNumericTpl =
        "{% if value_json.value is number %}{{ value_json.value | float }}{% else %}unavailable{% endif %}";

    for (uint8_t i = 0; i < (uint8_t)(sizeof(kDigitalHaSpecs) / sizeof(kDigitalHaSpecs[0])); ++i) {
        const FlowIoDigitalHaSpec& spec = kDigitalHaSpecs[i];
        if (!modules.ioModule.digitalInputSlotPublished(spec.logicalIdx)) continue;

        snprintf(
            gDiscoveryHeap->digitalStateSuffix[i],
            sizeof(gDiscoveryHeap->digitalStateSuffix[i]),
            "rt/io/input/i%02u",
            (unsigned)spec.logicalIdx
        );
        if (modules.ioModule.digitalInputValueType(spec.logicalIdx) != IO_VAL_BOOL) {
            const HASensorEntry entry{
                "io",
                spec.objectSuffix,
                spec.name,
                gDiscoveryHeap->digitalStateSuffix[i],
                kNumericTpl,
                nullptr,
                spec.icon,
                spec.unit,
                false,
                kAvailabilityTpl
            };
            (void)modules.haService->addSensor(modules.haService->ctx, &entry);
            continue;
        }

        const HABinarySensorEntry entry{
            "io",
            spec.objectSuffix,
            spec.name,
            gDiscoveryHeap->digitalStateSuffix[i],
            kBoolTpl,
            nullptr,
            nullptr,
            spec.icon
        };
        (void)modules.haService->addBinarySensor(modules.haService->ctx, &entry);
    }
}

void syncSwitches(const DomainSpec& domain, ModuleInstances& modules)
{
    if (!modules.haService || !modules.haService->addSwitch) return;
    requireSetup(ensureDiscoveryHeap(), "ha discovery heap");

    for (uint8_t i = 0; i < domain.poolDeviceCount; ++i) {
        const PoolDevicePreset& device = domain.poolDevices[i];
        const DomainSlotPreset* commandSlot = findDomainSlotById(domain, device.commandSlot);
        if (!commandSlot) continue;
        const IoSlotId ioSlot = findIoSlotForDomainSlot(domain, device.commandSlot);
        if (ioSlot == IO_SLOT_INVALID || ioSlotKind(ioSlot) != IO_SLOT_DIGITAL_OUTPUT) continue;

        const uint8_t logical = ioSlotIndex(ioSlot);
        if (!modules.ioModule.digitalOutputSlotUsed(logical)) continue;

        snprintf(
            gDiscoveryHeap->switchStateSuffix[i],
            sizeof(gDiscoveryHeap->switchStateSuffix[i]),
            "rt/pdm/state/pd%u",
            (unsigned)device.id
        );
        bool payloadOk = true;

        if (device.id == PoolIds::DeviceFiltrationPump) {
            int wrote = snprintf(
                gDiscoveryHeap->switchPayloadOn[i],
                sizeof(gDiscoveryHeap->switchPayloadOn[i]),
                "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":true}}"
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOn[i]))) payloadOk = false;
            wrote = snprintf(
                gDiscoveryHeap->switchPayloadOff[i],
                sizeof(gDiscoveryHeap->switchPayloadOff[i]),
                "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":false}}"
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOff[i]))) payloadOk = false;
        } else {
            int wrote = snprintf(
                gDiscoveryHeap->switchPayloadOn[i],
                sizeof(gDiscoveryHeap->switchPayloadOn[i]),
                "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":true}}",
                (unsigned)device.id
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOn[i]))) payloadOk = false;
            wrote = snprintf(
                gDiscoveryHeap->switchPayloadOff[i],
                sizeof(gDiscoveryHeap->switchPayloadOff[i]),
                "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":false}}",
                (unsigned)device.id
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOff[i]))) payloadOk = false;
        }

        if (!payloadOk) {
            requireSetup(false, "ha switch payload");
            continue;
        }

        const HASwitchEntry entry{
            "io",
            device.objectSuffix,
            commandSlot->displayName,
            gDiscoveryHeap->switchStateSuffix[i],
            "{% if value_json.on %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCmd,
            gDiscoveryHeap->switchPayloadOn[i],
            gDiscoveryHeap->switchPayloadOff[i],
            device.haIcon,
            nullptr
        };
        (void)modules.haService->addSwitch(modules.haService->ctx, &entry);
    }
}

}  // namespace

namespace Profiles {
namespace FlowIO {

void configureIoModule(const AppContext& ctx, ModuleInstances& modules)
{
    requireSetup(ctx.domain != nullptr, "missing domain spec");

    modules.ioModule.setOneWireBuses(&modules.oneWireWater, &modules.oneWireAir);
    modules.ioModule.setBindingPorts(
        FlowIoLayout::kBindingPorts,
        (uint8_t)(sizeof(FlowIoLayout::kBindingPorts) / sizeof(FlowIoLayout::kBindingPorts[0]))
    );
    modules.ioModule.setExpanders(
        FlowIoLayout::kExpanders,
        (uint8_t)(sizeof(FlowIoLayout::kExpanders) / sizeof(FlowIoLayout::kExpanders[0]))
    );

    for (uint8_t i = 0; i < ctx.domain->domainSlotCount; ++i) {
        const DomainSlotPreset& preset = ctx.domain->domainSlots[i];
        const IoSlotId ioSlot = findIoSlotForDomainSlot(*ctx.domain, preset.id);
        if (ioSlot == IO_SLOT_INVALID) continue;
        const IoId ioId = ioIdFromSlot(ioSlot);
        requireSetup(ioId != IO_ID_INVALID, "invalid domain slot IO mapping");

        if (preset.slotKind == IO_SLOT_DIGITAL_INPUT) {
            IODigitalInputDefinition def{};
            snprintf(def.id, sizeof(def.id), "%s", preset.endpointId ? preset.endpointId : "input");
            def.ioId = ioId;
            def.activeHigh = false;
            def.pullMode = IO_PULL_UP;
            applyDigitalDefaultsForDomainSlot(preset.id, def);
            requireSetup(modules.ioModule.defineDigitalInput(def), "define digital input");
            continue;
        }

        if (preset.slotKind != IO_SLOT_ANALOG_INPUT) continue;

        IOAnalogDefinition def{};
        snprintf(def.id, sizeof(def.id), "%s", preset.endpointId ? preset.endpointId : "analog");
        def.ioId = ioId;
        applyAnalogDefaultsForDomainSlot(preset.id, def);
        requireSetup(modules.ioModule.defineAnalogInput(def), "define analog input");
    }

    for (uint8_t i = 6; i < 17; ++i) {
        IOAnalogDefinition def{};
        snprintf(def.id, sizeof(def.id), "a%02u", (unsigned)i);
        def.ioId = (IoId)(IO_ID_AI_BASE + i);
        requireSetup(modules.ioModule.defineAnalogInput(def), "define extra analog input");
    }

    for (uint8_t i = 0; i < ctx.domain->domainSlotCount; ++i) {
        const DomainSlotPreset& preset = ctx.domain->domainSlots[i];
        if (preset.slotKind != IO_SLOT_DIGITAL_OUTPUT) continue;
        const IoSlotId ioSlot = findIoSlotForDomainSlot(*ctx.domain, preset.id);
        if (ioSlot == IO_SLOT_INVALID) continue;
        requireSetup(ioSlotKind(ioSlot) == IO_SLOT_DIGITAL_OUTPUT, "domain output mapped to non-output slot");

        const FlowIoLayout::DigitalOutputRoleDefault* spec = FlowIoLayout::digitalOutputDefaultForDomainSlot(preset.id);
        requireSetup(spec != nullptr, "missing output layout binding");

        IODigitalOutputDefinition def{};
        snprintf(def.id, sizeof(def.id), "%s", preset.endpointId ? preset.endpointId : "output");
        def.ioId = ioIdFromSlot(ioSlot);
        def.bindingPort = spec->bindingPort;
        def.activeHigh = spec->activeHigh;
        def.initialOn = false;
        def.startupPolicy = spec->retainOnWarmReboot
            ? IOOutputStartupPolicy::PreserveHardwareState
            : IOOutputStartupPolicy::ApplyInitial;
        def.retainOnWarmReboot = spec->retainOnWarmReboot;
        def.momentary = spec->momentary;
        def.pulseMs = spec->momentary ? spec->pulseMs : 0;
        requireSetup(modules.ioModule.defineDigitalOutput(def), "define digital output");
    }
}

void registerIoHomeAssistant(AppContext& ctx, ModuleInstances& modules)
{
    modules.haService = ctx.services.get<HAService>(ServiceId::Ha);
    if (!modules.haService) return;

    syncAnalogSensors(modules);
    syncDigitalInputBinarySensors(modules);
    syncSwitches(*ctx.domain, modules);

    if (modules.haService->requestRefresh) {
        (void)modules.haService->requestRefresh(modules.haService->ctx);
    }
}

void refreshIoHomeAssistantIfNeeded(ModuleInstances& modules)
{
#if FLOW_HA_ONESHOT_DISCOVERY
    if (!gOneShotRefreshBypassedLogged) {
        FLOWIO_HA_BOOT_TRACE("flow.io IO->HA dynamic refresh bypassed in one-shot mode");
        gOneShotRefreshBypassedLogged = true;
    }
    releaseDiscoveryHeapIfReady(modules);
    return;
#endif
    if (!modules.haService) return;
    const uint32_t dirtyMask = modules.ioModule.takeAnalogConfigDirtyMask();
    if (dirtyMask == 0) return;

    syncAnalogSensors(modules);
    if (modules.haService->requestRefresh) {
        (void)modules.haService->requestRefresh(modules.haService->ctx);
    }
}

void releaseIoHomeAssistantDiscoveryHeapIfDone(ModuleInstances& modules)
{
    releaseDiscoveryHeapIfReady(modules);
}

}  // namespace FlowIO
}  // namespace Profiles
