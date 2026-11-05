#include "Profiles/Micronova/MicronovaProfile.h"
#include "Profiles/Micronova/MicronovaIoAssembly.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>

#include "App/AppContext.h"
#include "Board/BoardSerialMap.h"
#include "Core/DataStore/DataStore.h"
#include "Core/Log.h"
#include "Core/LogModuleIds.h"
#include "Core/MqttTopics.h"
#include "Core/NvsKeys.h"
#include "Core/SystemLimits.h"
#include "Core/Services/IHA.h"
#include "Core/SnprintfCheck.h"
#include "Core/SystemStats.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"

#undef snprintf
#define snprintf(OUT, LEN, FMT, ...) \
    FLOW_SNPRINTF_CHECKED_MODULE((LogModuleId)LogModuleIdValue::Core, OUT, LEN, FMT, ##__VA_ARGS__)

namespace {

using Profiles::Micronova::ModuleInstances;

enum class MicronovaBootMode : uint8_t {
    Provisioning = 0,
    Runtime = 1
};

struct MicronovaBootConfigState {
    bool wifiEnabled = true;
    bool wifiHasSsid = false;
    bool mqttEnabled = false;
    bool mqttHasHost = false;
};

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Log::error((LogModuleId)LogModuleIdValue::Core, "micronova setup failure: %s", step ? step : "unknown");
    if (!Log::hub()) {
        Serial.printf("Micronova setup failure: %s\r\n", step ? step : "unknown");
    }
    while (true) delay(1000);
}

bool prefHasNonEmptyString_(Preferences& prefs, const char* key, char* out, size_t outLen)
{
    if (!key || !out || outLen == 0U) return false;
    out[0] = '\0';
    const size_t n = prefs.getString(key, out, outLen);
    return (n > 1U) && (out[0] != '\0');
}

MicronovaBootConfigState readBootConfigState_(Preferences& prefs)
{
    MicronovaBootConfigState st{};
    char wifiSsid[33] = {0};
    char mqttHost[Limits::Mqtt::Buffers::Host] = {0};
    st.wifiEnabled = prefs.getBool(NvsKeys::Wifi::Enabled, true);
    st.wifiHasSsid = prefHasNonEmptyString_(prefs, NvsKeys::Wifi::Ssid, wifiSsid, sizeof(wifiSsid));
    st.mqttEnabled = prefs.getBool(NvsKeys::Mqtt::Enabled, false);
    st.mqttHasHost = prefHasNonEmptyString_(prefs, NvsKeys::Mqtt::Host, mqttHost, sizeof(mqttHost));
    return st;
}

bool runtimeConfigReady_(const BoardSpec& board, Preferences& prefs)
{
    const MicronovaBootConfigState st = readBootConfigState_(prefs);
    const bool wifiConfigured = st.wifiEnabled && st.wifiHasSsid;
    if (!wifiConfigured) return false;

    if (!board.provisioning.requireMqttForConfigured) return true;
    const bool mqttConfigured = st.mqttEnabled && st.mqttHasHost;
    return mqttConfigured;
}

MicronovaBootMode resolveBootMode_(const BoardSpec& board, Preferences& prefs)
{
    if (!board.provisioning.enabled) return MicronovaBootMode::Runtime;
    return runtimeConfigReady_(board, prefs) ? MicronovaBootMode::Runtime : MicronovaBootMode::Provisioning;
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

    const int wrote = snprintf(out,
                               len,
                               "{\"ready\":%s,\"ip\":\"%s\",\"mac\":\"%s\",\"rssi\":%d,\"mqtt\":%s,\"ts\":%lu}",
                               netReady ? "true" : "false",
                               ip,
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
        out,
        len,
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

void registerMicronovaHomeAssistant(AppContext& ctx, ModuleInstances& modules)
{
    const HAService* ha = ctx.services.get<HAService>(ServiceId::Ha);
    if (!ha) return;

    static constexpr const char* kRawValueTpl = "{{ value }}";
    static constexpr const char* kIntTpl = "{{ value | int(-1) }}";
    static constexpr const char* kFloatTpl = "{{ value | float(0) }}";
    static constexpr const char* kIoFloatTpl =
        "{% if value_json.value is number %}{{ value_json.value | float | round(1) }}{% else %}unavailable{% endif %}";
    static constexpr const char* kIoBoolSwitchTpl = "{% if value_json.value %}ON{% else %}OFF{% endif %}";
    static constexpr const char* kIoAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";

    if (ha->addBinarySensor) {
        const HABinarySensorEntry connection{
            "micronova",
            "mn_connection",
            "Boiler Connection",
            "micronova/status/connection",
            "{{ 'True' if value == 'online' else 'False' }}",
            "connectivity",
            nullptr,
            "mdi:connection"
        };
        (void)ha->addBinarySensor(ha->ctx, &connection);
    }

    if (ha->addSensor) {
        const HASensorEntry sensors[] = {
            {"micronova", "mn_state", "Boiler State", "micronova/status/stove_state", kRawValueTpl, nullptr, "mdi:fireplace", nullptr, false, nullptr, true},
            {"micronova", "mn_state_code", "Boiler State Code", "micronova/status/stove_state_code", kIntTpl, "diagnostic", "mdi:numeric", nullptr, false, nullptr, false},
            {"micronova", "mn_alarm_code", "Alarm Code", "micronova/status/alarm_code", kIntTpl, "diagnostic", "mdi:alert-outline", nullptr, false, nullptr, false},
            {"micronova", "mn_display_line1", "Display Line 1", "micronova/status/display_line_1", kRawValueTpl, "diagnostic", "mdi:text", nullptr, false, nullptr, true},
            {"micronova", "mn_display_line2", "Display Line 2", "micronova/status/display_line_2", kRawValueTpl, "diagnostic", "mdi:text", nullptr, false, nullptr, true},
            {"micronova", "mn_display_line3", "Display Line 3", "micronova/status/display_line_3", kRawValueTpl, "diagnostic", "mdi:text", nullptr, false, nullptr, true},
            {"micronova", "mn_room_temp", "Room Temperature", "micronova/sensor/ambtemp", kFloatTpl, nullptr, "mdi:home-thermometer-outline", "\xC2\xB0""C", false, nullptr, false},
            {"micronova", "mn_fumes_temp", "Fumes Temperature", "micronova/sensor/fumetemp", kFloatTpl, nullptr, "mdi:smoke", "\xC2\xB0""C", false, nullptr, false},
            {"micronova", "mn_water_temp", "Water Temperature", "micronova/sensor/water_temperature", kFloatTpl, nullptr, "mdi:water-thermometer", "\xC2\xB0""C", false, nullptr, false},
            {"micronova", "mn_water_pressure", "Water Pressure", "micronova/sensor/water_pressure", kFloatTpl, nullptr, "mdi:gauge", "bar", false, nullptr, false},
            {"micronova", "mn_last_command", "Last Command", "micronova/status/last_command", kRawValueTpl, "diagnostic", "mdi:console", nullptr, false, nullptr, true},
            {"micronova", "mn_local_temp", "Local Temperature", "rt/io/input/a00", kIoFloatTpl, nullptr, "mdi:thermometer", "\xC2\xB0""C", false, kIoAvailabilityTpl, false},
        };
        for (uint8_t i = 0; i < (uint8_t)(sizeof(sensors) / sizeof(sensors[0])); ++i) {
            const bool ok = ha->addSensor(ha->ctx, &sensors[i]);
            if (!ok) {
                Serial.printf("Micronova HA addSensor failed object=%s topic=%s\r\n",
                              sensors[i].objectSuffix,
                              sensors[i].stateTopicSuffix);
            }
        }
    }

    if (ha->addSwitch) {
        const HASwitchEntry power{
            "micronova",
            "mn_power",
            "Power",
            "micronova/status/onoff",
            kRawValueTpl,
            "micronova/command/power/set",
            "ON",
            "OFF",
            "mdi:power",
            nullptr
        };
        const HASwitchEntry auxOutput{
            "micronova",
            "mn_aux_output",
            "Ambiant Contact",
            "rt/io/output/d00",
            kIoBoolSwitchTpl,
            "micronova/io/output/d00/set",
            "ON",
            "OFF",
            "mdi:electric-switch",
            nullptr
        };
        (void)ha->addSwitch(ha->ctx, &power);
        (void)ha->addSwitch(ha->ctx, &auxOutput);
    }

    if (ha->addNumber) {
        const HANumberEntry numbers[] = {
            {"micronova", "mn_power_level", "Power Level", "micronova/sensor/power", kIntTpl, "micronova/command/power_level/set", "{{ value | int }}", 0.0f, 5.0f, 1.0f, "slider", nullptr, "mdi:fire", nullptr},
            {"micronova", "mn_target_temp", "Target Temperature", "micronova/sensor/tempset", kIntTpl, "micronova/command/temperature/set", "{{ value | int }}", 5.0f, 35.0f, 1.0f, "box", nullptr, "mdi:thermometer-lines", "\xC2\xB0""C"},
        };
        for (uint8_t i = 0; i < (uint8_t)(sizeof(numbers) / sizeof(numbers[0])); ++i) {
            (void)ha->addNumber(ha->ctx, &numbers[i]);
        }
    }

    if (ha->addButton) {
        const HAButtonEntry buttons[] = {
            {
                "micronova",
                "mn_refresh",
                "Refresh Boiler",
                "micronova/command/refresh",
                "1",
                "diagnostic",
                "mdi:refresh"
            },
            {
                "micronova",
                "mn_read_all_sweep",
                "Read All Registers",
                "micronova/command/read_all_sweep",
                "1",
                "diagnostic",
                "mdi:database-search"
            },
            {
                "micronova",
                "mn_p_plus_ir",
                "IR P+",
                "micronova/command/p_plus",
                "1",
                "config",
                "mdi:plus-circle-outline"
            },
            {
                "micronova",
                "mn_p_minus_ir",
                "IR P-",
                "micronova/command/p_minus",
                "1",
                "config",
                "mdi:minus-circle-outline"
            }
        };
        for (uint8_t i = 0; i < (uint8_t)(sizeof(buttons) / sizeof(buttons[0])); ++i) {
            (void)ha->addButton(ha->ctx, &buttons[i]);
        }
    }

    if (ha->requestRefresh) {
        (void)ha->requestRefresh(ha->ctx);
    }
    (void)modules;
}

void postInit(AppContext& ctx, ModuleInstances& modules)
{
    modules.mqttModule.formatTopic(modules.topicNetworkState, sizeof(modules.topicNetworkState), "rt/network/state");
    modules.mqttModule.formatTopic(modules.topicSystemState, sizeof(modules.topicSystemState), "rt/system/state");
    modules.mqttModule.addRuntimePublisher(modules.topicNetworkState, 60000, 0, false, buildNetworkSnapshot);
    modules.mqttModule.addRuntimePublisher(modules.topicSystemState, 60000, 0, false, buildSystemSnapshot);
    registerMicronovaHomeAssistant(ctx, modules);
}

}  // namespace

namespace Profiles {
namespace Micronova {

void setupProfile(AppContext& ctx)
{
    ModuleInstances& modules = moduleInstances();

    Serial.begin(Board::SerialMap::uart0Baud());
    delay(50);
    modules.haModule.setBranding("pio", "Pellet.io", "Pellet.io", "Pellet Controller");

    ctx.preferences.begin(NvsKeys::StorageNamespace, false);
    ctx.registry.setPreferences(ctx.preferences);
    requireSetup(ctx.board != nullptr, "missing board spec");

    const MicronovaBootMode bootMode = resolveBootMode_(*ctx.board, ctx.preferences);
    const bool provisioningMode = (bootMode == MicronovaBootMode::Provisioning);
    const MicronovaBootConfigState bootCfg = readBootConfigState_(ctx.preferences);
    modules.webInterfaceModule.setProvisioningOnly(provisioningMode);
    Serial.printf("Micronova boot mode: %s\r\n", provisioningMode ? "provisioning" : "runtime");
    Serial.printf("Micronova boot decision: wifi(enabled=%d,ssid=%d) mqtt(enabled=%d,host=%d) require_mqtt=%d provisioning_enabled=%d\r\n",
                  (int)bootCfg.wifiEnabled,
                  (int)bootCfg.wifiHasSsid,
                  (int)bootCfg.mqttEnabled,
                  (int)bootCfg.mqttHasHost,
                  (int)ctx.board->provisioning.requireMqttForConfigured,
                  (int)ctx.board->provisioning.enabled);

    ctx.moduleManager.add(&modules.logHubModule);
    ctx.moduleManager.add(&modules.logDispatcherModule);
    ctx.moduleManager.add(&modules.logSerialSinkModule);
    ctx.moduleManager.add(&modules.eventBusModule);

    ctx.moduleManager.add(&modules.configStoreModule);
    ctx.moduleManager.add(&modules.dataStoreModule);
    ctx.moduleManager.add(&modules.commandModule);
    ctx.moduleManager.add(&modules.wifiModule);
    if (provisioningMode) {
        ctx.moduleManager.add(&modules.mqttConfigModule);
        ctx.moduleManager.add(&modules.wifiProvisioningModule);
        ctx.moduleManager.add(&modules.webInterfaceModule);
    } else {
        ctx.moduleManager.add(&modules.webInterfaceModule);
        ctx.moduleManager.add(&modules.alarmModule);
        ctx.moduleManager.add(&modules.timeModule);
        ctx.moduleManager.add(&modules.mqttModule);
        ctx.moduleManager.add(&modules.haModule);
        ctx.moduleManager.add(&modules.ioModule);
        ctx.moduleManager.add(&modules.micronovaBusModule);
        ctx.moduleManager.add(&modules.micronovaBoilerModule);
        ctx.moduleManager.add(&modules.micronovaMqttBridgeModule);
        ctx.moduleManager.add(&modules.systemModule);
        modules.systemMonitorModule.setModuleManager(&ctx.moduleManager);
        ctx.moduleManager.add(&modules.systemMonitorModule);
        requireSetup(configureIoModule(*ctx.board, modules), "configure Micronova IO");
        requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.ioModule), "register runtime provider io");
    }

    requireSetup(ctx.moduleManager.initAll(ctx.registry, ctx.services), "init modules");
    if (!provisioningMode) {
        postInit(ctx, modules);
    }
}

void loopProfile(AppContext&)
{
    delay(20);
}

}  // namespace Micronova
}  // namespace Profiles
