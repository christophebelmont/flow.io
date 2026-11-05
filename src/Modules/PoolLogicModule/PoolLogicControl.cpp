/**
 * @file PoolLogicControl.cpp
 * @brief Control loop, device state, and alarm conditions for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

namespace {
constexpr float kHeaterHysteresisC = 0.3f;
constexpr uint32_t kHeaterTempFreshMaxMs = 10UL * 60UL * 1000UL;
constexpr uint16_t kHeatAssistProbeRunSec = 5U * 60U;
constexpr uint16_t kHeatAssistIdleSlowSec = 30U * 60U;
constexpr uint16_t kHeatAssistIdleFastSec = 20U * 60U;
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
constexpr uint16_t kHeatAssistAdaptiveFallbackMin = 30U;
constexpr uint16_t kHeatAssistAdaptiveMinIntervalMin = 20U;
constexpr uint16_t kHeatAssistAdaptiveMaxIntervalMin = 60U;
constexpr float kHeatAssistAdaptiveLowDeltaC = 10.0f;
constexpr float kHeatAssistAdaptiveHighDeltaC = 20.0f;
#endif
constexpr uint8_t kHeatAssistFlagProbeRunning = (1U << 0);
constexpr uint8_t kHeatAssistFlagHeatingActive = (1U << 1);
constexpr uint8_t kHeatAssistFlagFastCycle = (1U << 2);
constexpr uint32_t kO2PersistPeriodMs = 30000UL;
constexpr float kO2DoseEpsilonMl = 0.5f;

const char* poolDeviceSvcStatusStr_(PoolDeviceSvcStatus st)
{
    switch (st) {
        case POOLDEV_SVC_OK: return "ok";
        case POOLDEV_SVC_ERR_INVALID_ARG: return "invalid_arg";
        case POOLDEV_SVC_ERR_UNKNOWN_SLOT: return "unknown_slot";
        case POOLDEV_SVC_ERR_NOT_READY: return "not_ready";
        case POOLDEV_SVC_ERR_DISABLED: return "disabled";
        case POOLDEV_SVC_ERR_INTERLOCK: return "interlock";
        case POOLDEV_SVC_ERR_IO: return "io";
        case POOLDEV_SVC_ERR_MAX_UPTIME: return "max_uptime";
        case POOLDEV_SVC_ERR_WRITES_DISABLED: return "writes_disabled";
        default: return "unknown";
    }
}

const char* poolDeviceBlockReasonStr_(uint8_t reason)
{
    switch (reason) {
        case POOL_DEVICE_BLOCK_NONE: return "none";
        case POOL_DEVICE_BLOCK_DISABLED: return "disabled";
        case POOL_DEVICE_BLOCK_INTERLOCK: return "interlock";
        case POOL_DEVICE_BLOCK_IO_ERROR: return "io_error";
        case POOL_DEVICE_BLOCK_MAX_UPTIME: return "max_uptime";
        case POOL_DEVICE_BLOCK_IO_DISABLED: return "io_disabled";
        default: return "unknown";
    }
}

uint16_t o2WeekKeyFromDayKey_(uint16_t dayKey)
{
    return (uint16_t)(dayKey / 7U);
}
}  // namespace

bool PoolLogicModule::isDisinfectionType_(DisinfectionType type) const
{
    return disinfectionType_ == (uint8_t)type;
}

const char* PoolLogicModule::disinfectionTypeStr_(uint8_t type)
{
    switch (type) {
        case DisinfectionChlorineBromine: return "chlorine_bromine";
        case DisinfectionSwg: return "swg";
        case DisinfectionActiveOxygen: return "active_oxygen";
        case DisinfectionDisabled: return "disabled";
        default: return "unknown";
    }
}

const char* PoolLogicModule::swgControlModeStr_(uint8_t mode)
{
    switch (mode) {
        case SwgControlOrp: return "orp";
        case SwgControlContinuous: return "continuous";
        default: return "unknown";
    }
}

const char* PoolLogicModule::o2ProtocolStateStr_(uint8_t state)
{
    switch (state) {
        case O2ProtocolIdle: return "idle";
        case O2ProtocolPending: return "pending";
        case O2ProtocolDosing: return "dosing";
        case O2ProtocolBlocked: return "blocked";
        default: return "unknown";
    }
}

const char* PoolLogicModule::o2BlockReasonStr_(uint8_t reason)
{
    switch (reason) {
        case O2BlockNone: return "none";
        case O2BlockInactive: return "inactive";
        case O2BlockTimeUnsynced: return "time_unsynced";
        case O2BlockPsi: return "psi";
        case O2BlockTankLow: return "tank_low";
        case O2BlockFlowInvalid: return "flow_invalid";
        case O2BlockFiltrationWait: return "filtration_wait";
        case O2BlockPumpService: return "pump_service";
        case O2BlockPumpBlocked: return "pump_blocked";
        case O2BlockConfig: return "config";
        default: return "unknown";
    }
}

ActivityRole PoolLogicModule::activityRoleForDeviceSlot_(uint8_t deviceSlot) const
{
    if (deviceSlot == filtrationDeviceSlot_) return ActivityRole::Filtration;
    if (deviceSlot == swgDeviceSlot_) return ActivityRole::Swg;
    if (deviceSlot == robotDeviceSlot_) return ActivityRole::Robot;
    if (deviceSlot == fillingDeviceSlot_) return ActivityRole::Filling;
    if (deviceSlot == phPumpDeviceSlot_) return ActivityRole::Ph;
    if (deviceSlot == orpPumpDeviceSlot_) return ActivityRole::Disinfection;
    if (deviceSlot == heaterDeviceSlot_) return ActivityRole::Heater;
    return ActivityRole::None;
}

const char* PoolLogicModule::activityRoleLabel_(ActivityRole role) const
{
    switch (role) {
        case ActivityRole::Filtration: return "Filtration";
        case ActivityRole::Swg: return "Électrolyseur";
        case ActivityRole::Robot: return "Robot";
        case ActivityRole::Filling: return "Remplissage";
        case ActivityRole::Ph: return "Pompe pH";
        case ActivityRole::Disinfection: return "Désinfection";
        case ActivityRole::Heater: return "Chauffage";
        case ActivityRole::None:
        default: return "Équipement";
    }
}

void PoolLogicModule::emitActivity_(ActivityCode code,
                                    ActivitySource source,
                                    ActivitySeverity severity,
                                    ActivityRole role,
                                    ActivityState state,
                                    ActivityReason reason,
                                    uint8_t deviceSlot,
                                    const char* title,
                                    const char* detail,
                                    const char* icon) const
{
    if (!activityLogSvc_ || !activityLogSvc_->emit) return;
    ActivityEvent event{};
    event.code = (uint16_t)code;
    event.domain = (uint8_t)ActivityDomain::PoolLogic;
    event.source = (uint8_t)source;
    event.severity = (uint8_t)severity;
    event.role = (uint8_t)role;
    event.state = (uint8_t)state;
    event.reason = (uint8_t)reason;
    event.targetSlot = deviceSlot;
    snprintf(event.title, sizeof(event.title), "%s", title ? title : "PoolLogic");
    snprintf(event.detail, sizeof(event.detail), "%s", detail ? detail : "");
    snprintf(event.icon, sizeof(event.icon), "%s", icon ? icon : "pool");
    (void)activityLogSvc_->emit(activityLogSvc_->ctx, &event);
}

void PoolLogicModule::emitDeviceActivity_(bool requested,
                                          bool on,
                                          uint8_t deviceSlot,
                                          const char* label,
                                          ActivityReason reason) const
{
    const ActivityRole role = activityRoleForDeviceSlot_(deviceSlot);
    const char* roleLabel = activityRoleLabel_(role);
    const char* deviceLabel = (label && label[0] != '\0') ? label : roleLabel;
    char title[48] = {0};
    char detail[128] = {0};
    const char* icon = "power_settings_new";
    if (role == ActivityRole::Filtration) icon = "pool";
    else if (role == ActivityRole::Robot) icon = "cleaning_services";
    else if (role == ActivityRole::Ph || role == ActivityRole::Disinfection) icon = "science";
    else if (role == ActivityRole::Heater) icon = "heat_pump";
    else if (role == ActivityRole::Filling) icon = "water_drop";
    else if (role == ActivityRole::Swg) icon = "bolt";

    if (requested) {
        snprintf(title,
                 sizeof(title),
                 "%s %s demandé",
                 deviceLabel,
                 on ? "ON" : "OFF");
        snprintf(detail,
                 sizeof(detail),
                 "PoolLogic demande %s pour %s (slot %u).",
                 on ? "le démarrage" : "l'arrêt",
                 roleLabel,
                 (unsigned)deviceSlot);
        emitActivity_(on ? ActivityCode::PoolLogicDeviceStartRequested
                         : ActivityCode::PoolLogicDeviceStopRequested,
                      ActivitySource::Auto,
                      ActivitySeverity::Info,
                      role,
                      on ? ActivityState::RequestedOn : ActivityState::RequestedOff,
                      reason,
                      deviceSlot,
                      title,
                      detail,
                      icon);
        return;
    }

    snprintf(title,
             sizeof(title),
             "%s %s",
             deviceLabel,
             on ? "a démarré" : "s'est arrêté");
    snprintf(detail,
             sizeof(detail),
             "État réel observé pour %s (slot %u).",
             roleLabel,
             (unsigned)deviceSlot);
    emitActivity_(on ? ActivityCode::PoolLogicDeviceStarted : ActivityCode::PoolLogicDeviceStopped,
                  ActivitySource::Auto,
                  on ? ActivitySeverity::Success : ActivitySeverity::Info,
                  role,
                  on ? ActivityState::On : ActivityState::Off,
                  ActivityReason::None,
                  deviceSlot,
                  title,
                  detail,
                  icon);
}

void PoolLogicModule::emitAutoModeDisabledByManualActivity_(ActivityRole role,
                                                            uint8_t deviceSlot,
                                                            const char* autoLabel) const
{
    const char* roleLabel = activityRoleLabel_(role);
    const char* label = (autoLabel && autoLabel[0] != '\0') ? autoLabel : roleLabel;
    const char* icon = "settings";
    if (role == ActivityRole::Filtration) icon = "pool";
    else if (role == ActivityRole::Ph || role == ActivityRole::Disinfection) icon = "science";

    char title[48] = {0};
    char detail[128] = {0};
    snprintf(title, sizeof(title), "Mode auto %s désactivé", label);
    snprintf(detail,
             sizeof(detail),
             "Commande manuelle sur %s: l'automatisme a été désactivé.",
             roleLabel);
    emitActivity_(ActivityCode::SystemConfigChanged,
                  ActivitySource::Manual,
                  ActivitySeverity::Info,
                  role,
                  ActivityState::None,
                  ActivityReason::Manual,
                  deviceSlot,
                  title,
                  detail,
                  icon);
}

bool PoolLogicModule::readPoolDeviceFlowLh_(uint8_t deviceSlot, float& flowLhOut) const
{
    flowLhOut = 0.0f;
    if (!cfgStore_ || deviceSlot >= POOL_DEVICE_MAX) return false;

    char moduleName[16] = {0};
    snprintf(moduleName, sizeof(moduleName), "pdm/pd%u", (unsigned)deviceSlot);

    bool truncated = false;
    if (!cfgStore_->toJsonModule(moduleName,
                                 o2PoolDeviceJsonBuf_,
                                 sizeof(o2PoolDeviceJsonBuf_),
                                 &truncated) ||
        truncated) {
        return false;
    }

    const char* key = strstr(o2PoolDeviceJsonBuf_, "\"flow_l_h\":");
    if (!key) return false;
    key += 11;
    char* end = nullptr;
    const float flow = strtof(key, &end);
    if (end == key) return false;
    if (!std::isfinite(flow) || flow <= 0.0f) return false;
    flowLhOut = flow;
    return true;
}

bool PoolLogicModule::currentO2LocalTime_(uint16_t& dayKeyOut,
                                          uint16_t& weekKeyOut,
                                          uint8_t& weekDayMon0Out,
                                          uint8_t& hourOut) const
{
    dayKeyOut = 0;
    weekKeyOut = 0;
    weekDayMon0Out = 0;
    hourOut = 0;
    if (!timeSvc_ || !timeSvc_->isSynced || !timeSvc_->epoch || !timeSvc_->isSynced(timeSvc_->ctx)) {
        return false;
    }

    const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
    if (epoch < 1609459200ULL) return false;
    const time_t now = (time_t)epoch;
    tm localNow{};
    if (!localtime_r(&now, &localNow)) return false;

    const uint64_t epochDay = epoch / 86400ULL;
    dayKeyOut = (uint16_t)(epochDay & 0xFFFFU);
    weekKeyOut = (uint16_t)((epochDay / 7ULL) & 0xFFFFU);
    weekDayMon0Out = (uint8_t)((localNow.tm_wday + 6) % 7);
    hourOut = (uint8_t)localNow.tm_hour;
    return true;
}

bool PoolLogicModule::isO2DoseDay_(uint8_t weekDayMon0) const
{
    if (o2SplitCount_ <= 1U) return weekDayMon0 == 0U;
    if (o2SplitCount_ == 2U) return weekDayMon0 == 0U || weekDayMon0 == 3U;
    return weekDayMon0 == 0U || weekDayMon0 == 2U || weekDayMon0 == 4U;
}

float PoolLogicModule::o2TemperatureFactor_(bool haveWaterTemp, float waterTemp) const
{
    if (!o2TempComp_ || !haveWaterTemp || !std::isfinite(waterTemp)) return 1.0f;
    if (waterTemp <= 18.0f) {
        const float factor = 1.0f - ((18.0f - waterTemp) * 0.02f);
        return (factor < 0.75f) ? 0.75f : factor;
    }
    if (waterTemp >= 24.0f) {
        const float factor = 1.0f + ((waterTemp - 24.0f) * 0.03f);
        return (factor > 1.50f) ? 1.50f : factor;
    }
    return 1.0f;
}

float PoolLogicModule::computeO2WeeklyDoseMl_(bool haveWaterTemp, float waterTemp) const
{
    if (!std::isfinite(o2PoolVolumeM3_) || o2PoolVolumeM3_ <= 0.0f ||
        !std::isfinite(o2DoseMlPer10M3Week_) || o2DoseMlPer10M3Week_ <= 0.0f ||
        !std::isfinite(o2LoadFactor_) || o2LoadFactor_ <= 0.0f) {
        return 0.0f;
    }

    const float base = (o2PoolVolumeM3_ / 10.0f) * o2DoseMlPer10M3Week_;
    const float dose = base * o2LoadFactor_ * o2TemperatureFactor_(haveWaterTemp, waterTemp);
    if (!std::isfinite(dose) || dose <= 0.0f) return 0.0f;
    return dose;
}

void PoolLogicModule::setO2ProtocolState_(uint8_t state, uint8_t blockReason, uint32_t nowMs)
{
    if (state > O2ProtocolBlocked) state = O2ProtocolIdle;
    if (o2ProtocolState_ != state || o2BlockReason_ != blockReason) {
        o2ProtocolState_ = state;
        o2BlockReason_ = blockReason;
        o2LastPersistMs_ = 0;
        LOGI("O2 protocol state=%s block=%s pending=%.1f done=%.1f",
             o2ProtocolStateStr_(o2ProtocolState_),
             o2BlockReasonStr_(o2BlockReason_),
             (double)o2PendingMl_,
             (double)o2WeeklyDoneMl_);
        char detail[128] = {0};
        snprintf(detail,
                 sizeof(detail),
                 "État=%s, blocage=%s, restant=%.1f ml, semaine=%.1f ml.",
                 o2ProtocolStateStr_(o2ProtocolState_),
                 o2BlockReasonStr_(o2BlockReason_),
                 (double)o2PendingMl_,
                 (double)o2WeeklyDoneMl_);
        if (isDisinfectionType_(DisinfectionActiveOxygen)) {
            emitActivity_(ActivityCode::PoolLogicO2StateChanged,
                          (blockReason == O2BlockNone) ? ActivitySource::Auto : ActivitySource::Safety,
                          (blockReason == O2BlockNone) ? ActivitySeverity::Info : ActivitySeverity::Warning,
                          ActivityRole::Disinfection,
                          ActivityState::None,
                          ActivityReason::O2,
                          orpPumpDeviceSlot_,
                          "Protocole oxygène actif mis à jour",
                          detail,
                          "science");
        }
    }
    persistO2Protocol_(nowMs, false);
}

void PoolLogicModule::persistO2Protocol_(uint32_t nowMs, bool force)
{
    if (!cfgStore_) return;
    if (!force && o2LastPersistMs_ != 0U && (uint32_t)(nowMs - o2LastPersistMs_) < kO2PersistPeriodMs) {
        return;
    }

    (void)cfgStore_->set(o2ProtocolStateVar_, o2ProtocolState_);
    (void)cfgStore_->set(o2LastDoseDayVar_, o2LastDoseDay_);
    (void)cfgStore_->set(o2WeeklyDoneVar_, o2WeeklyDoneMl_);
    (void)cfgStore_->set(o2PendingVar_, o2PendingMl_);
    o2LastPersistMs_ = nowMs ? nowMs : 1U;
}

bool PoolLogicModule::stepO2Protocol_(bool filtrationDesired,
                                      bool filtrationOn,
                                      uint32_t filtrationRunMin,
                                      bool haveWaterTemp,
                                      float waterTemp,
                                      bool psiError,
                                      bool tankLow,
                                      uint32_t nowMs,
                                      bool& requestFiltrationOut,
                                      bool& pumpDesiredOut)
{
    requestFiltrationOut = false;
    pumpDesiredOut = false;

    if (!isDisinfectionType_(DisinfectionActiveOxygen) || !autoMode_) {
        o2LastProgressMs_ = 0;
        if (o2PendingMl_ <= kO2DoseEpsilonMl) {
            o2PendingMl_ = 0.0f;
            setO2ProtocolState_(O2ProtocolIdle, O2BlockInactive, nowMs);
        } else {
            setO2ProtocolState_(O2ProtocolPending, O2BlockInactive, nowMs);
        }
        return false;
    }

    uint16_t dayKey = 0;
    uint16_t weekKey = 0;
    uint8_t weekDayMon0 = 0;
    uint8_t hour = 0;
    if (!currentO2LocalTime_(dayKey, weekKey, weekDayMon0, hour)) {
        o2LastProgressMs_ = 0;
        if (o2PendingMl_ > kO2DoseEpsilonMl) requestFiltrationOut = true;
        setO2ProtocolState_(o2PendingMl_ > kO2DoseEpsilonMl ? O2ProtocolBlocked : O2ProtocolIdle,
                            O2BlockTimeUnsynced,
                            nowMs);
        return false;
    }

    if (o2LastDoseDay_ != 0U && o2WeekKeyFromDayKey_(o2LastDoseDay_) != weekKey && o2WeeklyDoneMl_ > 0.0f) {
        o2WeeklyDoneMl_ = 0.0f;
        persistO2Protocol_(nowMs, true);
    }

    const float weeklyDoseMl = computeO2WeeklyDoseMl_(haveWaterTemp, waterTemp);
    const uint8_t split = (o2SplitCount_ == 0U) ? 1U : ((o2SplitCount_ > 3U) ? 3U : o2SplitCount_);
    const float doseMl = (split > 0U) ? (weeklyDoseMl / (float)split) : weeklyDoseMl;
    o2LastPlannedDoseMl_ = doseMl;

    if (o2PendingMl_ <= kO2DoseEpsilonMl) {
        o2PendingMl_ = 0.0f;
        if (weeklyDoseMl <= kO2DoseEpsilonMl || doseMl <= kO2DoseEpsilonMl) {
            setO2ProtocolState_(O2ProtocolBlocked, O2BlockConfig, nowMs);
            return false;
        }
        const bool dueToday =
            isO2DoseDay_(weekDayMon0) &&
            hour >= o2MainHour_ &&
            o2LastDoseDay_ != dayKey &&
            o2WeeklyDoneMl_ < (weeklyDoseMl - kO2DoseEpsilonMl);
        if (dueToday) {
            const float remainingWeekMl = weeklyDoseMl - o2WeeklyDoneMl_;
            o2PendingMl_ = (remainingWeekMl < doseMl) ? remainingWeekMl : doseMl;
            if (o2PendingMl_ < 0.0f) o2PendingMl_ = 0.0f;
            o2LastProgressMs_ = 0;
            setO2ProtocolState_(O2ProtocolPending, O2BlockNone, nowMs);
            persistO2Protocol_(nowMs, true);
        }
    }

    if (o2PendingMl_ <= kO2DoseEpsilonMl) {
        o2PendingMl_ = 0.0f;
        setO2ProtocolState_(O2ProtocolIdle, O2BlockNone, nowMs);
        return false;
    }

    requestFiltrationOut = true;
    if (psiError) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockPsi, nowMs);
        return false;
    }
    if (tankLow) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockTankLow, nowMs);
        return false;
    }

    float flowLh = 0.0f;
    if (!readPoolDeviceFlowLh_(orpPumpDeviceSlot_, flowLh)) {
        o2LastFlowLh_ = 0.0f;
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockFlowInvalid, nowMs);
        return false;
    }
    o2LastFlowLh_ = flowLh;

    const bool filtrationReady = filtrationOn && filtrationDesired && filtrationRunMin >= o2MinFilterRunMin_;
    if (!filtrationReady) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolPending, O2BlockFiltrationWait, nowMs);
        return false;
    }

    PoolDeviceSvcMeta meta{};
    if (!poolSvc_ || !poolSvc_->meta || poolSvc_->meta(poolSvc_->ctx, orpPumpDeviceSlot_, &meta) != POOLDEV_SVC_OK) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockPumpService, nowMs);
        return false;
    }
    if (meta.blockReason == POOL_DEVICE_BLOCK_MAX_UPTIME ||
        meta.blockReason == POOL_DEVICE_BLOCK_DISABLED ||
        meta.blockReason == POOL_DEVICE_BLOCK_IO_DISABLED ||
        meta.blockReason == POOL_DEVICE_BLOCK_INTERLOCK ||
        meta.blockReason == POOL_DEVICE_BLOCK_IO_ERROR) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockPumpBlocked, nowMs);
        return false;
    }

    pumpDesiredOut = true;
    if (orpPumpFsm_.on) {
        const uint32_t deltaMs = (o2LastProgressMs_ == 0U) ? 0U : (nowMs - o2LastProgressMs_);
        o2LastProgressMs_ = nowMs ? nowMs : 1U;
        if (deltaMs > 0U) {
            const float injectedMl = (flowLh / 3600.0f) * (float)deltaMs;
            if (std::isfinite(injectedMl) && injectedMl > 0.0f) {
                o2PendingMl_ -= injectedMl;
                o2WeeklyDoneMl_ += injectedMl;
                if (o2PendingMl_ <= kO2DoseEpsilonMl) {
                    if (o2PendingMl_ < 0.0f) {
                        o2WeeklyDoneMl_ += o2PendingMl_;
                    }
                    o2PendingMl_ = 0.0f;
                    o2LastDoseDay_ = dayKey;
                    pumpDesiredOut = false;
                    o2LastProgressMs_ = 0;
                    setO2ProtocolState_(O2ProtocolIdle, O2BlockNone, nowMs);
                    persistO2Protocol_(nowMs, true);
                    return true;
                }
                setO2ProtocolState_(O2ProtocolDosing, O2BlockNone, nowMs);
                persistO2Protocol_(nowMs, false);
            }
        } else {
            setO2ProtocolState_(O2ProtocolDosing, O2BlockNone, nowMs);
        }
    } else {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolPending, O2BlockNone, nowMs);
    }

    return pumpDesiredOut;
}

// Alarm conditions intentionally stay close to the control helpers because they
// read the same live IO/runtime state and should evolve together.
AlarmCondState PoolLogicModule::condPsiLowStatic_(void* ctx, uint32_t nowMs)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    if (!self->filtrationFsm_.on) return AlarmCondState::False;
    const uint32_t runSec = self->stateUptimeSec_(self->filtrationFsm_, nowMs);
    if (runSec <= self->psiStartupDelaySec_) return AlarmCondState::False;

    float psi = 0.0f;
    if (!self->loadAnalogSensor_(self->psiIoId_, psi)) {
        return AlarmCondState::Unknown;
    }

    return (psi < self->psiLowThreshold_) ? AlarmCondState::True : AlarmCondState::False;
}

AlarmCondState PoolLogicModule::condPsiHighStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    if (!self->filtrationFsm_.on) return AlarmCondState::False;

    float psi = 0.0f;
    if (!self->loadAnalogSensor_(self->psiIoId_, psi)) {
        return AlarmCondState::Unknown;
    }

    return (psi > self->psiHighThreshold_) ? AlarmCondState::True : AlarmCondState::False;
}

AlarmCondState PoolLogicModule::condPhTankLowStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;

    bool low = false;
    if (!self->loadDigitalSensor_(self->phLevelIoId_, low)) {
        return AlarmCondState::Unknown;
    }
    return low ? AlarmCondState::True : AlarmCondState::False;
}

AlarmCondState PoolLogicModule::condChlorineTankLowStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;

    bool low = false;
    if (!self->loadDigitalSensor_(self->chlorineLevelIoId_, low)) {
        return AlarmCondState::Unknown;
    }
    return low ? AlarmCondState::True : AlarmCondState::False;
}

AlarmCondState PoolLogicModule::condWaterLevelLowStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;

    bool low = false;
    if (!self->loadDigitalSensor_(self->levelIoId_, low)) {
        return AlarmCondState::Unknown;
    }
    // Harmonized digital level semantics: true means "problem detected".
    return low ? AlarmCondState::True : AlarmCondState::False;
}

AlarmCondState PoolLogicModule::condPhPumpMaxUptimeStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    return self ? self->condPumpMaxUptime_(self->phPumpDeviceSlot_) : AlarmCondState::Unknown;
}

AlarmCondState PoolLogicModule::condChlorinePumpMaxUptimeStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    return self ? self->condPumpMaxUptime_(self->orpPumpDeviceSlot_) : AlarmCondState::Unknown;
}

AlarmCondState PoolLogicModule::condPumpMaxUptime_(uint8_t deviceSlot) const
{
    if (!poolSvc_ || !poolSvc_->meta) return AlarmCondState::Unknown;

    PoolDeviceSvcMeta meta{};
    const PoolDeviceSvcStatus st = poolSvc_->meta(poolSvc_->ctx, deviceSlot, &meta);
    if (st == POOLDEV_SVC_OK) {
        return (meta.blockReason == POOL_DEVICE_BLOCK_MAX_UPTIME) ? AlarmCondState::True : AlarmCondState::False;
    }
    if (st == POOLDEV_SVC_ERR_DISABLED) return AlarmCondState::False;
    return AlarmCondState::Unknown;
}

bool PoolLogicModule::readDeviceActualOn_(uint8_t deviceSlot, bool& onOut) const
{
    if (!poolSvc_ || !poolSvc_->readActualOn) return false;
    uint8_t on = 0;
    if (poolSvc_->readActualOn(poolSvc_->ctx, deviceSlot, &on, nullptr) != POOLDEV_SVC_OK) return false;
    onOut = (on != 0U);
    return true;
}

bool PoolLogicModule::writeDeviceDesired_(uint8_t deviceSlot, bool on)
{
    if (!poolSvc_ || !poolSvc_->writeDesired) return false;
    const PoolDeviceSvcStatus st = poolSvc_->writeDesired(poolSvc_->ctx, deviceSlot, on ? 1U : 0U);
    if (st != POOLDEV_SVC_OK) {
        PoolDeviceSvcMeta meta{};
        const bool haveMeta = poolSvc_->meta &&
                              (poolSvc_->meta(poolSvc_->ctx, deviceSlot, &meta) == POOLDEV_SVC_OK);
        if (haveMeta) {
            LOGW("pooldev.writeDesired failed slot=%u desired=%u st=%u(%s) block=%u(%s) enabled=%u io=%u",
                 (unsigned)deviceSlot,
                 on ? 1u : 0u,
                 (unsigned)st,
                 poolDeviceSvcStatusStr_(st),
                 (unsigned)meta.blockReason,
                 poolDeviceBlockReasonStr_(meta.blockReason),
                 (unsigned)meta.enabled,
                 (unsigned)meta.ioId);
        } else {
            LOGW("pooldev.writeDesired failed slot=%u desired=%u st=%u(%s)",
                 (unsigned)deviceSlot,
                 on ? 1u : 0u,
                 (unsigned)st,
                 poolDeviceSvcStatusStr_(st));
        }
        return false;
    }
    return true;
}

bool PoolLogicModule::setPoolDeviceWritesEnabled_(bool enabled)
{
    if (!poolSvc_ || !poolSvc_->setWritesEnabled) return false;
    const PoolDeviceSvcStatus st = poolSvc_->setWritesEnabled(poolSvc_->ctx, enabled ? 1U : 0U);
    if (st != POOLDEV_SVC_OK) {
        LOGW("pooldev.setWritesEnabled failed enabled=%u st=%u(%s)",
             enabled ? 1u : 0u,
             (unsigned)st,
             poolDeviceSvcStatusStr_(st));
        return false;
    }
    return true;
}

// Device FSM synchronization is edge-oriented: it preserves the current state
// and only emits transitions when the observed hardware state changes.
void PoolLogicModule::syncDeviceState_(uint8_t deviceSlot, DeviceFsm& fsm, uint32_t nowMs, bool& turnedOnOut, bool& turnedOffOut)
{
    turnedOnOut = false;
    turnedOffOut = false;

    bool actualOn = false;
    if (!readDeviceActualOn_(deviceSlot, actualOn)) {
        return;
    }

    if (!fsm.known) {
        fsm.known = true;
        fsm.on = actualOn;
        fsm.stateSinceMs = nowMs;
        return;
    }

    if (fsm.on != actualOn) {
        turnedOnOut = (!fsm.on && actualOn);
        turnedOffOut = (fsm.on && !actualOn);
        fsm.on = actualOn;
        fsm.stateSinceMs = nowMs;
        PoolDeviceSvcMeta meta{};
        const char* label = nullptr;
        if (poolSvc_ && poolSvc_->meta &&
            poolSvc_->meta(poolSvc_->ctx, deviceSlot, &meta) == POOLDEV_SVC_OK &&
            meta.label[0] != '\0') {
            label = meta.label;
        }
        emitDeviceActivity_(false, actualOn, deviceSlot, label, ActivityReason::None);
    }
}

void PoolLogicModule::syncAllDeviceStates_(uint32_t nowMs)
{
    bool unusedStart = false;
    bool unusedStop = false;
    syncDeviceState_(filtrationDeviceSlot_, filtrationFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(robotDeviceSlot_, robotFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(swgDeviceSlot_, swgFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(fillingDeviceSlot_, fillingFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(phPumpDeviceSlot_, phPumpFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(orpPumpDeviceSlot_, orpPumpFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(heaterDeviceSlot_, heaterFsm_, nowMs, unusedStart, unusedStop);
}

void PoolLogicModule::adoptBootDeviceState_(uint32_t nowMs)
{
    syncAllDeviceStates_(nowMs);
    filtrationFsm_.lastDesired = filtrationFsm_.on;
    robotFsm_.lastDesired = robotFsm_.on;
    swgFsm_.lastDesired = swgFsm_.on;
    fillingFsm_.lastDesired = fillingFsm_.on;
    phPumpFsm_.lastDesired = phPumpFsm_.on;
    orpPumpFsm_.lastDesired = orpPumpFsm_.on;
    heaterFsm_.lastDesired = heaterFsm_.on;
}

uint32_t PoolLogicModule::stateUptimeSec_(const DeviceFsm& fsm, uint32_t nowMs) const
{
    if (!fsm.known || !fsm.on) return 0;
    return (uint32_t)((nowMs - fsm.stateSinceMs) / 1000UL);
}

bool PoolLogicModule::loadAnalogSensor_(IoId ioId, float& out, uint32_t* tsMsOut) const
{
    if (!ioSvc_ || !ioSvc_->readAnalog) return false;
    uint32_t tsMs = 0U;
    if (ioSvc_->readAnalog(ioSvc_->ctx, ioId, &out, &tsMs, nullptr) != IO_OK) return false;
    if (tsMsOut) *tsMsOut = tsMs;
    return true;
}

bool PoolLogicModule::loadDigitalSensor_(IoId ioId, bool& out) const
{
    if (!ioSvc_ || !ioSvc_->readDigital) return false;
    uint8_t on = 0;
    if (ioSvc_->readDigital(ioSvc_->ctx, ioId, &on, nullptr, nullptr) != IO_OK) return false;
    out = (on != 0U);
    return true;
}

void PoolLogicModule::resetTemporalPidState_(TemporalPidState& st, uint32_t nowMs)
{
    st.initialized = false;
    st.sampleValid = false;
    st.lastDemandOn = false;
    st.windowStartMs = nowMs;
    st.lastComputeMs = nowMs;
    st.sampleTsMs = 0;
    st.outputOnMs = 0;
    st.sampleInput = 0.0f;
    st.sampleSetpoint = 0.0f;
    st.sampleError = 0.0f;
    st.integral = 0.0f;
    st.prevError = 0.0f;
    st.lastError = 0.0f;
    st.runtimeTsMs = nowMs;
}

bool PoolLogicModule::stepTemporalPid_(TemporalPidState& st,
                                       float input,
                                       float setpoint,
                                       float kp,
                                       float ki,
                                       float kd,
                                       int32_t windowMsCfg,
                                       bool positiveWhenInputHigh,
                                       uint32_t nowMs,
                                       bool& demandOnOut,
                                       uint32_t& outputOnMsOut)
{
    // The PID output is converted into a time-on window so peristaltic pumps
    // can be driven with coarse duty-cycle control instead of a raw analog value.
    const uint32_t windowMs = (windowMsCfg > 1000) ? (uint32_t)windowMsCfg : 1000U;
    const uint32_t sampleMs = (pidSampleMs_ > 100) ? (uint32_t)pidSampleMs_ : 100U;
    const uint32_t minOnMs = (pidMinOnMs_ > 0) ? (uint32_t)pidMinOnMs_ : 0U;

    if (!st.initialized) {
        st.initialized = true;
        st.windowStartMs = nowMs;
        st.lastComputeMs = nowMs;
        st.sampleValid = false;
        st.sampleTsMs = 0;
        st.sampleInput = 0.0f;
        st.sampleSetpoint = 0.0f;
        st.sampleError = 0.0f;
        st.integral = 0.0f;
        st.prevError = 0.0f;
        st.lastError = 0.0f;
        st.outputOnMs = 0;
        st.lastDemandOn = false;
        st.runtimeTsMs = nowMs;
    }

    while ((uint32_t)(nowMs - st.windowStartMs) >= windowMs) {
        st.windowStartMs += windowMs;
    }

    if ((uint32_t)(nowMs - st.lastComputeMs) >= sampleMs) {
        const uint32_t dtMs = nowMs - st.lastComputeMs;
        const float dtSec = (dtMs > 0U) ? ((float)dtMs / 1000.0f) : 0.0f;
        st.lastComputeMs = nowMs;

        const float error = positiveWhenInputHigh ? (input - setpoint) : (setpoint - input);
        st.lastError = error;

        float outputMs = 0.0f;
        if (error > 0.0f && std::isfinite(error)) {
            if (ki != 0.0f && dtSec > 0.0f) {
                st.integral += error * dtSec;
            } else {
                st.integral = 0.0f;
            }
            const float deriv = (dtSec > 0.0f) ? ((error - st.prevError) / dtSec) : 0.0f;
            outputMs = (kp * error) + (ki * st.integral) + (kd * deriv);
            if (!std::isfinite(outputMs) || outputMs < 0.0f) outputMs = 0.0f;
            if (outputMs > (float)windowMs) outputMs = (float)windowMs;
        } else {
            st.integral = 0.0f;
            outputMs = 0.0f;
        }
        st.prevError = error;
        st.sampleValid = true;
        st.sampleInput = input;
        st.sampleSetpoint = setpoint;
        st.sampleError = error;
        st.sampleTsMs = nowMs;
        st.runtimeTsMs = nowMs;

        uint32_t outMs = (uint32_t)(outputMs + 0.5f);
        if (outMs < minOnMs) outMs = 0U;
        if (outMs > windowMs) outMs = windowMs;
        st.outputOnMs = outMs;
    }

    const uint32_t elapsedMs = nowMs - st.windowStartMs;
    const bool demandOn = (st.outputOnMs > 0U) && (elapsedMs < st.outputOnMs);
    if (demandOn != st.lastDemandOn) {
        st.lastDemandOn = demandOn;
        st.runtimeTsMs = nowMs;
    }

    demandOnOut = demandOn;
    outputOnMsOut = st.outputOnMs;
    return true;
}

void PoolLogicModule::applyDeviceControl_(uint8_t deviceSlot,
                                          const char* label,
                                          DeviceFsm& fsm,
                                          bool desired,
                                          uint32_t nowMs)
{
    const bool desiredChanged = (desired != fsm.lastDesired);
    // When the actual state does not follow the requested state, retry at a
    // bounded cadence instead of spamming the downstream pool-device service.
    const bool needRetry = (fsm.known && (fsm.on != desired) && (uint32_t)(nowMs - fsm.lastCmdMs) >= 5000U);

    if (desiredChanged || needRetry) {
        if (writeDeviceDesired_(deviceSlot, desired)) {
            LOGI("%s %s", desired ? "Start" : "Stop", label ? label : "Pool Device");
            if (desiredChanged) {
                emitDeviceActivity_(true, desired, deviceSlot, label, ActivityReason::Auto);
            }
        }
        fsm.lastCmdMs = nowMs;
    }

    fsm.lastDesired = desired;
}

void PoolLogicModule::runControlLoop_(uint32_t nowMs)
{
    // The loop always starts by refreshing observed actuator states so all
    // subsequent decisions are based on the latest physical feedback.
    bool filtrationStarted = false;
    bool filtrationStopped = false;
    bool robotStopped = false;
    bool unusedStart = false;
    bool unusedStop = false;

    syncDeviceState_(filtrationDeviceSlot_, filtrationFsm_, nowMs, filtrationStarted, filtrationStopped);
    syncDeviceState_(robotDeviceSlot_, robotFsm_, nowMs, unusedStart, robotStopped);
    syncDeviceState_(swgDeviceSlot_, swgFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(fillingDeviceSlot_, fillingFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(phPumpDeviceSlot_, phPumpFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(orpPumpDeviceSlot_, orpPumpFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(heaterDeviceSlot_, heaterFsm_, nowMs, unusedStart, unusedStop);

    if (filtrationStarted) {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
        resetTemporalPidState_(phPidState_, nowMs);
        resetTemporalPidState_(orpPidState_, nowMs);
    }
    if (filtrationStopped) {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
        resetTemporalPidState_(phPidState_, nowMs);
        resetTemporalPidState_(orpPidState_, nowMs);
        portENTER_CRITICAL(&pendingMux_);
        robotManualOverride_ = false;
        robotManualDesired_ = false;
        portEXIT_CRITICAL(&pendingMux_);
    }
    if (robotStopped) {
        cleaningDone_ = true;
        portENTER_CRITICAL(&pendingMux_);
        if (robotManualOverride_ && !robotManualDesired_) {
            robotManualOverride_ = false;
        }
        portEXIT_CRITICAL(&pendingMux_);
    }

    float psi = 0.0f;
    float ph = 0.0f;
    float waterTemp = 0.0f;
    float airTemp = 0.0f;
    float orp = 0.0f;
    bool poolLevelOn = false;
    bool phTankLow = false;
    bool chlorineTankLow = false;

    const bool havePsi = loadAnalogSensor_(psiIoId_, psi);
    const bool havePh = loadAnalogSensor_(phIoId_, ph);
    uint32_t waterTempTsMs = 0U;
    const bool haveWaterTemp = loadAnalogSensor_(waterTempIoId_, waterTemp, &waterTempTsMs);
    const bool waterTempFresh =
        haveWaterTemp &&
        (waterTempTsMs != 0U) &&
        ((uint32_t)(nowMs - waterTempTsMs) <= kHeaterTempFreshMaxMs);
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    uint32_t airTempTsMs = 0U;
    const bool haveAirTemp = loadAnalogSensor_(airTempIoId_, airTemp, &airTempTsMs);
    const bool airTempFresh =
        haveAirTemp &&
        std::isfinite(airTemp) &&
        (airTempTsMs != 0U) &&
        ((uint32_t)(nowMs - airTempTsMs) <= kHeaterTempFreshMaxMs);
#else
    const bool haveAirTemp = loadAnalogSensor_(airTempIoId_, airTemp);
#endif
    const bool haveOrp = loadAnalogSensor_(orpIoId_, orp);
    const bool haveLevel = loadDigitalSensor_(levelIoId_, poolLevelOn);
    const bool havePhTankLow = loadDigitalSensor_(phLevelIoId_, phTankLow);
    const bool haveChlorineTankLow = loadDigitalSensor_(chlorineLevelIoId_, chlorineTankLow);

    // Prefer centralized alarm state when available; otherwise fall back to a
    // local safety latch so standalone behavior remains conservative.
    if (alarmSvc_ && alarmSvc_->isActive) {
        const bool psiLow = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolPsiLow);
        const bool psiHigh = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolPsiHigh);
        const bool phTankLowAlarm = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolPhTankLow);
        const bool chlorineTankLowAlarm = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolChlorineTankLow);
        psiError_ = psiLow || psiHigh;
        phTankLowError_ = phTankLowAlarm;
        chlorineTankLowError_ = chlorineTankLowAlarm;
    } else {
        phTankLowError_ = havePhTankLow && phTankLow;
        chlorineTankLowError_ = haveChlorineTankLow && chlorineTankLow;
        if (filtrationFsm_.on && havePsi) {
            const uint32_t runSec = stateUptimeSec_(filtrationFsm_, nowMs);
            const bool underPressure = (runSec > psiStartupDelaySec_) && (psi < psiLowThreshold_);
            const bool overPressure = (psi > psiHighThreshold_);
            if ((underPressure || overPressure) && !psiError_) {
                psiError_ = true;
                LOGW("PSI error latched (psi=%.3f low=%.3f high=%.3f)",
                     (double)psi,
                     (double)psiLowThreshold_,
                     (double)psiHighThreshold_);
                char detail[128] = {0};
                snprintf(detail,
                         sizeof(detail),
                         "Pression %.3f bar hors plage %.3f-%.3f bar.",
                         (double)psi,
                         (double)psiLowThreshold_,
                         (double)psiHighThreshold_);
                emitActivity_(ActivityCode::PoolLogicSafetyPressureLatched,
                              ActivitySource::Safety,
                              ActivitySeverity::Warning,
                              ActivityRole::Filtration,
                              ActivityState::None,
                              ActivityReason::Safety,
                              filtrationDeviceSlot_,
                              "Sécurité pression piscine activée",
                              detail,
                              "warning");
            }
        }
    }

    // PID regulation is armed only after filtration has been stable long enough
    // to avoid reacting to startup transients.
    if (filtrationFsm_.on && !winterMode_) {
        const uint32_t runMin = stateUptimeSec_(filtrationFsm_, nowMs) / 60U;

        if (phAutoMode_ && !phPidEnabled_ && runMin >= delayPidsMin_) {
            phPidEnabled_ = true;
            LOGI("Activate pH regulation (delay=%umin)", (unsigned)runMin);
            emitActivity_(ActivityCode::PoolLogicPhRegulationEnabled,
                          ActivitySource::Pid,
                          ActivitySeverity::Info,
                          ActivityRole::Ph,
                          ActivityState::None,
                          ActivityReason::Pid,
                          phPumpDeviceSlot_,
                          "Régulation pH activée",
                          "La filtration est stable, le PID pH peut piloter la pompe.",
                          "science");
        }
        if (orpAutoMode_ && isDisinfectionType_(DisinfectionChlorineBromine) &&
            !orpPidEnabled_ && runMin >= delayPidsMin_) {
            orpPidEnabled_ = true;
            LOGI("Activate ORP regulation (delay=%umin)", (unsigned)runMin);
            emitActivity_(ActivityCode::PoolLogicOrpRegulationEnabled,
                          ActivitySource::Pid,
                          ActivitySeverity::Info,
                          ActivityRole::Disinfection,
                          ActivityState::None,
                          ActivityReason::Pid,
                          orpPumpDeviceSlot_,
                          "Régulation ORP activée",
                          "La filtration est stable, le PID ORP peut piloter la désinfection.",
                          "science");
        }
    } else {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
    }
    // Manual forcing relies on *_auto_mode=false. In that case, PID must not
    // keep a stale enabled state that could override manual requests.
    if (!phAutoMode_ && phPidEnabled_) {
        phPidEnabled_ = false;
    }
    if (!orpAutoMode_ && orpPidEnabled_) {
        orpPidEnabled_ = false;
    }

    bool windowActive = false;
    bool forceFiltrationReconcile = false;
    portENTER_CRITICAL(&pendingMux_);
    windowActive = filtrationWindowActive_;
    forceFiltrationReconcile = pendingFiltrationReconcile_;
    pendingFiltrationReconcile_ = false;
    portEXIT_CRITICAL(&pendingMux_);

    bool clockWindowActive = false;
    if (currentFiltrationWindowActive_(filtrationCalcStart_, filtrationCalcStop_, clockWindowActive)) {
        if (clockWindowActive != windowActive || forceFiltrationReconcile) {
            windowActive = clockWindowActive;
            portENTER_CRITICAL(&pendingMux_);
            filtrationWindowActive_ = windowActive;
            portEXIT_CRITICAL(&pendingMux_);
        }
    } else if (forceFiltrationReconcile && schedSvc_ && schedSvc_->isActive) {
        windowActive = schedSvc_->isActive(schedSvc_->ctx, SLOT_FILTR_WINDOW);
        portENTER_CRITICAL(&pendingMux_);
        filtrationWindowActive_ = windowActive;
        portEXIT_CRITICAL(&pendingMux_);
    }

    auto hasHeatAssistFlag = [&](uint8_t flag) -> bool {
        return (heatAssistFlags_ & flag) != 0U;
    };

    auto setHeatAssistFlag = [&](uint8_t flag, bool enabled) {
        if (enabled) heatAssistFlags_ = (uint8_t)(heatAssistFlags_ | flag);
        else heatAssistFlags_ = (uint8_t)(heatAssistFlags_ & (uint8_t)(~flag));
    };

    auto setHeatAssistReason = [&](HeatAssistReason reason) {
        heatAssistReason_ = reason;
    };

    auto resetHeatAssistSession = [&]() {
        setHeatAssistFlag(kHeatAssistFlagProbeRunning, false);
        setHeatAssistFlag(kHeatAssistFlagHeatingActive, false);
        heatAssistTimingPacked_ = (heatAssistTimingPacked_ & 0xFFFF0000UL);
    };

    const uint16_t nowSec = (uint16_t)((nowMs / 1000UL) & 0xFFFFU);
    auto getProbeStartSec = [&]() -> uint16_t {
        return (uint16_t)(heatAssistTimingPacked_ & 0xFFFFU);
    };
    auto getLastProbeEndSec = [&]() -> uint16_t {
        return (uint16_t)((heatAssistTimingPacked_ >> 16) & 0xFFFFU);
    };
    auto setProbeStartSec = [&](uint16_t sec) {
        heatAssistTimingPacked_ = (heatAssistTimingPacked_ & 0xFFFF0000UL) | (uint32_t)sec;
    };
    auto setLastProbeEndSec = [&](uint16_t sec) {
        heatAssistTimingPacked_ = (heatAssistTimingPacked_ & 0x0000FFFFUL) | (((uint32_t)sec) << 16);
    };
    auto elapsedSecSince = [&](uint16_t pastSec) -> uint16_t {
        return (uint16_t)(nowSec - pastSec);
    };

#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    auto refreshHeatAssistAdaptiveInterval = [&]() {
        const bool wasValid = heatAssistAdaptiveInputsValid_;
        const uint16_t previousIntervalMin = heatAssistIntervalMin_;
        if (!heatAssistValidatedWaterTempValid_ ||
            !std::isfinite(heatAssistValidatedWaterTempC_) ||
            !airTempFresh) {
            heatAssistAdaptiveInputsValid_ = false;
            heatAssistIntervalMin_ = kHeatAssistAdaptiveFallbackMin;
            if (wasValid || previousIntervalMin != heatAssistIntervalMin_) {
                LOGD("Heat Assist adaptive fallback interval=%u min water_valid=%u air_fresh=%u",
                     (unsigned)heatAssistIntervalMin_,
                     heatAssistValidatedWaterTempValid_ ? 1u : 0u,
                     airTempFresh ? 1u : 0u);
            }
            return;
        }

        heatAssistAirTempC_ = airTemp;
        heatAssistDeltaC_ = heatAssistValidatedWaterTempC_ - heatAssistAirTempC_;
        if (heatAssistDeltaC_ < 0.0f) heatAssistDeltaC_ = 0.0f;

        if (heatAssistDeltaC_ <= kHeatAssistAdaptiveLowDeltaC) {
            heatAssistIntervalMin_ = kHeatAssistAdaptiveMaxIntervalMin;
        } else if (heatAssistDeltaC_ >= kHeatAssistAdaptiveHighDeltaC) {
            heatAssistIntervalMin_ = kHeatAssistAdaptiveMinIntervalMin;
        } else {
            const float intervalMin =
                (float)kHeatAssistAdaptiveMaxIntervalMin -
                (heatAssistDeltaC_ - kHeatAssistAdaptiveLowDeltaC) * 4.0f;
            heatAssistIntervalMin_ = (uint16_t)lroundf(intervalMin);
        }
        heatAssistAdaptiveInputsValid_ = true;
        if (!wasValid || previousIntervalMin != heatAssistIntervalMin_) {
            LOGD("Heat Assist adaptive interval water=%.2fC air=%.2fC delta=%.2fC interval=%u min",
                 (double)heatAssistValidatedWaterTempC_,
                 (double)heatAssistAirTempC_,
                 (double)heatAssistDeltaC_,
                 (unsigned)heatAssistIntervalMin_);
        }
    };

    auto recordValidatedWaterTemperature = [&]() -> bool {
        if (!waterTempFresh || !std::isfinite(waterTemp)) return false;
        heatAssistValidatedWaterTempC_ = waterTemp;
        heatAssistValidatedWaterTempValid_ = true;
        refreshHeatAssistAdaptiveInterval();
        return true;
    };

    // A regular filtration run also provides a trustworthy basin temperature.
    // Do not retain the line temperature before water has circulated for 5 minutes.
    const bool recordedUnderFlow =
        heaterAutoMode_ &&
        filtrationFsm_.on &&
        stateUptimeSec_(filtrationFsm_, nowMs) >= kHeatAssistProbeRunSec &&
        recordValidatedWaterTemperature();
    if (recordedUnderFlow) {
        // While normal filtration or heating is already circulating water, the
        // latest trustworthy reading is also the origin of the next interval.
        setLastProbeEndSec(nowSec);
    } else {
        // Keep the cadence responsive to outdoor-temperature changes while the
        // pump is stopped, using only the last water value validated under flow.
        refreshHeatAssistAdaptiveInterval();
    }
#endif

    // Filtration arbitration intentionally applies safety, then manual mode,
    // then automatic scheduling/winter logic in that order.
    bool filtrationDesiredBase = filtrationFsm_.on;
    if (psiError_) {
        // Safety first: PSI alarms must stop filtration even in manual mode.
        filtrationDesiredBase = false;
    } else if (!autoMode_) {
        // Legacy-like manual mode: when auto_mode is off, keep filtration fully manual.
        filtrationDesiredBase = filtrationFsm_.on;
    } else {
        const bool timeSynced = timeSvc_ && timeSvc_->isSynced && timeSvc_->isSynced(timeSvc_->ctx);
        if (filtrationFsm_.on && !timeSynced) {
            // During a warm reboot, keep a retained pump running until the
            // scheduler can make a reliable clock-based decision.
            filtrationDesiredBase = true;
        } else if (filtrationFsm_.on && haveAirTemp && airTemp <= freezeHoldTempC_) {
            // Freeze hold: once running, never stop under freeze-hold threshold.
            filtrationDesiredBase = true;
        } else {
            const bool scheduleDemand = windowActive;
            const bool winterDemand = winterMode_ && haveAirTemp && (airTemp < winterStartTempC_);
            filtrationDesiredBase = (scheduleDemand || winterDemand);
        }
    }
    bool filtrationDesired = filtrationDesiredBase;

    // Robot and SWG remain derived outputs in auto mode. A manual robot request
    // is still arbitrated here so PoolLogic, not the HMI or PoolDeviceModule,
    // owns interlocks and duration limits.
    bool robotDesired = robotFsm_.on;
    bool robotManualOverride = false;
    bool robotManualDesired = false;
    portENTER_CRITICAL(&pendingMux_);
    robotManualOverride = robotManualOverride_;
    robotManualDesired = robotManualDesired_;
    portEXIT_CRITICAL(&pendingMux_);
    if (autoMode_) {
        robotDesired = false;
        if (filtrationFsm_.on && !cleaningDone_) {
            const uint32_t filtrationRunMin = stateUptimeSec_(filtrationFsm_, nowMs) / 60U;
            if (filtrationRunMin >= robotDelayMin_) robotDesired = true;
        }
        if (robotFsm_.on) {
            const uint32_t robotRunMin = stateUptimeSec_(robotFsm_, nowMs) / 60U;
            if (robotRunMin >= robotDurationMin_) robotDesired = false;
        }
        if (!filtrationFsm_.on) robotDesired = false;
    }
    if (robotManualOverride) {
        if (!filtrationFsm_.on) {
            robotDesired = false;
            portENTER_CRITICAL(&pendingMux_);
            robotManualOverride_ = false;
            robotManualDesired_ = false;
            portEXIT_CRITICAL(&pendingMux_);
        } else {
            robotDesired = robotManualDesired;
            if (robotManualDesired && robotFsm_.on) {
                const uint32_t robotRunMin = stateUptimeSec_(robotFsm_, nowMs) / 60U;
                if (robotRunMin >= robotDurationMin_) {
                    robotDesired = false;
                    cleaningDone_ = true;
                    portENTER_CRITICAL(&pendingMux_);
                    robotManualOverride_ = false;
                    robotManualDesired_ = false;
                    portEXIT_CRITICAL(&pendingMux_);
                }
            }
            if (!robotManualDesired && !robotFsm_.on) {
                portENTER_CRITICAL(&pendingMux_);
                robotManualOverride_ = false;
                portEXIT_CRITICAL(&pendingMux_);
            }
        }
    }

    bool swgDesired = swgFsm_.on;
    if (autoMode_) {
        swgDesired = false;
        if (isDisinfectionType_(DisinfectionSwg) && filtrationFsm_.on) {
            if (swgControlMode_ == SwgControlOrp) {
                if (swgFsm_.on) {
                    swgDesired = haveOrp && (orp <= orpSetpoint_);
                } else {
                    const bool startReady =
                        haveWaterTemp &&
                        (waterTemp >= secureElectroTempC_) &&
                        ((stateUptimeSec_(filtrationFsm_, nowMs) / 60U) >= delayElectroMin_);
                    swgDesired = startReady && haveOrp && (orp <= (orpSetpoint_ * 0.9f));
                }
            } else {
                if (swgFsm_.on) {
                    swgDesired = true;
                } else {
                    const bool startReady =
                        haveWaterTemp &&
                        (waterTemp >= secureElectroTempC_) &&
                        ((stateUptimeSec_(filtrationFsm_, nowMs) / 60U) >= delayElectroMin_);
                    swgDesired = startReady;
                }
            }
        }
    }

    bool heaterDesired = heaterFsm_.on;
    if (!heaterAutoMode_) {
        resetHeatAssistSession();
        setHeatAssistFlag(kHeatAssistFlagFastCycle, false);
        setLastProbeEndSec(0U);
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
        heatAssistValidatedWaterTempValid_ = false;
        heatAssistAdaptiveInputsValid_ = false;
        heatAssistIntervalMin_ = kHeatAssistAdaptiveFallbackMin;
#endif
        setHeatAssistReason(HeatAssistReason::Disabled);
    } else if (!autoMode_) {
        resetHeatAssistSession();
        setHeatAssistReason(HeatAssistReason::ManualMode);
    } else if (psiError_) {
        resetHeatAssistSession();
        heaterDesired = false;
        setHeatAssistReason(HeatAssistReason::PsiBlocked);
    } else if (!std::isfinite(heaterSetpoint_)) {
        resetHeatAssistSession();
        heaterDesired = false;
        setHeatAssistReason(HeatAssistReason::SetpointInvalid);
    } else {
        const float heaterStartThreshold = heaterSetpoint_ - kHeaterHysteresisC;
        const float heaterStopThreshold = heaterSetpoint_ + kHeaterHysteresisC;
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
        const bool heatAssistWaterTempAvailable = waterTempFresh && std::isfinite(waterTemp);
        // The requested interval is measurement-to-measurement. Start the pump
        // five minutes earlier so its validation run is included in that period.
        const uint16_t measurementIntervalSec = (uint16_t)(heatAssistIntervalMin_ * 60U);
        const uint16_t idleSec =
            (measurementIntervalSec > kHeatAssistProbeRunSec)
                ? (uint16_t)(measurementIntervalSec - kHeatAssistProbeRunSec)
                : 0U;
#else
        const bool heatAssistWaterTempAvailable = waterTempFresh;
        const bool fastCycle = hasHeatAssistFlag(kHeatAssistFlagFastCycle);
        const uint16_t idleSec = fastCycle ? kHeatAssistIdleFastSec : kHeatAssistIdleSlowSec;
#endif

        auto setProbeWaitReason = [&]() {
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
            setHeatAssistReason(HeatAssistReason::ProbeWaitAdaptive);
#else
            setHeatAssistReason(fastCycle ? HeatAssistReason::ProbeWait20m
                                          : HeatAssistReason::ProbeWait30m);
#endif
        };

        auto startProbe = [&]() {
            setHeatAssistFlag(kHeatAssistFlagProbeRunning, true);
            setHeatAssistFlag(kHeatAssistFlagHeatingActive, false);
            setProbeStartSec(nowSec);
            filtrationDesired = true;
            heaterDesired = false;
            setHeatAssistReason(HeatAssistReason::ProbeRunning);
        };

        if (hasHeatAssistFlag(kHeatAssistFlagHeatingActive)) {
            filtrationDesired = true;
            if (!heatAssistWaterTempAvailable) {
                heaterDesired = false;
                setHeatAssistReason(HeatAssistReason::TempUnavailable);
            } else if (waterTemp >= heaterStopThreshold) {
                setHeatAssistFlag(kHeatAssistFlagHeatingActive, false);
                setHeatAssistFlag(kHeatAssistFlagProbeRunning, false);
                setProbeStartSec(0U);
                setLastProbeEndSec(nowSec);
#if !defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                setHeatAssistFlag(kHeatAssistFlagFastCycle, true);
#endif
                heaterDesired = false;
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                // Heating may reach its setpoint while the daily filtration
                // window is still active. In that case, keep filtration running.
                filtrationDesired = filtrationDesiredBase;
                LOGD("Heat Assist setpoint reached water=%.2fC setpoint=%.2fC filtration_window=%u",
                     (double)waterTemp,
                     (double)heaterSetpoint_,
                     filtrationDesiredBase ? 1u : 0u);
#else
                filtrationDesired = false;
#endif
                setHeatAssistReason(HeatAssistReason::SetpointReached);
            } else {
                heaterDesired = true;
                setHeatAssistReason(HeatAssistReason::Heating);
            }
        } else if (hasHeatAssistFlag(kHeatAssistFlagProbeRunning)) {
            filtrationDesired = true;
            heaterDesired = false;
            const uint16_t probeElapsedSec = elapsedSecSince(getProbeStartSec());
            if (probeElapsedSec >= kHeatAssistProbeRunSec) {
                setHeatAssistFlag(kHeatAssistFlagProbeRunning, false);
                setProbeStartSec(0U);
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                (void)recordValidatedWaterTemperature();
#endif
                if (!heatAssistWaterTempAvailable) {
                    filtrationDesired = filtrationDesiredBase;
                    setLastProbeEndSec(nowSec);
                    setHeatAssistReason(HeatAssistReason::TempUnavailable);
                } else if (waterTemp <= heaterStartThreshold) {
                    setHeatAssistFlag(kHeatAssistFlagHeatingActive, true);
                    filtrationDesired = true;
                    heaterDesired = true;
                    setHeatAssistReason(HeatAssistReason::Heating);
                } else {
                    setLastProbeEndSec(nowSec);
                    filtrationDesired = filtrationDesiredBase;
                    setProbeWaitReason();
                }
            } else {
                setHeatAssistReason(HeatAssistReason::ProbeRunning);
            }
        } else if (filtrationFsm_.on || filtrationDesiredBase) {
            filtrationDesired = filtrationDesiredBase;
            if (!heatAssistWaterTempAvailable) {
                heaterDesired = false;
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                if (filtrationFsm_.on && !filtrationDesiredBase) {
                    // The daily run is ending without a usable final sample.
                    // Anchor the fallback interval now to prevent relay chatter.
                    setLastProbeEndSec(nowSec);
                    if (!heatAssistValidatedWaterTempValid_) {
                        heatAssistAdaptiveInputsValid_ = false;
                        heatAssistIntervalMin_ = kHeatAssistAdaptiveFallbackMin;
                    }
                    if (filtrationFsm_.lastDesired) {
                        LOGD("Heat Assist filtration end temp unavailable; retry interval=%u min",
                             (unsigned)heatAssistIntervalMin_);
                    }
                }
#endif
                setHeatAssistReason(HeatAssistReason::TempUnavailable);
            } else {
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                if (!recordedUnderFlow) {
                    // A newly-started daily filtration has not circulated long
                    // enough yet. Keep the heater off until its water sample is valid.
                    heaterDesired = false;
                    setHeatAssistReason(HeatAssistReason::IdlePumpOn);
                } else {
#endif
                    if (heaterFsm_.on) {
                        heaterDesired = (waterTemp < heaterStopThreshold);
                    } else {
                        heaterDesired = (waterTemp <= heaterStartThreshold);
                    }
                    if (heaterDesired) {
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                        // The water is already validated under flow: hand over
                        // directly to heating without stopping and restarting the pump.
                        setHeatAssistFlag(kHeatAssistFlagHeatingActive, true);
                        filtrationDesired = true;
                        LOGD("Heat Assist filtration-to-heating handoff water=%.2fC setpoint=%.2fC",
                             (double)waterTemp,
                             (double)heaterSetpoint_);
#endif
                        setHeatAssistReason(HeatAssistReason::Heating);
                    } else {
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                        if (filtrationFsm_.on && !filtrationDesiredBase && filtrationFsm_.lastDesired) {
                            LOGD("Heat Assist filtration end no heat water=%.2fC next_interval=%u min",
                                 (double)waterTemp,
                                 (unsigned)heatAssistIntervalMin_);
                        }
#endif
                        setHeatAssistReason(HeatAssistReason::IdlePumpOn);
                    }
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                }
#endif
            }
        } else {
            heaterDesired = false;
            filtrationDesired = false;
            const uint16_t lastProbeEndSec = getLastProbeEndSec();
            const bool dueForProbe =
                (lastProbeEndSec == 0U) || (elapsedSecSince(lastProbeEndSec) >= idleSec);
            if (dueForProbe) {
                startProbe();
            } else {
                setProbeWaitReason();
            }
        }
    }

    // Chemical dosing is computed last because it depends on the resolved
    // filtration state, alarm state, and sensor freshness.
    bool phPumpDesired = phPumpFsm_.on;
    bool orpPumpDesired = orpPumpFsm_.on;
    if (phAutoMode_ || orpAutoMode_) {
        if (filtrationDesired) {
            if (phAutoMode_) {
                const bool phAllowed = phPidEnabled_ && havePh && !psiError_ && !phTankLowError_;
                if (phAllowed) {
                    uint32_t outMs = 0;
                    (void)stepTemporalPid_(phPidState_,
                                           ph,
                                           phSetpoint_,
                                           phKp_,
                                           phKi_,
                                           phKd_,
                                           phWindowMs_,
                                           !phDosePlus_,
                                           nowMs,
                                           phPumpDesired,
                                           outMs);
                } else if (phPidState_.initialized || phPidState_.outputOnMs != 0U || phPidState_.lastDemandOn) {
                    resetTemporalPidState_(phPidState_, nowMs);
                }
            } else if (phPidState_.initialized || phPidState_.outputOnMs != 0U || phPidState_.lastDemandOn) {
                resetTemporalPidState_(phPidState_, nowMs);
            }

            if (orpAutoMode_) {
                const bool orpAllowed =
                    orpPidEnabled_ && haveOrp && isDisinfectionType_(DisinfectionChlorineBromine) &&
                    !psiError_ && !chlorineTankLowError_;
                if (orpAllowed) {
                    uint32_t outMs = 0;
                    (void)stepTemporalPid_(orpPidState_,
                                           orp,
                                           orpSetpoint_,
                                           orpKp_,
                                           orpKi_,
                                           orpKd_,
                                           orpWindowMs_,
                                           false,
                                           nowMs,
                                           orpPumpDesired,
                                           outMs);
                } else if (orpPidState_.initialized || orpPidState_.outputOnMs != 0U || orpPidState_.lastDemandOn) {
                    resetTemporalPidState_(orpPidState_, nowMs);
                }
            } else if (orpPidState_.initialized || orpPidState_.outputOnMs != 0U || orpPidState_.lastDemandOn) {
                resetTemporalPidState_(orpPidState_, nowMs);
            }
        } else {
            if (phAutoMode_ && (phPidState_.initialized || phPidState_.outputOnMs != 0U || phPidState_.lastDemandOn)) {
                resetTemporalPidState_(phPidState_, nowMs);
            }
            if (orpAutoMode_ &&
                (orpPidState_.initialized || orpPidState_.outputOnMs != 0U || orpPidState_.lastDemandOn)) {
                resetTemporalPidState_(orpPidState_, nowMs);
            }
        }
    } else {
        if (phPidState_.initialized || phPidState_.outputOnMs != 0U || phPidState_.lastDemandOn) {
            resetTemporalPidState_(phPidState_, nowMs);
        }
        if (orpPidState_.initialized || orpPidState_.outputOnMs != 0U || orpPidState_.lastDemandOn) {
            resetTemporalPidState_(orpPidState_, nowMs);
        }
    }

    bool o2RequestFiltration = false;
    bool o2PumpDesired = false;
    (void)stepO2Protocol_(filtrationDesired,
                          filtrationFsm_.on,
                          stateUptimeSec_(filtrationFsm_, nowMs) / 60U,
                          haveWaterTemp,
                          waterTemp,
                          psiError_,
                          chlorineTankLowError_,
                          nowMs,
                          o2RequestFiltration,
                          o2PumpDesired);
    if (o2RequestFiltration && !psiError_) {
        filtrationDesired = true;
    }
    if (isDisinfectionType_(DisinfectionActiveOxygen)) {
        orpPumpDesired = o2PumpDesired;
    }

    bool fillingDesired = false;
    if (haveLevel) {
        if (!fillingFsm_.on) {
            fillingDesired = poolLevelOn;
        } else {
            const bool minUpReached = stateUptimeSec_(fillingFsm_, nowMs) >= fillingMinOnSec_;
            fillingDesired = poolLevelOn || !minUpReached;
        }
    }

    if (forceFiltrationReconcile) {
        // Auto mode changes arrive through ConfigChanged, so force one immediate
        // reconciliation in the loop regardless of the previous manual path.
        filtrationFsm_.lastDesired = !filtrationDesired;
        filtrationFsm_.lastCmdMs = 0U;
    }
    applyDeviceControl_(filtrationDeviceSlot_, "Filtration Pump", filtrationFsm_, filtrationDesired, nowMs);
    applyDeviceControl_(phPumpDeviceSlot_, "pH Pump", phPumpFsm_, phPumpDesired, nowMs);
    applyDeviceControl_(orpPumpDeviceSlot_,
                        isDisinfectionType_(DisinfectionActiveOxygen) ? "O2 Pump" : "Chlorine Pump",
                        orpPumpFsm_,
                        orpPumpDesired,
                        nowMs);
    applyDeviceControl_(robotDeviceSlot_, "Robot Pump", robotFsm_, robotDesired, nowMs);
    applyDeviceControl_(swgDeviceSlot_, "SWG Pump", swgFsm_, swgDesired, nowMs);
    applyDeviceControl_(heaterDeviceSlot_, "Water Heater", heaterFsm_, heaterDesired, nowMs);
    applyDeviceControl_(fillingDeviceSlot_, "Filling Pump", fillingFsm_, fillingDesired, nowMs);
}
