/**
 * @file PoolDeviceLifecycle.cpp
 * @brief Lifecycle entry points and integration wiring for PoolDeviceModule.
 */

#include "PoolDeviceModule.h"
#include "Core/BufferUsageTracker.h"
#include "Core/MqttTopics.h"
#include "Domain/Pool/PoolIds.h"
#include "Domain/Pool/PoolDeviceSlots.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolDeviceModule)
#include "Core/ModuleLog.h"
#include <esp_heap_caps.h>
#include <new>

namespace {
static constexpr uint8_t kPoolDeviceCfgProducerId = 48;
static constexpr const char* kPoolDeviceCfgTopicBase = "cfg/pdm";
static constexpr uint8_t kPoolDeviceCfgBranchBase = 1;
static constexpr uint8_t kTimeCfgBranch = 1;
static constexpr uint16_t kCfgMsgBasePdm = 1;
static constexpr uint16_t kCfgMsgPdmSlotBase = 16;

template <typename T>
T* allocPsramArray_(size_t count)
{
    void* mem = heap_caps_malloc(sizeof(T) * count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(T) * count, MALLOC_CAP_8BIT);
    if (!mem) return nullptr;

    T* out = static_cast<T*>(mem);
    for (size_t i = 0; i < count; ++i) {
        new (&out[i]) T();
    }
    return out;
}

template <size_t Cols>
char (*allocPsramCharTable_(size_t rows))[Cols]
{
    void* mem = heap_caps_calloc(rows, Cols, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_calloc(rows, Cols, MALLOC_CAP_8BIT);
    return static_cast<char (*)[Cols]>(mem);
}

static constexpr uint8_t poolDeviceCfgBranchFromSlot_(uint8_t slot)
{
    return (slot < POOL_DEVICE_MAX) ? (uint8_t)(kPoolDeviceCfgBranchBase + slot) : 0;
}

} // namespace

bool PoolDeviceModule::ensureStorage_()
{
    if (!stateMutex_) {
        stateMutex_ = xSemaphoreCreateRecursiveMutexStatic(&stateMutexBuf_);
    }

    if (runtimePersistBuf_ && slots_ && cfgEnabledVar_ && cfgDependsVar_ && cfgFlowVar_ &&
        cfgTankCapVar_ && cfgTankInitVar_ && cfgMaxUptimeVar_) {
        return stateMutex_ != nullptr;
    }

    if (!runtimePersistBuf_) {
        runtimePersistBuf_ = allocPsramCharTable_<RUNTIME_PERSIST_BUF_LEN>(POOL_DEVICE_MAX);
    }
    if (!slots_) slots_ = allocPsramArray_<PoolDeviceSlot>(POOL_DEVICE_MAX);
    if (!cfgEnabledVar_) cfgEnabledVar_ = allocPsramArray_<ConfigVariable<bool,0>>(POOL_DEVICE_MAX);
    if (!cfgDependsVar_) cfgDependsVar_ = allocPsramArray_<ConfigVariable<uint8_t,0>>(POOL_DEVICE_MAX);
    if (!cfgFlowVar_) cfgFlowVar_ = allocPsramArray_<ConfigVariable<float,0>>(POOL_DEVICE_MAX);
    if (!cfgTankCapVar_) cfgTankCapVar_ = allocPsramArray_<ConfigVariable<float,0>>(POOL_DEVICE_MAX);
    if (!cfgTankInitVar_) cfgTankInitVar_ = allocPsramArray_<ConfigVariable<float,0>>(POOL_DEVICE_MAX);
    if (!cfgMaxUptimeVar_) cfgMaxUptimeVar_ = allocPsramArray_<ConfigVariable<int32_t,0>>(POOL_DEVICE_MAX);

    const bool ok = stateMutex_ && runtimePersistBuf_ && slots_ && cfgEnabledVar_ && cfgDependsVar_ && cfgFlowVar_ &&
                    cfgTankCapVar_ && cfgTankInitVar_ && cfgMaxUptimeVar_;
    if (ok) {
        LOGI("PoolDevice scalable storage ready slots=%u persist_bytes=%u",
             (unsigned)POOL_DEVICE_MAX,
             (unsigned)runtimePersistCapacity_());
    } else {
        LOGE("PoolDevice scalable storage allocation failed");
    }
    return ok;
}

bool PoolDeviceModule::lockState_(TickType_t timeoutTicks) const
{
    if (!stateMutex_) return false;
    return xSemaphoreTakeRecursive(stateMutex_, timeoutTicks) == pdTRUE;
}

void PoolDeviceModule::unlockState_() const
{
    if (stateMutex_) {
        (void)xSemaphoreGiveRecursive(stateMutex_);
    }
}

size_t PoolDeviceModule::runtimePersistUsage_() const
{
    if (!runtimePersistBuf_) return 0U;
    size_t total = 0U;
    for (uint8_t row = 0; row < POOL_DEVICE_MAX; ++row) {
        const size_t len = strnlen(runtimePersistBuf_[row], RUNTIME_PERSIST_BUF_LEN);
        if (len > 0U) total += len + 1U;
    }
    return total;
}

void PoolDeviceModule::onEventStatic_(const Event& e, void* user)
{
    if (!user) return;
    static_cast<PoolDeviceModule*>(user)->onEvent_(e);
}

void PoolDeviceModule::onEvent_(const Event& e)
{
    if (e.id == EventId::DataChanged) {
        if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
        const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
        if (p->id != DATAKEY_TIME_READY) return;
        if (!dataStore_) return;
        if (timeReady(*dataStore_)) requestPeriodReconcile_();
        return;
    }

    if (e.id == EventId::ConfigChanged) {
        if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (p->moduleId == (uint8_t)ConfigModuleId::Time &&
            p->localBranchId == kTimeCfgBranch) {
            requestPeriodReconcile_();
        }
        return;
    }

    if (e.id != EventId::SchedulerEventTriggered) return;
    if (!e.payload || e.len < sizeof(SchedulerEventTriggeredPayload)) return;

    const SchedulerEventTriggeredPayload* p = (const SchedulerEventTriggeredPayload*)e.payload;
    if ((SchedulerEdge)p->edge != SchedulerEdge::Trigger) return;

    uint8_t pending = 0;
    if (p->eventId == TIME_EVENT_SYS_DAY_START) {
        pending = RESET_PENDING_DAY;
    } else if (p->eventId == TIME_EVENT_SYS_WEEK_START) {
        pending = RESET_PENDING_WEEK;
    } else if (p->eventId == TIME_EVENT_SYS_MONTH_START) {
        pending = RESET_PENDING_MONTH;
    } else {
        return;
    }

    portENTER_CRITICAL(&resetMux_);
    resetPendingMask_ |= pending;
    portEXIT_CRITICAL(&resetMux_);
}

void PoolDeviceModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::PoolDevice;
    if (!ensureStorage_()) return;

    cfgStore_ = &cfg;
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    timeSvc_ = services.get<TimeService>(ServiceId::Time);
    if (!services.add(ServiceId::PoolDevice, &poolSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::PoolDevice));
    }
    domainStatusProvider_.bindServices(ioSvc_, &poolSvc_);
    if (!services.add(ServiceId::DomainStatus, &domainStatusProvider_.service())) {
        LOGE("service registration failed: %s", toString(ServiceId::DomainStatus));
    }
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    haSvc_ = services.get<HAService>(ServiceId::Ha);
    activityLogSvc_ = services.get<ActivityLogService>(ServiceId::ActivityLog);
    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    if (!ioSvc_) {
        LOGW("PoolDevice waiting for IOServiceV2");
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::SchedulerEventTriggered, &PoolDeviceModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::DataChanged, &PoolDeviceModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::ConfigChanged, &PoolDeviceModule::onEventStatic_, this);
    }

    // Each slot owns one config branch; runtime metrics use ConfigStore runtime blob APIs.
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        const uint8_t localBranchId = poolDeviceCfgBranchFromSlot_(i);

        const PoolDeviceSlotDescriptor& slot = PoolDeviceSlots::kSlots[i];

        cfgEnabledVar_[i].nvsKey = slot.enabledKey;
        cfgEnabledVar_[i].jsonName = "enabled";
        cfgEnabledVar_[i].moduleName = slot.configModuleName;
        cfgEnabledVar_[i].type = ConfigType::Bool;
        cfgEnabledVar_[i].value = &s.def.enabled;
        cfgEnabledVar_[i].persistence = ConfigPersistence::Persistent;
        cfgEnabledVar_[i].size = 0;
        cfg.registerVar(cfgEnabledVar_[i], kCfgModuleId, localBranchId);

        cfgDependsVar_[i].nvsKey = slot.dependsKey;
        cfgDependsVar_[i].jsonName = "depends_on_mask";
        cfgDependsVar_[i].moduleName = slot.configModuleName;
        cfgDependsVar_[i].type = ConfigType::UInt8;
        cfgDependsVar_[i].value = &s.def.dependsOnMask;
        cfgDependsVar_[i].persistence = ConfigPersistence::Persistent;
        cfgDependsVar_[i].size = 0;
        cfg.registerVar(cfgDependsVar_[i], kCfgModuleId, localBranchId);

        cfgFlowVar_[i].nvsKey = slot.flowKey;
        cfgFlowVar_[i].jsonName = "flow_l_h";
        cfgFlowVar_[i].moduleName = slot.configModuleName;
        cfgFlowVar_[i].type = ConfigType::Float;
        cfgFlowVar_[i].value = &s.def.flowLPerHour;
        cfgFlowVar_[i].persistence = ConfigPersistence::Persistent;
        cfgFlowVar_[i].size = 0;
        cfg.registerVar(cfgFlowVar_[i], kCfgModuleId, localBranchId);

        cfgTankCapVar_[i].nvsKey = slot.tankCapKey;
        cfgTankCapVar_[i].jsonName = "tank_cap_ml";
        cfgTankCapVar_[i].moduleName = slot.configModuleName;
        cfgTankCapVar_[i].type = ConfigType::Float;
        cfgTankCapVar_[i].value = &s.def.tankCapacityMl;
        cfgTankCapVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankCapVar_[i].size = 0;
        cfg.registerVar(cfgTankCapVar_[i], kCfgModuleId, localBranchId);

        cfgTankInitVar_[i].nvsKey = slot.tankInitKey;
        cfgTankInitVar_[i].jsonName = "tank_init_ml";
        cfgTankInitVar_[i].moduleName = slot.configModuleName;
        cfgTankInitVar_[i].type = ConfigType::Float;
        cfgTankInitVar_[i].value = &s.def.tankInitialMl;
        cfgTankInitVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankInitVar_[i].size = 0;
        cfg.registerVar(cfgTankInitVar_[i], kCfgModuleId, localBranchId);

        cfgMaxUptimeVar_[i].nvsKey = slot.maxUptimeKey;
        cfgMaxUptimeVar_[i].jsonName = "max_uptime_day_s";
        cfgMaxUptimeVar_[i].moduleName = slot.configModuleName;
        cfgMaxUptimeVar_[i].type = ConfigType::Int32;
        cfgMaxUptimeVar_[i].value = &s.def.maxUptimeDaySec;
        cfgMaxUptimeVar_[i].persistence = ConfigPersistence::Persistent;
        cfgMaxUptimeVar_[i].size = 0;
        cfg.registerVar(cfgMaxUptimeVar_[i], kCfgModuleId, localBranchId);
    }

    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pooldevice.write", cmdPoolWrite_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pool.refill", cmdPoolRefill_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pooldevice.uptime.reset", cmdPoolResetUptime_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pooldevice.uptime.reset_all", cmdPoolResetUptimeAll_, this);
    }
    if (haSvc_ && haSvc_->addSensor) {
        if (slots_[PoolIds::DeviceChlorinePump].used) {
            const HASensorEntry s0{
                "pooldev", "pd_chl_pmp_upt", "Pump uptime Chlorine",
                "rt/pdm/metrics/pd2", "{{ value_json.running.day_s | int(0) }}",
                nullptr, "mdi:timer-outline", "s"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s0);
            const HASensorEntry s0b{
                "pooldev", "pd_chl_tnk_rem", "Tank remaining Chlorine",
                "rt/pdm/metrics/pd2", "{{ ((value_json.tank.remaining_ml | float(0)) / 1000) | round(2) }}",
                nullptr, "mdi:water-check", "L"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s0b);
        }
        if (slots_[PoolIds::DevicePhPump].used) {
            const HASensorEntry s1{
                "pooldev", "pd_ph_pmp_upt", "Pump uptime pH",
                "rt/pdm/metrics/pd1", "{{ value_json.running.day_s | int(0) }}",
                nullptr, "mdi:timer-outline", "s"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s1);
            const HASensorEntry s1b{
                "pooldev", "pd_ph_tnk_rem", "Tank remaining pH",
                "rt/pdm/metrics/pd1", "{{ ((value_json.tank.remaining_ml | float(0)) / 1000) | round(2) }}",
                nullptr, "mdi:beaker-check-outline", "L", false
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s1b);
        }
        if (slots_[PoolIds::DeviceFillPump].used) {
            const HASensorEntry s2{
                "pooldev", "pd_fill_upt_mn", "Pump uptime Fill",
                "rt/pdm/metrics/pd4", "{{ ((value_json.running.day_s | float(0)) / 60) | round(0) | int(0) }}",
                nullptr, "mdi:timer-outline", "mn"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s2);
        }
        if (slots_[PoolIds::DeviceFiltrationPump].used) {
            const HASensorEntry s3{
                "pooldev", "pd_flt_upt_mn", "Pump uptime Filtration",
                "rt/pdm/metrics/pd0", "{{ ((value_json.running.day_s | float(0)) / 60) | round(0) | int(0) }}",
                nullptr, "mdi:timer-outline", "mn"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s3);
        }
        if (slots_[PoolIds::DeviceChlorineGenerator].used) {
            const HASensorEntry s4{
                "pooldev", "pd_chl_gen_upt", "Pump uptime Chlorine Generator",
                "rt/pdm/metrics/pd5", "{{ ((value_json.running.day_s | float(0)) / 60) | round(0) | int(0) }}",
                nullptr, "mdi:timer-outline", "mn"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s4);
        }
    }

    if (haSvc_ && haSvc_->addNumber) {
        if (slots_[0].used) {
            const HANumberEntry n0{
                "pooldev", "pd0_flow", "Filtration Pump Flowrate",
                "cfg/pdm/pd0", "{{ value_json.flow_l_h }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd0\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n0);
            const HANumberEntry n0b{
                "pooldev", "pd0_max_upt", "Max Uptime Filtration Pump",
                "cfg/pdm/pd0", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd0\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                0.0f, 1440.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n0b);
        }
        if (slots_[1].used) {
            const HANumberEntry n1{
                "pooldev", "pd1_flow", "pH Pump Flowrate",
                "cfg/pdm/pd1", "{{ value_json.flow_l_h }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd1\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n1);
        }
        if (slots_[2].used) {
            const HANumberEntry n2{
                "pooldev", "pd2_flow", "Chlorine Pump Flowrate",
                "cfg/pdm/pd2", "{{ value_json.flow_l_h }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd2\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n2);
        }
        if (slots_[PoolIds::DevicePhPump].used) {
            const HANumberEntry n3{
                "pooldev", "pd1_max_upt", "Max Uptime pH Pump",
                "cfg/pdm/pd1", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd1\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                1.0f, 120.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n3);
        }
        if (slots_[PoolIds::DeviceChlorinePump].used) {
            const HANumberEntry n4{
                "pooldev", "pd2_max_upt", "Max Uptime Chlorine Pump",
                "cfg/pdm/pd2", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd2\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                1.0f, 120.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n4);
        }
        if (slots_[PoolIds::DeviceFillPump].used) {
            const HANumberEntry n4b{
                "pooldev", "pd4_max_upt", "Max Uptime Fill Pump",
                "cfg/pdm/pd4", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd4\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                0.0f, 120.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n4b);
        }
        if (slots_[PoolIds::DeviceChlorineGenerator].used) {
            const HANumberEntry n5{
                "pooldev", "pd5_max_upt", "Max Uptime Chlorine Generator",
                "cfg/pdm/pd5", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd5\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                0.0f, 1440.0f, 1.0f, "box", "config", "mdi:timer-cog-outline", "mn"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n5);
        }
    }
    if (haSvc_ && haSvc_->addButton) {
        if (slots_[PoolIds::DevicePhPump].used) {
            const HAButtonEntry refillPhTank{
                "pooldev",
                "pd_refill_ph",
                "Fill pH Tank",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.refill\\\",\\\"args\\\":{\\\"slot\\\":1}}",
                "config",
                "mdi:beaker-plus-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &refillPhTank);
        }
        if (slots_[PoolIds::DeviceChlorinePump].used) {
            const HAButtonEntry refillChlorineTank{
                "pooldev",
                "pd_refill_chl",
                "Fill Chlorine Tank",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.refill\\\",\\\"args\\\":{\\\"slot\\\":2}}",
                "config",
                "mdi:water-plus"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &refillChlorineTank);
        }
        if (slots_[PoolIds::DeviceFiltrationPump].used) {
            const HAButtonEntry resetFiltrationUptime{
                "pooldev",
                "pd_reset_upt_flt",
                "Reset Uptime Filtration Pump",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":0}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetFiltrationUptime);
        }
        if (slots_[PoolIds::DevicePhPump].used) {
            const HAButtonEntry resetPhUptime{
                "pooldev",
                "pd_reset_upt_ph",
                "Reset Uptime pH Pump",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":1}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetPhUptime);
        }
        if (slots_[PoolIds::DeviceChlorinePump].used) {
            const HAButtonEntry resetChlorineUptime{
                "pooldev",
                "pd_reset_upt_chl",
                "Reset Uptime Chlorine Pump",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":2}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetChlorineUptime);
        }
        if (slots_[PoolIds::DeviceFillPump].used) {
            const HAButtonEntry resetFillUptime{
                "pooldev",
                "pd_reset_upt_fill",
                "Reset Uptime Fill Pump",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":4}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetFillUptime);
        }
        if (slots_[PoolIds::DeviceChlorineGenerator].used) {
            const HAButtonEntry resetGeneratorUptime{
                "pooldev",
                "pd_reset_upt_chl_gen",
                "Reset Uptime Chlorine Generator",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.uptime.reset\\\",\\\"args\\\":{\\\"slot\\\":5}}",
                "diagnostic",
                "mdi:timer-refresh-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &resetGeneratorUptime);
        }
        const HAButtonEntry resetAllUptime{
            "pooldev",
            "pd_reset_upt_all",
            "Reset Uptime All Pool Devices",
            MqttTopics::SuffixCmd,
            "{\\\"cmd\\\":\\\"pool.uptime.reset_all\\\"}",
            "diagnostic",
            "mdi:timer-refresh-outline"
        };
        (void)haSvc_->addButton(haSvc_->ctx, &resetAllUptime);
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (slots_[i].used) ++count;
    }
    BufferUsageTracker::note(TrackedBufferId::PoolDeviceSlots,
                             (size_t)count * sizeof(PoolDeviceSlot),
                             (size_t)POOL_DEVICE_MAX * sizeof(PoolDeviceSlot),
                             "init",
                             nullptr);
    LOGI("PoolDevice module ready (devices=%u)", (unsigned)count);
    (void)logHub_;
}

void PoolDeviceModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    static constexpr MqttConfigRouteProducer::Route kPoolDeviceCfgRoutes[] = {
        {kCfgMsgBasePdm,
         {(uint8_t)ConfigModuleId::PoolDevice, ConfigBranchRef::UnknownLocalBranch},
         nullptr,
         "",
         (uint8_t)MqttPublishPriority::Low,
         &PoolDeviceModule::buildCfgBasePdmStatic_,
         kPoolDeviceCfgTopicBase},
#define FLOW_POOLDEVICE_CFG_ROUTES(SLOT) \
        {(uint16_t)(kCfgMsgPdmSlotBase + (SLOT)), \
         {(uint8_t)ConfigModuleId::PoolDevice, poolDeviceCfgBranchFromSlot_(SLOT)}, \
         PoolDeviceSlots::kSlots[SLOT].configModuleName, \
         PoolDeviceSlots::kSlots[SLOT].id, \
         (uint8_t)MqttPublishPriority::Normal, \
         nullptr, \
         kPoolDeviceCfgTopicBase}
        FLOW_POOLDEVICE_CFG_ROUTES(0),
        FLOW_POOLDEVICE_CFG_ROUTES(1),
        FLOW_POOLDEVICE_CFG_ROUTES(2),
        FLOW_POOLDEVICE_CFG_ROUTES(3),
        FLOW_POOLDEVICE_CFG_ROUTES(4),
        FLOW_POOLDEVICE_CFG_ROUTES(5),
        FLOW_POOLDEVICE_CFG_ROUTES(6),
        FLOW_POOLDEVICE_CFG_ROUTES(7),
        FLOW_POOLDEVICE_CFG_ROUTES(8),
        FLOW_POOLDEVICE_CFG_ROUTES(9),
        FLOW_POOLDEVICE_CFG_ROUTES(10),
        FLOW_POOLDEVICE_CFG_ROUTES(11),
        FLOW_POOLDEVICE_CFG_ROUTES(12),
        FLOW_POOLDEVICE_CFG_ROUTES(13),
        FLOW_POOLDEVICE_CFG_ROUTES(14),
        FLOW_POOLDEVICE_CFG_ROUTES(15),
#undef FLOW_POOLDEVICE_CFG_ROUTES
    };
    static_assert((sizeof(kPoolDeviceCfgRoutes) / sizeof(kPoolDeviceCfgRoutes[0])) <= MqttConfigRouteProducer::MaxRoutes,
                  "PoolDevice MQTT config route table exceeds producer capacity");

    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kPoolDeviceCfgProducerId,
                               kPoolDeviceCfgRoutes,
                               (uint8_t)(sizeof(kPoolDeviceCfgRoutes) / sizeof(kPoolDeviceCfgRoutes[0])),
                               services);
    }

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        (void)loadPersistedMetrics_(i, s);
        if (runtimePersistBuf_[i][0] != '\0') {
            BufferUsageTracker::note(TrackedBufferId::PoolDeviceRuntimePersistTable,
                                     runtimePersistUsage_(),
                                     runtimePersistCapacity_(),
                                     s.id,
                                     runtimePersistBuf_[i]);
        }
    }
    requestPeriodReconcile_();
}

void PoolDeviceModule::loop()
{
    if (!lockState_(pdMS_TO_TICKS(500))) {
        LOGW("PoolDevice loop skipped: state lock timeout");
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }

    if (!runtimeReady_) {
        if (!configureRuntime_()) {
            unlockState_();
            vTaskDelay(pdMS_TO_TICKS(250));
            return;
        }
    }

    tickDevices_(millis());
    unlockState_();
    vTaskDelay(pdMS_TO_TICKS(200));
}
