#pragma once
/**
 * @file WebInterfaceModule.h
 * @brief Web interface module for Supervisor profile.
 *
 * Public facade only. The implementation is split across Server / WS / Log /
 * Lifecycle translation units.
 */

#include "Core/Module.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/Services/ILogger.h"
#include <HardwareSerial.h>
#include <ESPAsyncWebServer.h>
#include <freertos/queue.h>
#include <memory>
#include "Core/EventBus/EventBus.h"

#ifndef FLOW_ENABLE_WEB_SERIAL_TERMINAL
#define FLOW_ENABLE_WEB_SERIAL_TERMINAL 0
#endif

#ifndef FLOW_ENABLE_READONLY_SERIAL_LOG
#define FLOW_ENABLE_READONLY_SERIAL_LOG 1
#endif

struct BoardSpec;
#if defined(FLOW_PROFILE_WAVESHARE)
struct WebJsonBuffer;
#endif

class WebInterfaceModule : public Module {
public:
    explicit WebInterfaceModule(const BoardSpec& board);
    ~WebInterfaceModule() override;

    ModuleId moduleId() const override { return ModuleId::WebInterface; }
    const char* taskName() const override { return "webinterface"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return 4096;
#else
        return 4096;
#endif
    }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        // Keep AP provisioning web startup independent from heavy app modules.
        return 2;
#elif defined(FLOW_PROFILE_MICRONOVA)
        return 6;
#else
        return 7;
#endif
    }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::Wifi;
#if defined(FLOW_PROFILE_WAVESHARE)
        return ModuleId::Unknown;
#else
        if (i == 2) return ModuleId::EventBus;
        if (i == 3) return ModuleId::DataStore;
        if (i == 4) return ModuleId::Command;
        if (i == 5) return ModuleId::Hmi;
#if !defined(FLOW_PROFILE_MICRONOVA) && !defined(FLOW_PROFILE_WAVESHARE)
        if (i == 6) return ModuleId::I2cCfgClient;
#endif
        return ModuleId::Unknown;
#endif
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    bool canStart(ConfigStore&, ServiceRegistry& services) override {
#if defined(FLOW_PROFILE_WAVESHARE)
        const NetworkAccessService* net = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
        if (!net || !net->mode) return false;
        const NetworkAccessMode mode = net->mode(net->ctx);
        return mode == NetworkAccessMode::Station || mode == NetworkAccessMode::AccessPoint;
#else
        (void)services;
        return true;
#endif
    }
    void onStart(ConfigStore& cfg, ServiceRegistry& services) override;
    uint32_t startDelayMs() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return 3000U;
#else
        return Limits::Boot::WebInterfaceStartDelayMs;
#endif
    }
    void loop() override;
    void setProvisioningOnly(bool enabled) { provisioningOnly_ = enabled; }
    bool provisioningOnly() const { return provisioningOnly_; }

private:
    static constexpr int kServerPort = 80;
    static constexpr size_t kLocalLogLineMax = 192;

    // Keep UART framing aligned with core log entry limits.
    static constexpr size_t kSerialLogLineChars =
        (size_t)LOG_MSG_MAX + (size_t)LOG_MODULE_NAME_MAX + 64U;
#if FLOW_ENABLE_READONLY_SERIAL_LOG
    // Shared line buffer is reused by wslog dequeue path in read-only mode.
    static constexpr size_t kLineBufferSize = kLocalLogLineMax;
    static constexpr size_t kUartRxBufferSize = kLineBufferSize + 64U;
#else
    static constexpr size_t kLineBufferSize = kLocalLogLineMax;
    static constexpr size_t kUartRxBufferSize = 64U;
#endif

    // Server and network integration
    void startServer_();
    void startLocalRuntime_();
    void handleUpdateRequest_(AsyncWebServerRequest* request, FirmwareUpdateTarget target);
    bool isWebReachable_() const;
    bool getNetworkIp_(char* out, size_t len, NetworkAccessMode* modeOut) const;
    const char* networkTransport_(NetworkAccessMode mode) const;

    // WebSocket transport
    void onWsLogEvent_(AsyncWebSocket* server,
                       AsyncWebSocketClient* client,
                       AwsEventType type,
                       void* arg,
                       uint8_t* data,
                       size_t len);
    void flushLine_(bool force);
    void logWsFlowPressure_(const char* reason);
    void logWsLogPressure_(const char* reason);
    bool acquireRuntimeValuesBodyScratch_();
    void releaseRuntimeValuesBodyScratch_();
    void initRuntimeValuesBodyScratch_();
    void freeRuntimeValuesBodyScratch_();

    // Log formatting and local sink plumbing
    void flushLocalLogQueue_();
    static bool isLogByte_(uint8_t c);
    static char levelChar_(LogLevel lvl);
    static const char* levelColor_(LogLevel lvl);
    static const char* colorReset_();
    static bool isSystemTimeValid_();
    static void formatUptime_(char* out, size_t outSize, uint32_t ms);
    static void formatTimestamp_(WebInterfaceModule* self, const LogEntry& e, char* out, size_t outSize);
    static void formatLogEntryLine_(WebInterfaceModule* self,
                                    const LogEntry& e,
                                    char* out,
                                    size_t outSize,
                                    bool colorize);
    static void onLocalLogSinkWrite_(void* ctx, const LogEntry& e);
    static bool writeBootLogJsonLine_(void* writerCtx,
                                      const LogEntry& e,
                                      uint16_t index,
                                      uint16_t total);
    void dumpBootLogCapture_(AsyncWebSocketClient* client);
    void sendBootLogHttpResponse_(AsyncWebServerRequest* request, bool statusOnly);
    void sendActivityLogHttpResponse_(AsyncWebServerRequest* request, bool statusOnly);
    void emitConfigActivity_(const char* contextLabel, const char* modulesLabel, uint16_t fieldCount);
    void emitConfigPatchActivity_(const char* contextLabel, const char* patchJson);
    bool initLocalLogQueue_();
    void freeLocalLogQueue_();

    // Lifecycle and service surface
    bool setPaused_(bool paused);
    bool isPaused_() const;
    bool getHealth_(WebInterfaceHealth* out) const;
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void noteLoopActivity_();
    void noteHttpActivity_();
    void noteWsActivity_();
    void noteServerStarted_();
    static void onHttpActivityHook_(void* ctx);
    void scheduleReboot_(uint32_t delayMs, const char* reason);
    uint8_t wsActiveSource_() const;
    void setWsActiveSource_(uint8_t source);
#if defined(FLOW_PROFILE_WAVESHARE)
    void refreshIoResponseCaches_();
    void sendIoResponseCache_(AsyncWebServerRequest* request, bool topology);
#endif

    HardwareSerial& uart_ = Serial2;
    uint32_t uartBaud_ = 115200U;
    int uartRxPin_ = 16;
    int uartTxPin_ = 17;
    bool bridgeUartConfigured_ = false;
    bool bridgeUartEnabled_ = false;
    AsyncWebServer server_{kServerPort};
    AsyncWebSocket wsLog_{"/wslog"};

    const LogHubService* logHub_ = nullptr;
    const LogSinkRegistryService* logSinkReg_ = nullptr;
    const BootLogCaptureService* bootLogCapture_ = nullptr;
    const ActivityLogService* activityLog_ = nullptr;
    const TimeService* timeSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const HmiService* hmiSvc_ = nullptr;
    const FlowCfgRemoteService* flowCfgSvc_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    DataStore* dataStore_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    EventBus* eventBus_ = nullptr;
    bool started_ = false;
    bool spiffsReady_ = false;
    volatile bool netReady_ = false;
    volatile bool uartPaused_ = false;
    bool localLogSinkRegistered_ = false;
    const FirmwareUpdateService* fwUpdateSvc_ = nullptr;
    ServiceRegistry* services_ = nullptr;
    bool provisioningOnly_ = false;
    bool provisioningDisableAfterConfigured_ = false;
    bool provisioningRequireMqttForConfigured_ = false;
    bool rebootPending_ = false;
    uint32_t rebootAtMs_ = 0;
    char rebootReason_[24] = {0};
    bool webStartLedPulseActive_ = false;
    uint32_t webStartLedPulseUntilMs_ = 0U;
    bool webStartLedPrevAutoMode_ = true;
    bool webStartLedPrevAutoModeValid_ = false;

#if defined(FLOW_PROFILE_MICRONOVA)
    static constexpr UBaseType_t kLocalLogQueueLen = 6;
    static constexpr size_t kRuntimeValuesBodyMax = 512U;
    static constexpr size_t kRuntimeValuesJsonDocCapacity = 768U;
#elif defined(FLOW_PROFILE_WAVESHARE)
    static constexpr UBaseType_t kLocalLogQueueLen = 128;
    static_assert(kLocalLogQueueLen == 128, "flow.io wslog queue must keep 128 lines");
    static constexpr size_t kRuntimeValuesBodyMax = 4096U;
    static constexpr size_t kRuntimeValuesJsonDocCapacity = 4096U;
#else
    static constexpr UBaseType_t kLocalLogQueueLen = 12;
    static constexpr size_t kRuntimeValuesBodyMax = 2048U;
    static constexpr size_t kRuntimeValuesJsonDocCapacity = 2048U;
#endif
    QueueHandle_t localLogQueue_ = nullptr;
    StaticQueue_t* localLogQueueControl_ = nullptr;
    uint8_t* localLogQueueStorage_ = nullptr;
    bool localLogQueueStatic_ = false;
    bool localLogQueueStorageInPsram_ = false;
    static constexpr uint8_t kWsLogFlushBurstMax = 4U;

    char lineBuf_[kLineBufferSize] = {0};
    size_t lineLen_ = 0;
#if defined(FLOW_PROFILE_WAVESHARE)
    char* runtimeValuesBodyScratch_ = nullptr;
#else
    char runtimeValuesBodyScratchLocal_[kRuntimeValuesBodyMax + 1U] = {0};
    char* runtimeValuesBodyScratch_ = runtimeValuesBodyScratchLocal_;
#endif
    bool runtimeValuesBodyScratchInPsram_ = false;
    bool runtimeValuesBodyScratchOwned_ = false;
    portMUX_TYPE runtimeValuesBodyMux_ = portMUX_INITIALIZER_UNLOCKED;
    volatile bool runtimeValuesBodyBusy_ = false;
    uint32_t wsFlowConnectCount_ = 0;
    uint32_t wsFlowDisconnectCount_ = 0;
    uint32_t wsFlowSentCount_ = 0;
    uint32_t wsFlowDropCount_ = 0;
    uint32_t wsFlowPartialCount_ = 0;
    uint32_t wsFlowDiscardCount_ = 0;
    uint32_t wsFlowLastPressureLogMs_ = 0;
    uint32_t wsLogConnectCount_ = 0;
    uint32_t wsLogDisconnectCount_ = 0;
    uint32_t wsLogSentCount_ = 0;
    uint32_t wsLogDropCount_ = 0;
    uint32_t wsLogPartialCount_ = 0;
    uint32_t wsLogDiscardCount_ = 0;
    uint32_t wsLogCoalescedCount_ = 0;
    uint32_t wsLogLastPressureLogMs_ = 0;
    uint32_t wsLogPendingSummaryDrops_ = 0;
    mutable portMUX_TYPE wsSourceMux_ = portMUX_INITIALIZER_UNLOCKED;
    uint8_t wsSource_ = 0; // 0=supervisor local logs, 1=flow serial logs
    mutable portMUX_TYPE healthMux_ = portMUX_INITIALIZER_UNLOCKED;
    WebInterfaceHealth health_{};

#if defined(FLOW_PROFILE_WAVESHARE)
    std::shared_ptr<WebJsonBuffer> ioTopologyResponse_;
    std::shared_ptr<WebJsonBuffer> ioRuntimeResponse_;
    std::shared_ptr<WebJsonBuffer> ioRuntimeSpareResponse_;
    void* ioResponseSnapshot_ = nullptr;
    portMUX_TYPE ioResponseMux_ = portMUX_INITIALIZER_UNLOCKED;
    uint32_t ioTopologyChangeGeneration_ = 1U;
    uint32_t ioTopologyBuiltGeneration_ = 0U;
    uint32_t ioTopologyRevision_ = 0U;
    uint32_t ioLastTopologyBuildMs_ = 0U;
    uint32_t ioLastRuntimeBuildMs_ = 0U;
    uint32_t ioLastResponseBuildAttemptMs_ = 0U;
#endif

    WebInterfaceService webInterfaceSvc_{
        ServiceBinding::bind<&WebInterfaceModule::setPaused_>,
        ServiceBinding::bind<&WebInterfaceModule::isPaused_>,
        ServiceBinding::bind<&WebInterfaceModule::getHealth_>,
        this
    };
};
