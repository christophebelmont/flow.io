/**
 * @file SystemMonitorModule.cpp
 * @brief Implementation file.
 */
#include "SystemMonitorModule.h"
#include "Core/BufferUsageTracker.h"
#include "Core/ModuleManager.h"   ///< required for iteration
#include "Core/Services/ILogger.h"
#include <Arduino.h>
#include <WiFi.h>                ///< only for RSSI (optional)
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <new>
#include <string.h>
#ifdef CONFIG_HEAP_TASK_TRACKING
#include <esp_heap_task_info.h>
#endif
#ifndef FLOW_WEB_HEAP_FORENSICS
#define FLOW_WEB_HEAP_FORENSICS 0
#endif
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::SystemMonitorModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr uint8_t kSysMonCfgProducerId = 45;
static constexpr uint8_t kSysMonCfgBranch = 1;
static constexpr uint32_t kWebWatchdogMinCheckPeriodMs = 250U;
static constexpr uint32_t kWebWatchdogMinStaleMs = 1000U;
static constexpr uint32_t kWebWatchdogMinBootGraceMs = 5000U;
static constexpr uint32_t kWebWatchdogClientIdleFactor = 2U;
static constexpr uint8_t kWebWatchdogMaxFailuresCap = 20U;
static constexpr uint32_t kPressurePanicRebootDelayMs = 5000U;
static constexpr uint32_t kPressureCriticalRebootDelayMs = 15000U;
static constexpr MqttConfigRouteProducer::Route kSysMonCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::SystemMonitor, kSysMonCfgBranch}, "sysmon", "sysmon", (uint8_t)MqttPublishPriority::Normal, nullptr},
};

volatile bool gHeapAllocFailedPending = false;
volatile size_t gHeapAllocFailedSize = 0;
volatile uint32_t gHeapAllocFailedCaps = 0;
const char* volatile gHeapAllocFailedFunction = nullptr;

enum class MemoryPressureState : uint8_t {
    Normal = 0,
    Constrained = 1,
    Shedding = 2,
    Critical = 3,
    Panic = 4,
};

MemoryPressureState deriveMemoryPressureState_(const SystemStatsSnapshot& snap)
{
    const uint32_t freeBytes = snap.heap.internalFreeBytes;
    const uint32_t largestBytes = snap.heap.internalLargestFreeBlock;
    const uint8_t frag = snap.heap.internalFragPercent;

    // Keep "shedding" below 20KB free heap, then tighten higher states to
    // reduce warning churn while preserving severe-state protection.
    if (freeBytes < 12000U && largestBytes < 5000U && frag > 55U) return MemoryPressureState::Panic;
    if (freeBytes < 16000U && largestBytes < 7000U && frag > 45U) return MemoryPressureState::Critical;
    if (freeBytes < 20000U && largestBytes < 10000U && frag > 35U) return MemoryPressureState::Shedding;
    if (freeBytes < 24000U && largestBytes < 14000U && frag > 28U) return MemoryPressureState::Constrained;
    return MemoryPressureState::Normal;
}

MemoryPressureState applyMemoryPressureHysteresis_(const SystemStatsSnapshot& snap,
                                                   MemoryPressureState previous)
{
    const MemoryPressureState derived = deriveMemoryPressureState_(snap);
    if ((uint8_t)derived >= (uint8_t)previous) return derived;

    const uint32_t freeBytes = snap.heap.internalFreeBytes;
    const uint32_t largestBytes = snap.heap.internalLargestFreeBlock;
    const uint8_t frag = snap.heap.internalFragPercent;

    // Escalation uses the strict thresholds above immediately. Recovery needs
    // extra margin so short-lived heap allocations cannot make adjacent states
    // alternate and emit a warning on every sysmon pass.
    switch (previous) {
    case MemoryPressureState::Panic:
        if (freeBytes < 14000U && largestBytes < 6000U && frag > 50U) return previous;
        break;
    case MemoryPressureState::Critical:
        if (freeBytes < 18000U && largestBytes < 8500U && frag > 40U) return previous;
        break;
    case MemoryPressureState::Shedding:
        if (freeBytes < 24000U && largestBytes < 12000U && frag > 30U) return previous;
        break;
    case MemoryPressureState::Constrained:
        if (freeBytes < 28000U && largestBytes < 16000U && frag > 22U) return previous;
        break;
    case MemoryPressureState::Normal:
    default:
        break;
    }
    return derived;
}

const char* memoryPressureStateStr_(MemoryPressureState st)
{
    switch (st) {
    case MemoryPressureState::Normal: return "normal";
    case MemoryPressureState::Constrained: return "constrained";
    case MemoryPressureState::Shedding: return "shedding";
    case MemoryPressureState::Critical: return "critical";
    case MemoryPressureState::Panic: return "panic";
    default: return "normal";
    }
}

void onHeapAllocFailed_(size_t size, uint32_t caps, const char* functionName)
{
    gHeapAllocFailedSize = size;
    gHeapAllocFailedCaps = caps;
    gHeapAllocFailedFunction = functionName;
    gHeapAllocFailedPending = true;
}

#ifdef CONFIG_HEAP_TASK_TRACKING
void copyTaskName_(char* out, size_t outLen, TaskHandle_t task)
{
    if (!out || outLen == 0U) return;
    out[0] = '\0';

    const char* name = task ? pcTaskGetName(task) : pcTaskGetName(nullptr);
    if (!name || !name[0]) {
        snprintf(out, outLen, "%s", "-");
        return;
    }

    snprintf(out, outLen, "%s", name);
}
#endif

bool isLikelyValidTaskHandle_(TaskHandle_t handle)
{
    const uintptr_t raw = (uintptr_t)handle;
    if (raw == 0U) return false;
    // Task handles are TCB pointers in DRAM; lower bounds differ by target.
    // ESP32-S3 (and RISC-V families) commonly allocate around 0x3FCxxxxx.
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
    if (raw < 0x3FC00000U) return false;
#else
    if (raw < 0x3FF00000U) return false;
#endif
    if (raw > 0x60000000U) return false;
    return true;
}

}

const char* SystemMonitorModule::wifiStateStr(WifiState st) {
    switch (st) {
    case WifiState::Disabled:    return "Disabled";
    case WifiState::Idle:        return "Idle";
    case WifiState::Connecting:  return "Connecting";
    case WifiState::Connected:   return "Connected";
    case WifiState::ErrorWait:   return "ErrorWait";
    default:                     return "Unknown";
    }
}

void SystemMonitorModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::SystemMonitor;
    constexpr uint8_t kCfgBranchId = kSysMonCfgBranch;
    services_ = &services;
    cfgStore_ = &cfg;
    cfg.registerVar(tracePeriodVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(webWatchdogEnabledVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(webWatchdogCheckPeriodVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(webWatchdogStaleVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(webWatchdogBootGraceVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(webWatchdogMaxFailuresVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(webWatchdogAutoRebootVar_, kCfgModuleId, kCfgBranchId);

    wifiSvc = services.get<WifiService>(ServiceId::Wifi);
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    webInterfaceSvc_ = services.get<WebInterfaceService>(ServiceId::WebInterface);
    fwUpdateSvc_ = services.get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    cfgSvc  = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    logHub  = services.get<LogHubService>(ServiceId::LogHub);
    haSvc_  = services.get<HAService>(ServiceId::Ha);

#if defined(configUSE_TRACE_FACILITY) && (configUSE_TRACE_FACILITY == 1) && \
    !defined(FLOW_PROFILE_MICRONOVA)
#if defined(FLOW_PROFILE_WAVESHARE)
    constexpr uint32_t taskSnapshotCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    constexpr const char* taskSnapshotMemory = "PSRAM";
#else
    constexpr uint32_t taskSnapshotCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    constexpr const char* taskSnapshotMemory = "internal";
#endif
    taskStatusSnapshot_ = static_cast<TaskStatus_t*>(
        heap_caps_calloc(kTaskStatusSnapshotCapacity, sizeof(TaskStatus_t), taskSnapshotCaps)
    );
    if (taskStatusSnapshot_) {
        LOGI("Task snapshot allocated capacity=%u bytes=%lu memory=%s",
             (unsigned)kTaskStatusSnapshotCapacity,
             (unsigned long)(kTaskStatusSnapshotCapacity * sizeof(TaskStatus_t)),
             taskSnapshotMemory);
    } else {
        LOGW("Task snapshot unavailable capacity=%u bytes=%lu memory=%s",
             (unsigned)kTaskStatusSnapshotCapacity,
             (unsigned long)(kTaskStatusSnapshotCapacity * sizeof(TaskStatus_t)),
             taskSnapshotMemory);
    }
#endif

#if FLOW_WEB_HEAP_FORENSICS
#if defined(FLOW_PROFILE_WAVESHARE)
    heapWatchSamples_ = static_cast<HeapWatchSample*>(
        heap_caps_calloc(kHeapWatchSampleCount,
                         sizeof(HeapWatchSample),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (heapWatchSamples_) {
        LOGI("HeapWatch history allocated in PSRAM bytes=%lu",
             (unsigned long)(kHeapWatchSampleCount * sizeof(HeapWatchSample)));
    } else {
        LOGW("HeapWatch history unavailable: PSRAM allocation failed bytes=%lu",
             (unsigned long)(kHeapWatchSampleCount * sizeof(HeapWatchSample)));
    }
#endif
    const esp_err_t allocHookErr = heap_caps_register_failed_alloc_callback(&onHeapAllocFailed_);
    if (allocHookErr != ESP_OK) {
        LOGW("Heap alloc-fail hook registration failed err=%d", (int)allocHookErr);
    }
#endif
}

void SystemMonitorModule::registerHaEntities_(ServiceRegistry& services)
{
    if (haEntitiesRegistered_) return;
    if (!haSvc_) haSvc_ = services.get<HAService>(ServiceId::Ha);
    if (!haSvc_ || !haSvc_->addSensor) return;

    const HASensorEntry uptimeMinutes{
        "system",
        "sys_upt_mn",
        "Uptime",
        "rt/system/state",
        "{{ ((value_json.upt_ms | float(0)) / 60000) | round(0) | int(0) }}",
        "diagnostic",
        "mdi:timer-outline",
        "mn",
        false,
        nullptr
    };
    const HASensorEntry heapFreeBytes{
        "system",
        "sys_hp_free",
        "Heap Free",
        "rt/system/state",
        "{{ ((value_json.heap.free | float(0)) / 1024) | round(1) }}",
        "diagnostic",
        "mdi:memory",
        "ko",
        false,
        nullptr
    };
    const HASensorEntry heapMinFreeBytes{
        "system",
        "sys_hp_min_free",
        "Heap Min Free",
        "rt/system/state",
        "{{ ((value_json.heap.min_free | float(0)) / 1024) | round(1) }}",
        "diagnostic",
        "mdi:memory",
        "ko",
        false,
        nullptr
    };
    const HASensorEntry heapFragPercent{
        "system",
        "sys_hp_frag",
        "Heap Fragmentation",
        "rt/system/state",
        "{{ value_json.heap.frag | int(0) }}",
        "diagnostic",
        "mdi:chart-donut",
        "%",
        false,
        nullptr
    };

    bool ok = true;
    ok = haSvc_->addSensor(haSvc_->ctx, &uptimeMinutes) && ok;
    ok = haSvc_->addSensor(haSvc_->ctx, &heapFreeBytes) && ok;
    ok = haSvc_->addSensor(haSvc_->ctx, &heapMinFreeBytes) && ok;
    ok = haSvc_->addSensor(haSvc_->ctx, &heapFragPercent) && ok;
    if (ok) {
        haEntitiesRegistered_ = true;
    } else {
        LOGW("HA registration failed: system monitor entities");
    }
}

void SystemMonitorModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kSysMonCfgProducerId,
                               kSysMonCfgRoutes,
                               (uint8_t)(sizeof(kSysMonCfgRoutes) / sizeof(kSysMonCfgRoutes[0])),
                               services);
    }
    registerHaEntities_(services);
}

void SystemMonitorModule::logBootInfo() {
    LOGI("Reset reason=%s", SystemStats::resetReasonStr());
    LOGI("CPU=%luMHz", (unsigned long)ESP.getCpuFreqMHz());
    const bool psramOk = psramFound();
    const uint32_t psramSizeBytes = ESP.getPsramSize();
    LOGI("PSRAM found=%s size=%luKB free=%luKB",
         psramOk ? "yes" : "no",
         (unsigned long)(psramSizeBytes / 1024U),
         (unsigned long)(ESP.getFreePsram() / 1024U));
}

void SystemMonitorModule::logHeapStats() {
    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);

    if (!logHub || !logHub->getStats) {
        LOGD("Heap total8 free=%lu min=%lu largest=%lu internal free=%lu min=%lu largest=%lu internal_frag=%u%%",
             (unsigned long)snap.heap.freeBytes,
             (unsigned long)snap.heap.minFreeBytes,
             (unsigned long)snap.heap.largestFreeBlock,
             (unsigned long)snap.heap.internalFreeBytes,
             (unsigned long)snap.heap.internalMinFreeBytes,
             (unsigned long)snap.heap.internalLargestFreeBlock,
             (unsigned int)snap.heap.internalFragPercent);
        return;
    }

    LogHubStatsSnapshot stats{};
    logHub->getStats(logHub->ctx, &stats);
    LOGD("Heap total8 free=%lu min=%lu largest=%lu internal free=%lu min=%lu largest=%lu internal_frag=%u%% LogQ=%u/%u drop=%lu trunc=%lu",
         (unsigned long)snap.heap.freeBytes,
         (unsigned long)snap.heap.minFreeBytes,
         (unsigned long)snap.heap.largestFreeBlock,
         (unsigned long)snap.heap.internalFreeBytes,
         (unsigned long)snap.heap.internalMinFreeBytes,
         (unsigned long)snap.heap.internalLargestFreeBlock,
         (unsigned int)snap.heap.internalFragPercent,
         (unsigned)stats.peakQueued,
         (unsigned)stats.queueLen,
         (unsigned long)stats.droppedCount,
         (unsigned long)stats.formatTruncCount);

    if (stats.droppedCount > 0 || stats.formatTruncCount > 0) {
        const char* lastDropModule =
            (logHub->resolveModuleName && stats.lastDropModuleId != (LogModuleId)LogModuleIdValue::Unknown)
                ? logHub->resolveModuleName(logHub->ctx, stats.lastDropModuleId)
                : "-";
        const char* lastTruncModule =
            (logHub->resolveModuleName && stats.lastFormatTruncModuleId != (LogModuleId)LogModuleIdValue::Unknown)
                ? logHub->resolveModuleName(logHub->ctx, stats.lastFormatTruncModuleId)
                : "-";
        LOGD("LogQ src drop=%s trunc=%s now=%u",
             lastDropModule ? lastDropModule : "-",
             lastTruncModule ? lastTruncModule : "-",
             (unsigned)stats.queuedNow);
    }
}

void SystemMonitorModule::logTaskStacks() {
#if defined(FLOW_PROFILE_MICRONOVA)
    static bool warned = false;
    if (!warned) {
        warned = true;
        LOGW("Stack task monitoring disabled on Micronova profile (stability guard)");
    }
    return;
#endif

    if (!moduleManager) {
        LOGD("ModuleManager not set, task stats disabled");
        return;
    }

    static constexpr char kStackPrefix[] = "Stack ";
    static constexpr uint8_t kMaxTasksPerLine = 3;
    static constexpr size_t kLineMsgBudget = (size_t)LOG_MSG_MAX - sizeof(kStackPrefix);
    char line[kLineMsgBudget + 1];
    size_t off = 0;
    line[0] = '\0';
    uint8_t tasksOnLine = 0;
    bool hasTask = false;
    uint16_t skippedInvalidHandles = 0;
    uint16_t removedEndedTasks = 0;

#if defined(configUSE_TRACE_FACILITY) && (configUSE_TRACE_FACILITY == 1)
    UBaseType_t liveTaskCount = uxTaskGetNumberOfTasks();
    if (liveTaskCount == 0U) {
        LOGD("Stack none");
        return;
    }
    TaskStatus_t* const liveTasks = taskStatusSnapshot_;
    if (!liveTasks) {
        LOGW("Stack snapshot unavailable (persistent buffer not allocated tasks=%u)",
             (unsigned)liveTaskCount);
        return;
    }
    if (liveTaskCount > kTaskStatusSnapshotCapacity) {
        LOGW("Stack snapshot capacity exceeded tasks=%u capacity=%u",
             (unsigned)liveTaskCount,
             (unsigned)kTaskStatusSnapshotCapacity);
        return;
    }
    liveTaskCount =
        uxTaskGetSystemState(liveTasks, (UBaseType_t)kTaskStatusSnapshotCapacity, nullptr);
    if (liveTaskCount == 0U) {
        LOGD("Stack none");
        return;
    }
#else
    static bool warnedNoTraceFacility = false;
    if (!warnedNoTraceFacility) {
        warnedNoTraceFacility = true;
        LOGW("Stack task monitoring disabled (configUSE_TRACE_FACILITY not enabled)");
    }
    return;
#endif

    uint8_t n = moduleManager->getTaskEntryCount();
    for (uint8_t i = 0; i < n; ++i) {
        const ModuleManager::TaskEntry* task = moduleManager->getTaskEntry(i);
        if (!task || !task->module) continue;

        // Snapshot the task handle once to avoid a race where another core
        // clears/replaces it between validation and the watermark query.
        const TaskHandle_t taskHandle = task->handle;
        if (!taskHandle) {
            ++skippedInvalidHandles;
            continue;
        }

        const ModuleTaskSpec* specs = task->module->taskSpecs();
        const uint8_t taskCount = task->module->taskCount();
        if (!specs || task->taskIndex >= taskCount) continue;
        const ModuleTaskSpec spec = specs[task->taskIndex];

        UBaseType_t hw = 0;
#if defined(configUSE_TRACE_FACILITY) && (configUSE_TRACE_FACILITY == 1)
        const TaskStatus_t* liveTask = nullptr;
        for (UBaseType_t t = 0; t < liveTaskCount; ++t) {
            if (liveTasks[t].xHandle == taskHandle) {
                liveTask = &liveTasks[t];
                break;
            }
        }
        if (!liveTask) {
            if (moduleManager->removeTaskEntry(taskHandle)) {
                ++removedEndedTasks;
                --n;
                --i;
            } else {
                ++skippedInvalidHandles;
            }
            continue;
        }
        if (!isLikelyValidTaskHandle_(taskHandle)) {
            ++skippedInvalidHandles;
            continue;
        }
        hw = (UBaseType_t)liveTask->usStackHighWaterMark;
#else
        ++skippedInvalidHandles;
        continue;
#endif
        hasTask = true;
        const bool isMqttWorker = task->module->moduleId() == ModuleId::Mqtt;
        const bool isLow = isMqttWorker ? (hw < 1536U) : (hw < 300U);

        char entry[80];
        const int ew = isMqttWorker
            ? snprintf(entry, sizeof(entry), "%s/%s@c%ld size=%luB free_min=%uB%s",
                       toString(task->module->moduleId()),
                       spec.name ? spec.name : "?",
                       (long)spec.coreId,
                       (unsigned long)spec.stackSize,
                       (unsigned)hw,
                       isLow ? "!" : "")
            : snprintf(entry, sizeof(entry), "%s/%s@c%ld=%u%s",
                       toString(task->module->moduleId()),
                       spec.name ? spec.name : "?",
                       (long)spec.coreId,
                       (unsigned)hw,
                       isLow ? "!" : "");
        if (ew < 0) continue;

        const size_t entryLen = (size_t)ew;
        const size_t sepLen = (tasksOnLine > 0) ? 1U : 0U;

        if (tasksOnLine >= kMaxTasksPerLine || (off + sepLen + entryLen) > kLineMsgBudget) {
            if (tasksOnLine > 0) {
                LOGD("Stack %s", line);
            }
            off = 0;
            line[0] = '\0';
            tasksOnLine = 0;
        }

        if ((off + sepLen + entryLen) > kLineMsgBudget) {
            continue;
        }

        if (sepLen) {
            line[off++] = ' ';
            line[off] = '\0';
        }
        memcpy(line + off, entry, entryLen + 1);
        off += entryLen;
        ++tasksOnLine;

        if (tasksOnLine >= kMaxTasksPerLine) {
            LOGD("Stack %s", line);
            off = 0;
            line[0] = '\0';
            tasksOnLine = 0;
        }
    }

    if (!hasTask) {
        LOGD("Stack none");
        if (skippedInvalidHandles > 0U) {
            LOGW("Stack skipped invalid handles=%u", (unsigned)skippedInvalidHandles);
        }
        if (removedEndedTasks > 0U) {
            LOGD("Stack pruned ended tasks=%u", (unsigned)removedEndedTasks);
        }
        return;
    }

    if (tasksOnLine > 0) {
        LOGD("Stack %s", line);
    }
#if defined(configUSE_TRACE_FACILITY) && (configUSE_TRACE_FACILITY == 1)
    for (UBaseType_t t = 0; t < liveTaskCount; ++t) {
        const char* name = liveTasks[t].pcTaskName;
        if (!name) continue;
        if (strcmp(name, "async_tcp") != 0 &&
            strcmp(name, "mqtt_task") != 0 &&
            strcmp(name, "esp_mqtt_task") != 0) {
            continue;
        }
        const UBaseType_t hw = (UBaseType_t)liveTasks[t].usStackHighWaterMark;
        if (strcmp(name, "async_tcp") == 0) {
            const uint32_t configuredBytes = (uint32_t)CONFIG_ASYNC_TCP_STACK_SIZE;
            const uint32_t minFreeBytes = (uint32_t)hw;
            const uint32_t maxUsedBytes =
                (minFreeBytes < configuredBytes) ? (configuredBytes - minFreeBytes) : 0U;
            LOGD("Stack external/%s size=%luB used_max=%luB free_min=%luB%s",
                 name,
                 (unsigned long)configuredBytes,
                 (unsigned long)maxUsedBytes,
                 (unsigned long)minFreeBytes,
                 (minFreeBytes < 2048U) ? "!" : "");
        } else {
            LOGD("Stack external/%s free_min=%uB%s",
                 name,
                 (unsigned)hw,
                 (hw < 1024U) ? "!" : "");
        }
    }
#endif
    if (skippedInvalidHandles > 0U) {
        LOGW("Stack skipped invalid handles=%u", (unsigned)skippedInvalidHandles);
    }
    if (removedEndedTasks > 0U) {
        LOGD("Stack pruned ended tasks=%u", (unsigned)removedEndedTasks);
    }
}

void SystemMonitorModule::logTrackedBuffers()
{
    TrackedBufferSnapshot snapshots[(size_t)TrackedBufferId::Count]{};
    const size_t count = BufferUsageTracker::snapshot(snapshots, (size_t)TrackedBufferId::Count);
    static constexpr char kBufPrefix[] = "Buf ";
    static constexpr size_t kLineMsgBudget = (size_t)LOG_MSG_MAX - sizeof(kBufPrefix);
    char line[kLineMsgBudget + 1];
    size_t off = 0;
    uint8_t itemsOnLine = 0;
    bool loggedAny = false;
    line[0] = '\0';

    for (size_t i = 0; i < count; ++i) {
        const TrackedBufferSnapshot& s = snapshots[i];
        if (!s.name) continue;
        if (s.peakUsed == 0U && s.capacity == 0U) continue;

        char entry[96];
        const bool isFull = (s.capacity > 0U) && (s.peakUsed >= s.capacity);
        const int ew = s.source[0]
            ? snprintf(entry, sizeof(entry), "%s=%lu/%lu%s@%s",
                       s.name,
                       (unsigned long)s.peakUsed,
                       (unsigned long)s.capacity,
                       isFull ? "!" : "",
                       s.source)
            : snprintf(entry, sizeof(entry), "%s=%lu/%lu%s",
                       s.name,
                       (unsigned long)s.peakUsed,
                       (unsigned long)s.capacity,
                       isFull ? "!" : "");
        if (ew <= 0) continue;

        const size_t entryLen = (size_t)ew;
        const size_t sepLen = (itemsOnLine > 0U) ? 1U : 0U;
        if (itemsOnLine > 0U && (off + sepLen + entryLen) > kLineMsgBudget) {
            LOGD("Buf %s", line);
            line[0] = '\0';
            off = 0;
            itemsOnLine = 0;
        }
        if ((off + ((itemsOnLine > 0U) ? 1U : 0U) + entryLen) > kLineMsgBudget) {
            if (entryLen > kLineMsgBudget) continue;
        }
        if (itemsOnLine > 0U) {
            line[off++] = ' ';
            line[off] = '\0';
        }
        memcpy(line + off, entry, entryLen + 1U);
        off += entryLen;
        ++itemsOnLine;
        loggedAny = true;
    }

    if (!loggedAny) {
        LOGD("Buf none");
        return;
    }

    if (itemsOnLine > 0U) {
        LOGD("Buf %s", line);
    }
}

void SystemMonitorModule::appendHeapWatchSample_(const SystemStatsSnapshot& snap)
{
    if (!heapWatchSamples_) return;
    HeapWatchSample& sample = heapWatchSamples_[heapWatchWriteIndex_];
    sample.uptimeMs = snap.uptimeMs;
    sample.freeBytes = snap.heap.freeBytes;
    sample.minFreeBytes = snap.heap.minFreeBytes;
    sample.largestFreeBlock = snap.heap.largestFreeBlock;
    sample.internalFreeBytes = snap.heap.internalFreeBytes;
    sample.internalMinFreeBytes = snap.heap.internalMinFreeBytes;
    sample.internalLargestFreeBlock = snap.heap.internalLargestFreeBlock;

    heapWatchWriteIndex_ = (heapWatchWriteIndex_ + 1U) % kHeapWatchSampleCount;
    if (heapWatchCount_ < kHeapWatchSampleCount) {
        ++heapWatchCount_;
    }
}

void SystemMonitorModule::armHeapWatchDump_(const SystemStatsSnapshot& snap, const char* reason)
{
    heapWatchTripActive_ = true;
    heapWatchDumpPending_ = true;
    heapWatchTriggerMs_ = snap.uptimeMs;
    heapWatchTriggerFreeBytes_ = snap.heap.internalFreeBytes;
    heapWatchTriggerMinFreeBytes_ = snap.heap.internalMinFreeBytes;
    heapWatchTriggerLargestFreeBlock_ = snap.heap.internalLargestFreeBlock;
    heapWatchFrozenWriteIndex_ = heapWatchWriteIndex_;
    heapWatchFrozenCount_ = heapWatchCount_;
    snprintf(heapWatchTriggerReason_, sizeof(heapWatchTriggerReason_), "%s", reason ? reason : "-");
#ifdef CONFIG_HEAP_TASK_TRACKING
    captureHeapWatchTaskTotals_();
#endif
    LOGW("HeapWatch trip captured reason=%s free=%lu min=%lu largest=%lu",
         heapWatchTriggerReason_,
         (unsigned long)heapWatchTriggerFreeBytes_,
         (unsigned long)heapWatchTriggerMinFreeBytes_,
         (unsigned long)heapWatchTriggerLargestFreeBlock_);
}

void SystemMonitorModule::dumpHeapWatchWindow_() const
{
    if (!heapWatchSamples_ || heapWatchFrozenCount_ == 0U) {
        LOGW("HeapWatch window unavailable");
        return;
    }

    const size_t sampleCount =
        (heapWatchFrozenCount_ < kHeapWatchDumpSampleCount) ? heapWatchFrozenCount_ : kHeapWatchDumpSampleCount;
    const size_t startIndex =
        (heapWatchFrozenWriteIndex_ + kHeapWatchSampleCount - sampleCount) % kHeapWatchSampleCount;

    for (size_t i = 0; i < sampleCount; ++i) {
        const size_t idx = (startIndex + i) % kHeapWatchSampleCount;
        const HeapWatchSample& sample = heapWatchSamples_[idx];
        const long dtMs = (long)sample.uptimeMs - (long)heapWatchTriggerMs_;
        LOGW("HeapWatch win dt=%ld total_free=%lu total_min=%lu total_largest=%lu internal_free=%lu internal_min=%lu internal_largest=%lu",
             dtMs,
             (unsigned long)sample.freeBytes,
             (unsigned long)sample.minFreeBytes,
             (unsigned long)sample.largestFreeBlock,
             (unsigned long)sample.internalFreeBytes,
             (unsigned long)sample.internalMinFreeBytes,
             (unsigned long)sample.internalLargestFreeBlock);
    }
}

#ifdef CONFIG_HEAP_TASK_TRACKING
void SystemMonitorModule::captureHeapWatchTaskTotals_()
{
    heapWatchTaskTotalCount_ = 0U;

    heap_task_totals_t totals[kHeapWatchTaskTotalsMax]{};
    size_t numTotals = 0U;
    heap_task_info_params_t params{};
    params.caps[0] = 0;
    params.mask[0] = 0;
    params.totals = totals;
    params.num_totals = &numTotals;
    params.max_totals = kHeapWatchTaskTotalsMax;

    heap_caps_get_per_task_info(&params);

    const size_t totalCount = (numTotals < kHeapWatchTaskTotalsMax) ? numTotals : kHeapWatchTaskTotalsMax;
    for (size_t i = 0; i < totalCount; ++i) {
        heapWatchTaskTotals_[i].sizeBytes = (uint32_t)totals[i].size[0];
        heapWatchTaskTotals_[i].blockCount = (uint32_t)totals[i].count[0];
        copyTaskName_(heapWatchTaskTotals_[i].taskName, sizeof(heapWatchTaskTotals_[i].taskName), totals[i].task);
    }
    heapWatchTaskTotalCount_ = totalCount;
    for (size_t i = 0; i < heapWatchTaskTotalCount_; ++i) {
        size_t best = i;
        for (size_t j = i + 1; j < heapWatchTaskTotalCount_; ++j) {
            if (heapWatchTaskTotals_[j].sizeBytes > heapWatchTaskTotals_[best].sizeBytes) {
                best = j;
            }
        }
        if (best == i) continue;
        HeapWatchTaskTotal tmp = heapWatchTaskTotals_[i];
        heapWatchTaskTotals_[i] = heapWatchTaskTotals_[best];
        heapWatchTaskTotals_[best] = tmp;
    }
}

void SystemMonitorModule::dumpHeapWatchTaskTotals_() const
{
    if (heapWatchTaskTotalCount_ == 0U) {
        LOGW("HeapWatch tasks unavailable");
        return;
    }

    for (size_t i = 0; i < heapWatchTaskTotalCount_; ++i) {
        const HeapWatchTaskTotal& total = heapWatchTaskTotals_[i];
        if (total.sizeBytes == 0U && total.blockCount == 0U) continue;
        LOGW("HeapWatch task=%s alloc=%lu blocks=%lu",
             total.taskName[0] ? total.taskName : "-",
             (unsigned long)total.sizeBytes,
             (unsigned long)total.blockCount);
    }
}
#endif

void SystemMonitorModule::dumpHeapWatch_()
{
    LOGW("HeapWatch dump reason=%s trigger_free=%lu trigger_min=%lu trigger_largest=%lu captured=%u dump_last=%u sample_ms=%lu",
         heapWatchTriggerReason_[0] ? heapWatchTriggerReason_ : "-",
         (unsigned long)heapWatchTriggerFreeBytes_,
         (unsigned long)heapWatchTriggerMinFreeBytes_,
         (unsigned long)heapWatchTriggerLargestFreeBlock_,
         (unsigned)heapWatchFrozenCount_,
         (unsigned)kHeapWatchDumpSampleCount,
         (unsigned long)kHeapWatchSamplePeriodMs);
    dumpHeapWatchWindow_();
#ifdef CONFIG_HEAP_TASK_TRACKING
    dumpHeapWatchTaskTotals_();
#endif
    heapWatchDumpPending_ = false;
}

void SystemMonitorModule::pollHeapWatch_(uint32_t now)
{
    if (lastHeapWatchSampleMs_ != 0U && (uint32_t)(now - lastHeapWatchSampleMs_) < kHeapWatchSamplePeriodMs) {
        return;
    }
    lastHeapWatchSampleMs_ = now;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    appendHeapWatchSample_(snap);

    if (heapWatchLastSeenMinFree_ == UINT32_MAX) {
        heapWatchLastSeenMinFree_ = snap.heap.internalMinFreeBytes;
    }

    const bool currentFreeTrip = snap.heap.internalFreeBytes <= kHeapWatchTripFreeBytes;
    const bool minLowWaterTrip =
        snap.heap.internalMinFreeBytes <= kHeapWatchTripFreeBytes &&
        snap.heap.internalMinFreeBytes < heapWatchLastSeenMinFree_;

    if (!heapWatchTripActive_ && !heapWatchDumpPending_) {
        if (currentFreeTrip) {
            armHeapWatchDump_(snap, "free");
        } else if (minLowWaterTrip) {
            armHeapWatchDump_(snap, "min_low");
        }
    }

    if (snap.heap.internalMinFreeBytes < heapWatchLastSeenMinFree_) {
        heapWatchLastSeenMinFree_ = snap.heap.internalMinFreeBytes;
    }

    if (heapWatchDumpPending_) {
        const bool recovered = snap.heap.internalFreeBytes >= kHeapWatchRecoverFreeBytes;
        const bool timeout = (uint32_t)(snap.uptimeMs - heapWatchTriggerMs_) >= kHeapWatchDumpDelayMs;
        if (recovered || timeout) {
            dumpHeapWatch_();
        }
    }

    if (heapWatchTripActive_ && snap.heap.internalFreeBytes >= kHeapWatchRecoverFreeBytes) {
        LOGI("HeapWatch recovered internal_free=%lu internal_min=%lu internal_largest=%lu",
             (unsigned long)snap.heap.internalFreeBytes,
             (unsigned long)snap.heap.internalMinFreeBytes,
             (unsigned long)snap.heap.internalLargestFreeBlock);
        heapWatchTripActive_ = false;
    }
}

void SystemMonitorModule::logPendingHeapAllocFailure_()
{
    if (!gHeapAllocFailedPending) return;

    const size_t size = gHeapAllocFailedSize;
    const uint32_t caps = gHeapAllocFailedCaps;
    const char* functionName = gHeapAllocFailedFunction;
    gHeapAllocFailedPending = false;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    LOGW("Heap alloc failed size=%lu caps=0x%08lx func=%s total_free=%lu total_min=%lu total_largest=%lu internal_free=%lu internal_min=%lu internal_largest=%lu",
         (unsigned long)size,
         (unsigned long)caps,
         functionName ? functionName : "-",
         (unsigned long)snap.heap.freeBytes,
         (unsigned long)snap.heap.minFreeBytes,
         (unsigned long)snap.heap.largestFreeBlock,
         (unsigned long)snap.heap.internalFreeBytes,
         (unsigned long)snap.heap.internalMinFreeBytes,
         (unsigned long)snap.heap.internalLargestFreeBlock);
}

void SystemMonitorModule::pollMemoryPressureReboot_(uint32_t now)
{
    const uint32_t bootGraceMs =
        (cfgData_.webWatchdogBootGraceMs > (int32_t)kWebWatchdogMinBootGraceMs)
            ? (uint32_t)cfgData_.webWatchdogBootGraceMs
            : kWebWatchdogMinBootGraceMs;
    if (now < bootGraceMs) {
        memoryPressureState_ = (uint8_t)MemoryPressureState::Normal;
        memoryPressureStateSinceMs_ = now;
        memoryPressureRebootIssued_ = false;
        return;
    }

    if (!fwUpdateSvc_ && services_) {
        fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    }
    const bool firmwareUpdateBusy =
        fwUpdateSvc_ && fwUpdateSvc_->isBusy && fwUpdateSvc_->isBusy(fwUpdateSvc_->ctx);

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    const MemoryPressureState state = applyMemoryPressureHysteresis_(
        snap, (MemoryPressureState)memoryPressureState_);
    const uint8_t stateRaw = (uint8_t)state;

    if (stateRaw != memoryPressureState_) {
        const MemoryPressureState prevState = (MemoryPressureState)memoryPressureState_;
        const bool prevCriticalOrWorse =
            (prevState == MemoryPressureState::Critical) || (prevState == MemoryPressureState::Panic);
        const bool nowCriticalOrWorse =
            (state == MemoryPressureState::Critical) || (state == MemoryPressureState::Panic);
        const bool suppressTransitionLog = firmwareUpdateBusy && !prevCriticalOrWorse && !nowCriticalOrWorse;

        memoryPressureState_ = stateRaw;
        memoryPressureStateSinceMs_ = now;
        memoryPressureRebootIssued_ = false;
        if (!suppressTransitionLog) {
            LOGW("Memory pressure -> %s internal_free=%lu internal_largest=%lu internal_frag=%u%%%s",
                 memoryPressureStateStr_(state),
                 (unsigned long)snap.heap.internalFreeBytes,
                 (unsigned long)snap.heap.internalLargestFreeBlock,
                 (unsigned int)snap.heap.internalFragPercent,
                 firmwareUpdateBusy ? " fwupdate=busy" : "");
        }
        return;
    }

    if (memoryPressureRebootIssued_) return;
    if (!cfgData_.webWatchdogAutoReboot) return;

    const uint32_t sinceMs = memoryPressureStateSinceMs_;
    const uint32_t heldMs = (sinceMs > 0U) ? (uint32_t)(now - sinceMs) : 0U;

    bool shouldReboot = false;
    if (state == MemoryPressureState::Panic && heldMs >= kPressurePanicRebootDelayMs) {
        shouldReboot = true;
    } else if (state == MemoryPressureState::Critical && heldMs >= kPressureCriticalRebootDelayMs) {
        shouldReboot = true;
    }
    if (!shouldReboot) return;

    memoryPressureRebootIssued_ = true;
    LOGE("Memory pressure persisted (%s %lums), reboot requested internal_free=%lu internal_largest=%lu internal_frag=%u%%",
         memoryPressureStateStr_(state),
         (unsigned long)heldMs,
         (unsigned long)snap.heap.internalFreeBytes,
         (unsigned long)snap.heap.internalLargestFreeBlock,
         (unsigned int)snap.heap.internalFragPercent);
    if (!cmdSvc_ && services_) {
        cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
    }
    if (cmdSvc_ && cmdSvc_->execute) {
        char reply[160] = {0};
        (void)cmdSvc_->execute(cmdSvc_->ctx, "system.reboot", "{}", nullptr, reply, sizeof(reply));
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}

void SystemMonitorModule::pollWebWatchdog_(uint32_t now)
{
    if (!cfgData_.webWatchdogEnabled) {
        webWatchdogConsecutiveFailures_ = 0U;
        webWatchdogRebootIssued_ = false;
        return;
    }

    const uint32_t checkPeriodMs =
        (cfgData_.webWatchdogCheckPeriodMs > (int32_t)kWebWatchdogMinCheckPeriodMs)
            ? (uint32_t)cfgData_.webWatchdogCheckPeriodMs
            : kWebWatchdogMinCheckPeriodMs;
    if (lastWebWatchdogCheckMs_ != 0U && (uint32_t)(now - lastWebWatchdogCheckMs_) < checkPeriodMs) {
        return;
    }
    lastWebWatchdogCheckMs_ = now;

    const uint32_t bootGraceMs =
        (cfgData_.webWatchdogBootGraceMs > (int32_t)kWebWatchdogMinBootGraceMs)
            ? (uint32_t)cfgData_.webWatchdogBootGraceMs
            : kWebWatchdogMinBootGraceMs;
    if (now < bootGraceMs) {
        webWatchdogConsecutiveFailures_ = 0U;
        webWatchdogRebootIssued_ = false;
        return;
    }

    if (!services_) return;
    if (!netAccessSvc_) {
        netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
    }
    if (!webInterfaceSvc_) {
        webInterfaceSvc_ = services_->get<WebInterfaceService>(ServiceId::WebInterface);
    }
    if (!cmdSvc_) {
        cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
    }
    if (!webInterfaceSvc_ || !webInterfaceSvc_->getHealth) return;

    NetworkAccessMode mode = NetworkAccessMode::None;
    bool webReachable = false;
    if (netAccessSvc_) {
        if (netAccessSvc_->mode) mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        if (netAccessSvc_->isWebReachable) {
            webReachable = netAccessSvc_->isWebReachable(netAccessSvc_->ctx);
        } else {
            webReachable = (mode != NetworkAccessMode::None);
        }
    }
    if (!webReachable || mode == NetworkAccessMode::None) {
        webWatchdogConsecutiveFailures_ = 0U;
        webWatchdogRebootIssued_ = false;
        return;
    }

    WebInterfaceHealth health{};
    if (!webInterfaceSvc_->getHealth(webInterfaceSvc_->ctx, &health)) return;

    if (!health.started || health.paused) {
        webWatchdogConsecutiveFailures_ = 0U;
        webWatchdogRebootIssued_ = false;
        return;
    }

    const uint32_t staleMs =
        (cfgData_.webWatchdogStaleMs > (int32_t)kWebWatchdogMinStaleMs)
            ? (uint32_t)cfgData_.webWatchdogStaleMs
            : kWebWatchdogMinStaleMs;
    const uint32_t loopAgeMs = (health.lastLoopMs > 0U) ? (uint32_t)(now - health.lastLoopMs) : UINT32_MAX;
    const bool loopStale = (health.lastLoopMs == 0U) || (loopAgeMs > staleMs);

    const uint16_t activeClients = (uint16_t)(health.wsSerialClients + health.wsLogClients);
    uint32_t lastClientActivityMs = health.lastWsActivityMs;
    if (health.lastHttpActivityMs > lastClientActivityMs) {
        lastClientActivityMs = health.lastHttpActivityMs;
    }
    const uint32_t clientIdleMs =
        (activeClients == 0U)
            ? 0U
            : ((lastClientActivityMs > 0U) ? (uint32_t)(now - lastClientActivityMs) : UINT32_MAX);
    const bool clientsStale =
        (activeClients > 0U) && ((lastClientActivityMs == 0U) || (clientIdleMs > (staleMs * kWebWatchdogClientIdleFactor)));

    // A connected but idle WS/HTTP client is normal (dashboard open, no user action).
    // Reboot escalation must only happen when the web loop itself stops progressing.
    if (!loopStale) {
        if (webWatchdogConsecutiveFailures_ > 0U) {
            LOGI("Web watchdog recovered failures=%u loop_age=%lu clients=%u",
                 (unsigned)webWatchdogConsecutiveFailures_,
                 (unsigned long)loopAgeMs,
                 (unsigned)activeClients);
        }
        if (clientsStale) {
            LOGD("Web watchdog client idle (non-fatal) loop_age=%lu clients=%u client_idle=%lu ws_ms=%lu http_ms=%lu",
                 (unsigned long)loopAgeMs,
                 (unsigned)activeClients,
                 (unsigned long)clientIdleMs,
                 (unsigned long)health.lastWsActivityMs,
                 (unsigned long)health.lastHttpActivityMs);
        }
        webWatchdogConsecutiveFailures_ = 0U;
        webWatchdogRebootIssued_ = false;
        return;
    }

    if (webWatchdogConsecutiveFailures_ < 0xFFU) {
        ++webWatchdogConsecutiveFailures_;
    }

    const uint8_t maxFailuresCfg =
        (cfgData_.webWatchdogMaxFailures > 0) ? (uint8_t)cfgData_.webWatchdogMaxFailures : 1U;
    const uint8_t maxFailures =
        (maxFailuresCfg < kWebWatchdogMaxFailuresCap) ? maxFailuresCfg : kWebWatchdogMaxFailuresCap;

    LOGW("Web watchdog fail=%u/%u loop_stale=%d loop_age=%lu stale=%lu clients=%u client_idle=%lu ws_ms=%lu http_ms=%lu",
         (unsigned)webWatchdogConsecutiveFailures_,
         (unsigned)maxFailures,
         (int)loopStale,
         (unsigned long)loopAgeMs,
         (unsigned long)staleMs,
         (unsigned)activeClients,
         (unsigned long)clientIdleMs,
         (unsigned long)health.lastWsActivityMs,
         (unsigned long)health.lastHttpActivityMs);

    if (webWatchdogConsecutiveFailures_ < maxFailures) return;
    if (!cfgData_.webWatchdogAutoReboot) {
        LOGE("Web watchdog reached threshold but auto-reboot is disabled");
        return;
    }
    if (webWatchdogRebootIssued_) return;
    webWatchdogRebootIssued_ = true;

    LOGE("Web watchdog threshold reached, reboot requested");
    if (cmdSvc_ && cmdSvc_->execute) {
        char reply[160] = {0};
        (void)cmdSvc_->execute(cmdSvc_->ctx, "system.reboot", "{}", nullptr, reply, sizeof(reply));
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}

void SystemMonitorModule::buildHealthJson(char* out, size_t outLen) {
    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);

    /// wifi
    WifiState wst = WifiState::Disabled;
    bool wcon = false;
    char ip[16] = "";
    int rssi = -127;

    if (wifiSvc) {
        wst = wifiSvc->state(wifiSvc->ctx);
        wcon = wifiSvc->isConnected(wifiSvc->ctx);
        wifiSvc->getIP(wifiSvc->ctx, ip, sizeof(ip));
        if (wcon) rssi = WiFi.RSSI();
    }

    snprintf(out, outLen,
        "{"
            "\"upt_ms\":%llu,"
            "\"heap\":{"
                "\"free\":%lu,"
                "\"min_free\":%lu,"
                "\"largest\":%lu,"
                "\"frag\":%u,"
                "\"internal_free\":%lu,"
                "\"internal_min_free\":%lu,"
                "\"internal_largest\":%lu,"
                "\"internal_frag\":%u"
            "}"
        "}",
        (unsigned long long)snap.uptimeMs64,
        (unsigned long)snap.heap.freeBytes,
        (unsigned long)snap.heap.minFreeBytes,
        (unsigned long)snap.heap.largestFreeBlock,
        (unsigned int)snap.heap.fragPercent,
        (unsigned long)snap.heap.internalFreeBytes,
        (unsigned long)snap.heap.internalMinFreeBytes,
        (unsigned long)snap.heap.internalLargestFreeBlock,
        (unsigned int)snap.heap.internalFragPercent
    );
}

void SystemMonitorModule::loop() {
    if (!bootInfoLogged_) {
        bootInfoLogged_ = true;
        logBootInfo();
    }

    const uint32_t now = millis();
#if FLOW_WEB_HEAP_FORENSICS
    pollHeapWatch_(now);
    logPendingHeapAllocFailure_();
#endif
    pollMemoryPressureReboot_(now);
    if (cfgStore_) {
        cfgStore_->logNvsWriteSummaryIfDue(now, 60000U);
    }
    pollWebWatchdog_(now);

    const uint32_t periodMs = (cfgData_.tracePeriodMs > 0) ? (uint32_t)cfgData_.tracePeriodMs : 5000U;
    const uint32_t heapOffsetMs = periodMs / 3U;
    const uint32_t bufOffsetMs = (periodMs * 2U) / 3U;

    if (traceCycleStartMs_ == 0U) {
        traceCycleStartMs_ = now;
        stackLoggedThisCycle_ = false;
        heapLoggedThisCycle_ = false;
        buffersLoggedThisCycle_ = false;
    }

    while ((uint32_t)(now - traceCycleStartMs_) >= periodMs) {
        traceCycleStartMs_ += periodMs;
        stackLoggedThisCycle_ = false;
        heapLoggedThisCycle_ = false;
        buffersLoggedThisCycle_ = false;
    }

    const uint32_t cycleElapsedMs = (uint32_t)(now - traceCycleStartMs_);
    if (!stackLoggedThisCycle_) {
        logTaskStacks();
        stackLoggedThisCycle_ = true;
    }
    if (!heapLoggedThisCycle_ && cycleElapsedMs >= heapOffsetMs) {
        logHeapStats();
        heapLoggedThisCycle_ = true;
    }
    if (!buffersLoggedThisCycle_ && cycleElapsedMs >= bufOffsetMs) {
        logTrackedBuffers();
        buffersLoggedThisCycle_ = true;
    }

    vTaskDelay(pdMS_TO_TICKS(25));
}
