#include "Profiles/FlowIO/FlowIOProfile.h"
#include "Profiles/FlowIO/FlowIOIoAssembly.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <string.h>

#include "App/AppContext.h"
#include "Board/BoardSpec.h"
#include "Board/BoardSerialMap.h"
#include "Core/DataStore/DataStore.h"
#include "Core/Log.h"
#include "Core/LogModuleIds.h"
#include "Core/NvsKeys.h"
#include "Core/SnprintfCheck.h"
#include "Core/SystemLimits.h"
#include "Core/SystemStats.h"
#include "Core/WokwiDefaultOverrides.h"
#include "Core/Services/IFlowCfg.h"
#include "Domain/Pool/PoolBehaviors.h"
#include "Domain/Pool/PoolIds.h"

#undef snprintf
#define snprintf(OUT, LEN, FMT, ...) \
    FLOW_SNPRINTF_CHECKED_MODULE((LogModuleId)LogModuleIdValue::Core, OUT, LEN, FMT, ##__VA_ARGS__)

namespace {

using Profiles::FlowIO::ModuleInstances;
I2CCfgServerModule* gI2cCfgServerForNetworkSnapshot = nullptr;

const PoolDevicePreset* findPoolPresetById(const DomainSpec& domain, PoolDeviceId id)
{
    for (uint8_t i = 0; i < domain.poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = domain.poolDevices[i];
        if (preset.id == id) return &preset;
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

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Log::error((LogModuleId)LogModuleIdValue::Core, "setup failure: %s", step ? step : "unknown");
    if (!Log::hub()) {
        Board::SerialMap::logSerial().printf("Setup failure: %s\r\n", step ? step : "unknown");
    }
    while (true) delay(1000);
}


bool buildNetworkSnapshot(MQTTModule* mqtt, char* out, size_t len)
{
    if (!mqtt || !out || len == 0) return false;
    DataStore* ds = mqtt->dataStorePtr();
    if (!ds) return false;

    IpV4 ip4 = networkIp(*ds);
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", ip4.b[0], ip4.b[1], ip4.b[2], ip4.b[3]);
    const bool netReady = networkReady(*ds);
    const bool mqttOk = mqttReady(*ds);
    const int rssi = WiFi.isConnected() ? WiFi.RSSI() : -127;
    char supervisorIp[20] = {0};
    if (!gI2cCfgServerForNetworkSnapshot ||
        !gI2cCfgServerForNetworkSnapshot->supervisorPeerIpText(supervisorIp, sizeof(supervisorIp), nullptr)) {
        snprintf(supervisorIp, sizeof(supervisorIp), "");
    }
    uint8_t mac[6] = {0};
    char macText[18] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(macText,
             sizeof(macText),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)mac[0],
             (unsigned)mac[1],
             (unsigned)mac[2],
             (unsigned)mac[3],
             (unsigned)mac[4],
             (unsigned)mac[5]);

    const int wrote = snprintf(out, len,
                               "{\"ready\":%s,\"ip\":\"%s\",\"sup_ip\":\"%s\",\"mac\":\"%s\",\"rssi\":%d,\"mqtt\":%s,\"ts\":%lu}",
                               netReady ? "true" : "false",
                               ip,
                               supervisorIp,
                               macText,
                               rssi,
                               mqttOk ? "true" : "false",
                               (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

bool buildSystemSnapshot(MQTTModule* mqtt, char* out, size_t len)
{
    if (!mqtt || !out || len == 0) return false;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    DataStore* ds = mqtt->dataStorePtr();
    const uint32_t rxDrop = ds ? mqttRxDrop(*ds) : 0U;
    const uint32_t parseFail = ds ? mqttParseFail(*ds) : 0U;
    const uint32_t handlerFail = ds ? mqttHandlerFail(*ds) : 0U;
    const uint32_t oversizeDrop = ds ? mqttOversizeDrop(*ds) : 0U;

    const int wrote = snprintf(
        out, len,
        "{\"upt_ms\":%llu,\"heap\":{\"free\":%lu,\"min_free\":%lu,\"largest\":%lu,\"frag\":%u},"
        "\"mqtt_rx\":{\"rx_drop\":%lu,\"oversize_drop\":%lu,\"parse_fail\":%lu,\"handler_fail\":%lu},\"ts\":%lu}",
        (unsigned long long)snap.uptimeMs64,
        (unsigned long)snap.heap.freeBytes,
        (unsigned long)snap.heap.minFreeBytes,
        (unsigned long)snap.heap.largestFreeBlock,
        (unsigned int)snap.heap.fragPercent,
        (unsigned long)rxDrop,
        (unsigned long)oversizeDrop,
        (unsigned long)parseFail,
        (unsigned long)handlerFail,
        (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

void registerModules(AppContext& ctx, ModuleInstances& modules)
{
    ctx.moduleManager.add(&modules.logHubModule);
    ctx.moduleManager.add(&modules.logDispatcherModule);
    ctx.moduleManager.add(&modules.logSerialSinkModule);
    ctx.moduleManager.add(&modules.eventBusModule);

    ctx.moduleManager.add(&modules.configStoreModule);
    ctx.moduleManager.add(&modules.dataStoreModule);
    ctx.moduleManager.add(&modules.commandModule);
    ctx.moduleManager.add(&modules.i2cCfgServerModule);
    ctx.moduleManager.add(&modules.hmiUdpServerModule);
    ctx.moduleManager.add(&modules.hmiModule);
    ctx.moduleManager.add(&modules.alarmModule);
    ctx.moduleManager.add(&modules.wifiModule);
    ctx.moduleManager.add(&modules.timeModule);
    ctx.moduleManager.add(&modules.mqttModule);
    ctx.moduleManager.add(&modules.haModule);
    ctx.moduleManager.add(&modules.systemModule);
    ctx.moduleManager.add(&modules.ioModule);
    ctx.moduleManager.add(&modules.poolLogicModule);
    ctx.moduleManager.add(&modules.poolDeviceModule);

    modules.systemMonitorModule.setModuleManager(&ctx.moduleManager);
    ctx.moduleManager.add(&modules.systemMonitorModule);
}

uint8_t dependsOnMaskForPreset(const DomainSpec& domain, const PoolDevicePreset& preset)
{
    if (preset.dependsOnDevice == POOL_DEVICE_INVALID) return 0;
    const PoolDevicePreset* dependency = findPoolPresetById(domain, preset.dependsOnDevice);
    if (!dependency) return 0;
    return (uint8_t)(1u << dependency->id);
}

void configurePoolDevices(const AppContext& ctx, ModuleInstances& modules)
{
    for (uint8_t i = 0; i < ctx.domain->poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = ctx.domain->poolDevices[i];
        const IoSlotId ioSlot = findIoSlotForDomainSlot(*ctx.domain, preset.commandSlot);
        requireSetup(ioSlot != IO_SLOT_INVALID, "missing pool device IO slot binding");

        PoolDeviceDefinition def{};
        snprintf(def.label, sizeof(def.label), "%s", preset.displayName);
        def.slot = preset.id;
        def.commandSlot = preset.commandSlot;
        def.ioSlot = ioSlot;
        def.type = preset.poolDeviceType;
        def.flowLPerHour = preset.flowLPerHour;
        def.tankCapacityMl = preset.tankCapacityMl;
        def.tankInitialMl = preset.tankInitialMl;
        def.dependsOnMask = dependsOnMaskForPreset(*ctx.domain, preset);
        def.maxUptimeDaySec = preset.maxUptimeDaySec;
        requireSetup(modules.poolDeviceModule.defineDevice(def), "define pool device");
    }
}

void postInit(AppContext& ctx, ModuleInstances& modules)
{
    const DataStoreService* dsSvc = ctx.services.get<DataStoreService>(ServiceId::DataStore);
    modules.ioDataStore = dsSvc ? dsSvc->store : nullptr;

    modules.mqttModule.formatTopic(modules.topicNetworkState, sizeof(modules.topicNetworkState), "rt/network/state");
    modules.mqttModule.formatTopic(modules.topicSystemState, sizeof(modules.topicSystemState), "rt/system/state");
    gI2cCfgServerForNetworkSnapshot = &modules.i2cCfgServerModule;
    modules.mqttModule.addRuntimePublisher(modules.topicNetworkState, 60000, 0, false, buildNetworkSnapshot);
    modules.mqttModule.addRuntimePublisher(modules.topicSystemState, 60000, 0, false, buildSystemSnapshot);
    registerIoHomeAssistant(ctx, modules);
}

}  // namespace

namespace Profiles {
namespace FlowIO {

void setupProfile(AppContext& ctx)
{
    ModuleInstances& modules = moduleInstances();

    Serial.begin(Board::SerialMap::uart0Baud());
    delay(50);

    ctx.preferences.begin(NvsKeys::StorageNamespace, false);
    ctx.registry.setPreferences(ctx.preferences);
    registerModules(ctx, modules);
    modules.hmiModule.setRemoteUdpServer(&modules.hmiUdpServerModule);
    configureIoModule(ctx, modules);
    configurePoolDevices(ctx, modules);

    // Keep PoolLogic runtime snapshots first so HA-critical state (including
    // heat_assist reason) stays available even when runtime route capacity is reached.
    requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.poolLogicModule), "register runtime provider poollogic");
    requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.ioModule), "register runtime provider io");
    requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.poolDeviceModule), "register runtime provider pooldev");
    requireSetup(modules.i2cCfgServerModule.registerRuntimeUiProvider(&modules.systemModule), "register runtime ui provider system");
    requireSetup(modules.i2cCfgServerModule.registerRuntimeUiProvider(&modules.wifiModule), "register runtime ui provider wifi");
    requireSetup(modules.i2cCfgServerModule.registerRuntimeUiProvider(&modules.mqttModule), "register runtime ui provider mqtt");
    requireSetup(modules.i2cCfgServerModule.registerRuntimeUiProvider(&modules.alarmModule), "register runtime ui provider alarms");
    requireSetup(modules.i2cCfgServerModule.registerRuntimeUiProvider(&modules.ioModule), "register runtime ui provider io");
    requireSetup(modules.i2cCfgServerModule.registerRuntimeUiProvider(&modules.poolDeviceModule), "register runtime ui provider pooldev");
    requireSetup(modules.i2cCfgServerModule.registerRuntimeUiProvider(&modules.poolLogicModule), "register runtime ui provider poollogic");

    requireSetup(ctx.moduleManager.initAll(ctx.registry, ctx.services), "init modules");
    postInit(ctx, modules);
}

void loopProfile(AppContext&)
{
    ModuleInstances& modules = moduleInstances();
    releaseIoHomeAssistantDiscoveryHeapIfDone(modules);
    refreshIoHomeAssistantIfNeeded(modules);
    delay(20);
}

}  // namespace FlowIO
}  // namespace Profiles
