#pragma once
/**
 * @file SystemMonitorModule.h
 * @brief Periodic system health/metrics reporting module.
 */
#include "Core/Module.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"
#include "Core/SystemStats.h"

// forward decl to avoid include dependency
class ModuleManager;

/**
 * @brief Active module that logs system metrics and health.
 */
class SystemMonitorModule : public Module {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::SystemMonitor; }
    /** @brief Task name. */
    const char* taskName() const override { return "sysmon"; }
    /** @brief Pin monitoring module on core 0. */
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 5120; }
    UBaseType_t taskStackCaps() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
        return Module::taskStackCaps();
#endif
    }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    // Only logger is mandatory
    /** @brief Depends on log hub. */
    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        return ModuleId::Unknown;
    }

    /** @brief Initialize monitoring. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry& services) override;
    /** @brief Monitoring loop. */
    void loop() override;

    // Needed to report task stack stats of all modules
    /** @brief Set ModuleManager for task stack reporting. */
    void setModuleManager(ModuleManager* mm) { moduleManager = mm; }

private:
    // Covers all ModuleManager-owned tasks plus FreeRTOS/network runtime tasks.
    // The buffer is allocated once during init and retained for the firmware lifetime.
    static constexpr size_t kTaskStatusSnapshotCapacity =
        Limits::Core::Capacity::MaxModuleTasks + 32U;
    static constexpr uint32_t kHeapWatchSamplePeriodMs = 50U;
    static constexpr uint32_t kHeapWatchTripFreeBytes = 2048U;
    static constexpr uint32_t kHeapWatchRecoverFreeBytes = 8192U;
    static constexpr uint32_t kHeapWatchDumpDelayMs = 1500U;
    static constexpr size_t kHeapWatchSampleCount = 160U;
    static constexpr size_t kHeapWatchDumpSampleCount = 24U;
    static constexpr size_t kHeapWatchTaskNameLen = 12U;
#ifdef CONFIG_HEAP_TASK_TRACKING
    static constexpr size_t kHeapWatchTaskTotalsMax = 12U;
#endif

    struct HeapWatchSample {
        uint32_t uptimeMs = 0;
        uint32_t freeBytes = 0;
        uint32_t minFreeBytes = 0;
        uint32_t largestFreeBlock = 0;
        uint32_t internalFreeBytes = 0;
        uint32_t internalMinFreeBytes = 0;
        uint32_t internalLargestFreeBlock = 0;
    };

#ifdef CONFIG_HEAP_TASK_TRACKING
    struct HeapWatchTaskTotal {
        uint32_t sizeBytes = 0;
        uint32_t blockCount = 0;
        char taskName[kHeapWatchTaskNameLen] = {0};
    };
#endif

    struct SystemMonitorConfig {
        int32_t tracePeriodMs = 5000;
        bool webWatchdogEnabled = true;
        int32_t webWatchdogCheckPeriodMs = 2000;
        int32_t webWatchdogStaleMs = 5000;
        int32_t webWatchdogBootGraceMs = 60000;
        int32_t webWatchdogMaxFailures = 3;
        bool webWatchdogAutoReboot = true;
    };

    ModuleManager* moduleManager = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    ServiceRegistry* services_ = nullptr;

    const WifiService* wifiSvc = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    const WebInterfaceService* webInterfaceSvc_ = nullptr;
    const FirmwareUpdateService* fwUpdateSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const ConfigStoreService* cfgSvc = nullptr;
    const LogHubService* logHub = nullptr;
    const HAService* haSvc_ = nullptr;
    TaskStatus_t* taskStatusSnapshot_ = nullptr;
    bool haEntitiesRegistered_ = false;

    uint32_t lastJsonDumpMs = 0;
    uint32_t lastHeapWatchSampleMs_ = 0;
    uint32_t heapWatchLastSeenMinFree_ = UINT32_MAX;
    uint32_t heapWatchTriggerMs_ = 0;
    uint32_t heapWatchTriggerFreeBytes_ = 0;
    uint32_t heapWatchTriggerMinFreeBytes_ = 0;
    uint32_t heapWatchTriggerLargestFreeBlock_ = 0;
    uint32_t traceCycleStartMs_ = 0;
    uint32_t lastWebWatchdogCheckMs_ = 0;
    uint32_t memoryPressureStateSinceMs_ = 0;
    size_t heapWatchWriteIndex_ = 0;
    size_t heapWatchCount_ = 0;
    size_t heapWatchFrozenWriteIndex_ = 0;
    size_t heapWatchFrozenCount_ = 0;
    uint8_t webWatchdogConsecutiveFailures_ = 0;
    bool bootInfoLogged_ = false;
    bool heapWatchTripActive_ = false;
    bool heapWatchDumpPending_ = false;
    bool webWatchdogRebootIssued_ = false;
    bool memoryPressureRebootIssued_ = false;
    bool stackLoggedThisCycle_ = false;
    bool heapLoggedThisCycle_ = false;
    bool buffersLoggedThisCycle_ = false;
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;
#if defined(FLOW_PROFILE_WAVESHARE)
    HeapWatchSample* heapWatchSamples_ = nullptr;
#else
    HeapWatchSample heapWatchSamples_[kHeapWatchSampleCount]{};
#endif
    char heapWatchTriggerReason_[20] = {0};
    uint8_t memoryPressureState_ = 0; // 0=normal,1=constrained,2=shedding,3=critical,4=panic
#ifdef CONFIG_HEAP_TASK_TRACKING
    HeapWatchTaskTotal heapWatchTaskTotals_[kHeapWatchTaskTotalsMax]{};
    size_t heapWatchTaskTotalCount_ = 0;
#endif

    void logBootInfo();
    void logHeapStats();
    void logTaskStacks();
    void logTrackedBuffers();
    void pollHeapWatch_(uint32_t now);
    void appendHeapWatchSample_(const SystemStatsSnapshot& snap);
    void armHeapWatchDump_(const SystemStatsSnapshot& snap, const char* reason);
    void dumpHeapWatch_();
    void dumpHeapWatchWindow_() const;
    void logPendingHeapAllocFailure_();
    void pollMemoryPressureReboot_(uint32_t now);
    void pollWebWatchdog_(uint32_t now);
#ifdef CONFIG_HEAP_TASK_TRACKING
    void captureHeapWatchTaskTotals_();
    void dumpHeapWatchTaskTotals_() const;
#endif
    void buildHealthJson(char* out, size_t outLen);
    void registerHaEntities_(ServiceRegistry& services);

    static const char* wifiStateStr(WifiState st);

    SystemMonitorConfig cfgData_{};
    ConfigVariable<int32_t,0> tracePeriodVar_{
        NVS_KEY(NvsKeys::SystemMonitor::TracePeriodMs), "trace_period_ms", "sysmon", ConfigType::Int32,
        &cfgData_.tracePeriodMs, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<bool,0> webWatchdogEnabledVar_{
        NVS_KEY(NvsKeys::SystemMonitor::WebWatchdogEnabled), "web_watchdog_enabled", "sysmon", ConfigType::Bool,
        &cfgData_.webWatchdogEnabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t,0> webWatchdogCheckPeriodVar_{
        NVS_KEY(NvsKeys::SystemMonitor::WebWatchdogCheckPeriodMs), "web_watchdog_check_period_ms", "sysmon", ConfigType::Int32,
        &cfgData_.webWatchdogCheckPeriodMs, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t,0> webWatchdogStaleVar_{
        NVS_KEY(NvsKeys::SystemMonitor::WebWatchdogStaleMs), "web_watchdog_stale_ms", "sysmon", ConfigType::Int32,
        &cfgData_.webWatchdogStaleMs, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t,0> webWatchdogBootGraceVar_{
        NVS_KEY(NvsKeys::SystemMonitor::WebWatchdogBootGraceMs), "web_watchdog_boot_grace_ms", "sysmon", ConfigType::Int32,
        &cfgData_.webWatchdogBootGraceMs, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t,0> webWatchdogMaxFailuresVar_{
        NVS_KEY(NvsKeys::SystemMonitor::WebWatchdogMaxFailures), "web_watchdog_max_failures", "sysmon", ConfigType::Int32,
        &cfgData_.webWatchdogMaxFailures, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<bool,0> webWatchdogAutoRebootVar_{
        NVS_KEY(NvsKeys::SystemMonitor::WebWatchdogAutoReboot), "web_watchdog_auto_reboot", "sysmon", ConfigType::Bool,
        &cfgData_.webWatchdogAutoReboot, ConfigPersistence::Persistent, 0
    };
};
