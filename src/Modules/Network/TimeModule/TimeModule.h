#pragma once
/**
 * @file TimeModule.h
 * @brief Time synchronization and scheduling module.
 */
#include "Core/Module.h"
#include "Core/ServiceBinding.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"
#include <time.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>

#ifndef FLOW_RTC_PCF85063
#define FLOW_RTC_PCF85063 0
#endif

/** @brief Time sync configuration values. */
struct TimeConfig {
    // Current backend is NTP. The module contract is intentionally generic
    // to allow future backend extensions (RTC, external time source, ...).
    char server1[40] = "pool.ntp.org";
    char server2[40] = "time.nist.gov";
    char tz[64]      = "CET-1CEST,M3.5.0/2,M10.5.0/3";
    char manualTime[24] = "";
    bool enabled = true;
    bool weekStartMonday = true;
};

/**
 * @brief Active module that synchronizes time and drives scheduler events.
 */
class TimeModule : public Module {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::Time; }
    /** @brief Task name. */
    const char* taskName() const override { return "time"; }
    /** @brief Pin control-path scheduler on core 1. */
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 4096; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    /** @brief Depends on log hub, datastore, command and event bus. */
    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        if (i == 2) return ModuleId::Command;
        if (i == 3) return ModuleId::EventBus;
        return ModuleId::Unknown;
    }

    /** @brief Initialize time config and services. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Load persisted scheduler blob once config is fully loaded. */
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Time task loop. */
    void loop() override;

    /** @brief Force a resync attempt. */
    void forceResync();
    /** @brief Apply a user-provided UTC time for the current session and RTC write-back. */
    bool setManualTimeUtc(uint64_t epochSec);

private:
    static constexpr uint32_t INVALID_MINUTE_KEY = 0xFFFFFFFFUL;
    static constexpr size_t TIME_SCHED_BLOB_SIZE = 1536;

    struct SchedulerSlotRuntime {
        bool used = false;
        TimeSchedulerSlot def{};
        bool active = false;
        uint32_t lastTriggerMinuteKey = INVALID_MINUTE_KEY;
    };

    struct SchedulerPendingEvent {
        uint8_t slot = 0;
        uint8_t edge = 0;
        uint8_t replayed = 0;
        uint16_t eventId = 0;
        uint64_t epochSec = 0;
    };

    struct SourceSnapshot {
        bool available = false;
        bool valid = false;
        uint64_t epochSec = 0ULL;
        uint32_t sampledAtMs = 0;
    };

    struct PersistentMeta {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint8_t rtcInitialized = 0;
        uint8_t lastKnownGoodSource = 0;
        uint32_t lastNtpSyncUtc = 0;
        uint32_t lastKnownGoodTimeUtc = 0;
    };

    TimeConfig cfgData{};
    char scheduleBlob_[TIME_SCHED_BLOB_SIZE] = {0};

    ConfigStore* cfgStore = nullptr;
    const CommandService* cmdSvc = nullptr;
    const LogHubService* logHub = nullptr;
    EventBus* eventBus = nullptr;
    DataStore* dataStore = nullptr;
    ServiceRegistry* services_ = nullptr;
    const HmiService* hmiSvc_ = nullptr;
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;

    static void onEventStatic(const Event& e, void* user);
    void onEvent(const Event& e);

    static bool cmdResync(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedInfo(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedGet(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedSet(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedClear(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedClearAll(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    bool handleCmdSchedInfo_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handleCmdSchedGet_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handleCmdSchedSet_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handleCmdSchedClear_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handleCmdSchedClearAll_(const CommandRequest& req, char* reply, size_t replyLen);

    TimeSyncState state = TimeSyncState::WaitingNetwork;
    uint32_t stateTs = 0;

    // Keep existing NVS keys for backward compatibility with deployed devices.
    ConfigVariable<char,0> server1Var {
        NVS_KEY(NvsKeys::Time::Server1),"server1","time",ConfigType::CharArray,
        (char*)cfgData.server1,ConfigPersistence::Persistent,sizeof(cfgData.server1)
    };
    ConfigVariable<char,0> server2Var {
        NVS_KEY(NvsKeys::Time::Server2),"server2","time",ConfigType::CharArray,
        (char*)cfgData.server2,ConfigPersistence::Persistent,sizeof(cfgData.server2)
    };
    ConfigVariable<char,0> tzVar {
        NVS_KEY(NvsKeys::Time::Tz),"tz","time",ConfigType::CharArray,
        (char*)cfgData.tz,ConfigPersistence::Persistent,sizeof(cfgData.tz)
    };
    ConfigVariable<bool,0> enabledVar {
        NVS_KEY(NvsKeys::Time::Enabled),"enabled","time",ConfigType::Bool,
        &cfgData.enabled,ConfigPersistence::Persistent,0
    };
    ConfigVariable<char,0> manualTimeVar {
        NVS_KEY(NvsKeys::Time::ManualTime),"manual_time","time",ConfigType::CharArray,
        (char*)cfgData.manualTime,ConfigPersistence::Persistent,sizeof(cfgData.manualTime)
    };
    ConfigVariable<bool,0> weekStartMondayVar {
        NVS_KEY(NvsKeys::Time::WeekStartMonday),"week_start_mon","time",ConfigType::Bool,
        &cfgData.weekStartMonday,ConfigPersistence::Persistent,0
    };
    ConfigVariable<char,0> scheduleBlobVar {
        NVS_KEY(NvsKeys::Time::ScheduleBlob),"slots_blob","time/scheduler",ConfigType::CharArray,
        (char*)scheduleBlob_,ConfigPersistence::Persistent,sizeof(scheduleBlob_)
    };

    void setState(TimeSyncState s);

    TimeSyncState stateSvc_() const;
    bool isSynced_() const;
    bool isExternalRtc_() const;
    TimeSource sourceSvc_() const;
    const char* sourceNameSvc_() const;
    TimeQuality qualitySvc_() const;
    const char* qualityNameSvc_() const;
    bool currentStateSvc_(TimeState* out) const;
    bool weekStartMondaySvc_() const;
    uint64_t epoch_() const;
    bool formatLocalTime_(char* out, size_t len) const;
    bool setExternalEpoch_(uint64_t epochSec);
    bool setManualEpoch_(uint64_t epochSec);
    bool setRtcEpoch_(uint64_t epochSec, TimeSource source, TimeQuality quality, const char* sourceTag);
    void setActiveSource_(TimeSource source, TimeQuality quality);
    const char* activeSourceName_() const;
    static const char* sourceName_(TimeSource source);
    static const char* qualityName_(TimeQuality quality);
    static bool isTimePlausible_(uint64_t epochSec);
    static bool isTimePlausibleSvc_(uint64_t epochSec);
    static const char* shortStatusFr_(TimeQuality quality, TimeSource source);
    static const char* shortStatusEn_(TimeQuality quality, TimeSource source);
    void updateRuntimeStatus_();
    void logTimeJump_(uint64_t oldEpochSec, uint64_t newEpochSec, TimeSource source, TimeQuality quality);
    void recordSource_(TimeSource source, bool available, bool valid, uint64_t epochSec, uint32_t sampledAtMs);
    void loadPersistentMeta_();
    void persistMetaIfChanged_();
    void noteGoodTime_(TimeSource source, TimeQuality quality, uint64_t epochSec);
    bool ensureHmiService_();
    bool nextionRtcReadEpoch_(uint64_t& epochSec);
    bool nextionRtcWriteEpoch_(uint64_t epochSec);
    void serviceNextionFallback_(uint32_t nowMs);
    void serviceNextionWriteBack_(uint32_t nowMs);
    void serviceManualTimeConfig_(uint32_t nowMs);
    static bool parseManualTime_(const char* text, uint64_t& epochSec);
    static bool epochToHmiRtc_(uint64_t epochSec, HmiRtcDateTime& out);

    bool setSlotSvc_(const TimeSchedulerSlot* slotDef);
    bool getSlotSvc_(uint8_t slot, TimeSchedulerSlot* outDef) const;

    bool setSlot_(const TimeSchedulerSlot& slotDef);
    bool getSlot_(uint8_t slot, TimeSchedulerSlot& outDef) const;
    bool clearSlot_(uint8_t slot);
    bool clearAllSlots_();
    uint8_t usedCount_() const;
    uint16_t activeMask_() const;
    bool isActive_(uint8_t slot) const;

    bool loadScheduleFromBlob_();
    bool serializeSchedule_(char* out, size_t outLen) const;
    bool persistSchedule_();
    time_t nowEpoch_() const;
    static void sanitizeLabel_(char* label);
    void applySystemSlots_(SchedulerSlotRuntime* slots, size_t count) const;
    bool isSystemSlot_(uint8_t slot) const;
    bool isMonthStartEvent_(const SchedulerSlotRuntime& slotRt, const tm& localNow) const;
    void resetScheduleRuntime_();
    void tickScheduler_();

    static uint8_t weekBitFromTm_(const tm& localNow);
    static uint32_t minuteOfDay_(const tm& localNow);
    static bool isWeekdayEnabled_(uint8_t mask, uint8_t weekBit);
    static bool isRecurringTriggerNow_(const TimeSchedulerSlot& def, uint8_t weekBit, uint32_t minuteOfDay);
    static bool isRecurringActiveNow_(const TimeSchedulerSlot& def, uint8_t weekBit, uint8_t prevWeekBit,
                                      uint32_t minuteOfDay);

#if FLOW_RTC_PCF85063
    bool ensureInternalRtcInit_();
    bool internalRtcReadEpoch_(uint64_t& epochSec);
    bool internalRtcWriteEpoch_(uint64_t epochSec);
    bool internalRtcBatteryPresent_();
    bool internalRtcReadRegs_(uint8_t reg, uint8_t* data, uint8_t len);
    bool internalRtcWriteRegs_(uint8_t reg, const uint8_t* data, uint8_t len);
    void serviceInternalRtcFallback_(uint32_t nowMs);
    void serviceInternalRtcWriteBack_(uint32_t nowMs);
    void serviceInternalRtcDailyResync_(uint32_t nowMs);
#endif

    // ---- network warmup ----
    bool _netReady = false;
    uint32_t _netReadyTs = 0;

    // ---- retry backoff ----
    uint8_t _retryCount = 0;
    uint32_t _retryDelayMs = 2000; // 2s start
    bool syncedFromExternalRtc_ = false;
    bool syncedFromInternalRtc_ = false;
    TimeSource activeSource_ = TimeSource::None;
    TimeQuality activeQuality_ = TimeQuality::Invalid;
    TimeState timeState_{};
    PersistentMeta persistentMeta_{};
    PersistentMeta lastPersistedMeta_{};
    bool persistentMetaLoaded_ = false;
    SourceSnapshot ntpSource_{};
    SourceSnapshot internalRtcSource_{};
    SourceSnapshot nextionSource_{};
    uint32_t lastNextionRtcReadAttemptMs_ = 0;
    uint32_t lastNextionRtcWriteAttemptMs_ = 0;
    uint32_t lastNextionRtcWriteDayStamp_ = 0xFFFFFFFFUL;
    bool nextionRtcWritePending_ = false;
    char manualTimeApplied_[sizeof(TimeConfig::manualTime)] = {0};
    uint32_t lastManualTimeAttemptMs_ = 0;

#if FLOW_RTC_PCF85063
    bool internalRtcInitDone_ = false;
    bool internalRtcReady_ = false;
    int internalRtcSda_ = -1;
    int internalRtcScl_ = -1;
    uint32_t internalRtcFreqHz_ = 400000U;
    uint32_t lastInternalRtcReadAttemptMs_ = 0;
    uint32_t lastInternalRtcWriteAttemptMs_ = 0;
    uint32_t lastInternalRtcResyncMs_ = 0;
    uint32_t lastInternalRtcWriteDayStamp_ = 0xFFFFFFFFUL;
    bool internalRtcWritePending_ = false;
#endif

    // ---- time scheduler ----
    mutable portMUX_TYPE schedMux_ = portMUX_INITIALIZER_UNLOCKED;
    SchedulerSlotRuntime sched_[TIME_SCHED_MAX_SLOTS]{};
    SchedulerPendingEvent schedulerPending_[TIME_SCHED_MAX_SLOTS * 2]{};
    bool schedNeedsReload_ = true;
    bool schedInitialized_ = false;
    uint16_t activeMaskValue_ = 0;
    uint32_t simBootMs_ = 0;
    uint64_t lastSchedulerEvalEpochSec_ = 0;

    TimeService timeSvc_{
        ServiceBinding::bind<&TimeModule::stateSvc_>,
        ServiceBinding::bind<&TimeModule::isSynced_>,
        ServiceBinding::bind<&TimeModule::epoch_>,
        ServiceBinding::bind<&TimeModule::formatLocalTime_>,
        this,
        ServiceBinding::bind<&TimeModule::setExternalEpoch_>,
        ServiceBinding::bind<&TimeModule::isExternalRtc_>,
        ServiceBinding::bind<&TimeModule::sourceSvc_>,
        ServiceBinding::bind<&TimeModule::sourceNameSvc_>,
        ServiceBinding::bind<&TimeModule::qualitySvc_>,
        ServiceBinding::bind<&TimeModule::qualityNameSvc_>,
        ServiceBinding::bind<&TimeModule::currentStateSvc_>,
        ServiceBinding::bind<&TimeModule::setManualEpoch_>,
        &TimeModule::isTimePlausibleSvc_,
        ServiceBinding::bind<&TimeModule::weekStartMondaySvc_>
    };
    TimeSchedulerService schedSvc_{
        ServiceBinding::bind<&TimeModule::setSlotSvc_>,
        ServiceBinding::bind<&TimeModule::getSlotSvc_>,
        ServiceBinding::bind<&TimeModule::clearSlot_>,
        ServiceBinding::bind<&TimeModule::clearAllSlots_>,
        ServiceBinding::bind<&TimeModule::usedCount_>,
        ServiceBinding::bind<&TimeModule::activeMask_>,
        ServiceBinding::bind<&TimeModule::isActive_>,
        this
    };
};
