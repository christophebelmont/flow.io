#pragma once
/**
 * @file WifiModule.h
 * @brief WiFi connectivity module.
 */
#include "Board/BoardSpec.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Module.h"
#include "Core/RuntimeUi.h"
#include "Core/ServiceBinding.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/NvsKeys.h"
#include "Core/WokwiDefaultOverrides.h"
#include "Core/Services/Services.h"
#include <WiFi.h>
#include <ESPmDNS.h>

#ifndef ETH_TIMEOUT_MS
#define ETH_TIMEOUT_MS 7000UL
#endif

/** @brief WiFi configuration values. */
struct WifiConfig {
    bool enabled = true;
    // IEEE 802.11 SSID supports up to 32 bytes (+ '\0').
#if defined(FLOW_PROFILE_MICRONOVA) || defined(FLOW_PROFILE_WAVESHARE)
    char ssid[33] = "";
#else
    char ssid[33] = FLOW_WIRDEF_WIFI_SSID;
#endif
    // WPA/WPA2 supports 8..63 chars passphrase or 64-char hex PSK (+ '\0').
    char pass[65] = FLOW_WIRDEF_WIFI_PASS;
};

/**
 * @brief Active module that manages WiFi connectivity.
 */
class WifiModule : public Module, public IRuntimeUiValueProvider {
public:
    WifiModule() = default;
    explicit WifiModule(const BoardSpec& board);

    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::Wifi; }
    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }
    /** @brief Task name. */
    const char* taskName() const override { return "wifi"; }
    /** @brief Pin network module on core 0. */
    BaseType_t taskCore() const override { return 0; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    /** @brief Give extra headroom to WiFi stack/callback activity. */
    uint16_t taskStackSize() const override {
#if defined(FLOW_PROFILE_SUPERVISOR) || defined(FLOW_PROFILE_MICRONOVA)
        return 4096;
#elif defined(FLOW_PROFILE_WAVESHARE)
        return 3584;
#else
        return 2816;
#endif
    }

    /** @brief Depends on log hub, datastore and event bus. */
    uint8_t dependencyCount() const override { return 3; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        if (i == 2) return ModuleId::EventBus;
        return ModuleId::Unknown;
    }

    /** @brief Initialize WiFi config/services. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Apply loaded persistent config. */
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief WiFi task loop. */
    void loop() override;
    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override;

private:
    enum RuntimeUiValueId : uint8_t {
        RuntimeUiReady = 1,
        RuntimeUiIp = 2,
        RuntimeUiRssi = 3,
    };

    static constexpr uint8_t kScanMaxResults = Limits::Wifi::MaxScanResults;
#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
    static constexpr bool kScanIncludeHidden = false;
#else
    static constexpr bool kScanIncludeHidden = true;
#endif
    static constexpr uint32_t kScanThrottleMs = Limits::Wifi::Timing::ScanThrottleMs;
    static constexpr uint32_t kScanPrimaryDwellMs = 360U;
    static constexpr uint32_t kScanRetryDwellMs = 500U;
    static constexpr uint32_t kInitialConnectDelayMs = Limits::Wifi::Timing::InitialConnectDelayMs;
    static constexpr uint32_t kStartupTransientLogWindowMs = Limits::Wifi::Timing::StartupTransientLogWindowMs;

    struct WifiScanEntry {
        char ssid[33];
        int16_t rssi;
        uint8_t auth;
        bool hidden;
    };

    WifiConfig cfgData;
    WifiState state = WifiState::Idle;
    uint32_t stateTs = 0;
    const LogHubService* logHub = nullptr;
    DataStore* dataStore = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    EventBus* eventBus_ = nullptr;
    bool gotIpSent = false;
    bool mdnsStarted = false;
    uint32_t connectAttempt_ = 0;
    bool reconnectKickSent_ = false;
    bool staRetryEnabled_ = true;
    bool ethernetEnabled_ = false;
    bool hadSuccessfulConnection_ = false;
    wl_status_t lastConnectStatus_ = WL_IDLE_STATUS;
    uint8_t lastDisconnectReason_ = 0;
    uint32_t lastConnectingLogMs_ = 0;
    uint32_t initialConnectNotBeforeMs_ = 0;
    uint32_t startupTransientLogUntilMs_ = 0;
    wifi_event_id_t wifiEventHandlerId_ = 0;
    uint32_t lastEmptySsidLogMs = 0;
    uint32_t lastBeginMs_ = 0;
    uint32_t beginBackoffMs_ = 1500U;
    char deviceName_[33] = "flowio";
    char mdnsApplied[sizeof(deviceName_)] = {0};
    volatile bool deviceNameDirty_ = false;
    volatile bool scanRequested_ = false;
    volatile bool scanRunning_ = false;
    bool scanHasResults_ = false;
    int16_t scanLastError_ = 0;
    uint8_t scanCount_ = 0;
    uint8_t scanTotalFound_ = 0;
    uint8_t scanApRetryCount_ = 0;
    uint8_t scanFailRetryCount_ = 0;
    uint32_t scanLastStartMs_ = 0;
    uint32_t scanLastDoneMs_ = 0;
    uint32_t scanTimeoutMs_ = 0;
    uint16_t scanGeneration_ = 0;
    WifiScanEntry scanEntries_[kScanMaxResults] = {};
    portMUX_TYPE scanMux_ = portMUX_INITIALIZER_UNLOCKED;
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;
    const HAService* haSvc_ = nullptr;
    bool haEntitiesRegistered_ = false;
    
    // Config variables
    ConfigVariable<bool,0> enabledVar {
        NVS_KEY(NvsKeys::Wifi::Enabled),"enabled","wifi",
        ConfigType::Bool,
        &cfgData.enabled,
        ConfigPersistence::Persistent,
        0
    };

    ConfigVariable<char,0> ssidVar {
        NVS_KEY(NvsKeys::Wifi::Ssid),"ssid","wifi",
        ConfigType::CharArray,
        cfgData.ssid,
        ConfigPersistence::Persistent,
        sizeof(cfgData.ssid)
    };

    ConfigVariable<char,0> passVar {
        NVS_KEY(NvsKeys::Wifi::Pass),"pass","wifi",
        ConfigType::CharArray,
        cfgData.pass,
        ConfigPersistence::Persistent,
        sizeof(cfgData.pass)
    };

    // service
    WifiState stateSvc_() const;
    bool isConnected_() const;
    bool getIP_(char* out, size_t len) const;
    bool requestReconnect_();
    bool scanStatusJson_(char* out, size_t outLen);
    bool setStaRetryEnabled_(bool enabled);
    static bool cmdDumpCfg_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);

    void setState(WifiState s);
    static void onWifiEventSys_(arduino_event_t* event);
    static const char* stateName_(WifiState s);
    static const char* wlStatusName_(wl_status_t st);
    bool isStartupTransientWindow_() const;
    void logConfigSummary_() const;
    void startConnect();
    void stopMdns_();
    void syncMdns_();
    void loadSystemDeviceName_();
    void refreshEthernetConfig_(ConfigStore& cfg);
    bool preferredEthernetAvailable_() const;
    bool requestScan_(bool force);
    void processScan_();
    bool buildScanStatusJson_(char* out, size_t outLen);
    void registerHaEntities_(ServiceRegistry& services);

    WifiService wifiSvc_{
        ServiceBinding::bind<&WifiModule::stateSvc_>,
        ServiceBinding::bind<&WifiModule::isConnected_>,
        ServiceBinding::bind<&WifiModule::getIP_>,
        this,
        ServiceBinding::bind<&WifiModule::requestReconnect_>,
        ServiceBinding::bind<&WifiModule::requestScan_>,
        ServiceBinding::bind<&WifiModule::scanStatusJson_>,
        ServiceBinding::bind<&WifiModule::setStaRetryEnabled_>
    };
};
