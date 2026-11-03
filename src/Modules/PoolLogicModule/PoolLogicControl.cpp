/**
 * @file PoolLogicControl.cpp
 * @brief Control loop, device state, and alarm conditions for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"

#include <cmath>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

namespace {
constexpr float kHeaterHysteresisC = 0.3f;
constexpr uint32_t kHeaterTempFreshMaxMs = 10UL * 60UL * 1000UL;
constexpr uint16_t kHeatAssistProbeRunSec = 5U * 60U;
constexpr uint16_t kHeatAssistIdleSlowSec = 30U * 60U;
constexpr uint16_t kHeatAssistIdleFastSec = 20U * 60U;
constexpr uint8_t kHeatAssistFlagProbeRunning = (1U << 0);
constexpr uint8_t kHeatAssistFlagHeatingActive = (1U << 1);
constexpr uint8_t kHeatAssistFlagFastCycle = (1U << 2);

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
        default: return "unknown";
    }
}
}  // namespace

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
    }
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
    }
    if (robotStopped) {
        cleaningDone_ = true;
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
    const bool haveAirTemp = loadAnalogSensor_(airTempIoId_, airTemp);
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
        }
        if (orpAutoMode_ && !orpPidEnabled_ && runMin >= delayPidsMin_) {
            orpPidEnabled_ = true;
            LOGI("Activate ORP regulation (delay=%umin)", (unsigned)runMin);
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
    if (forceFiltrationReconcile && schedSvc_ && schedSvc_->isActive) {
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

    const bool winterCycleDemand = winterCycle_.update(
        nowMs, autoMode_ && winterMode_, winterDurationMin_, winterIntervalHours_);
    const bool filtrationDesiredBase = poolFiltrationDemand(
        psiError_, autoMode_, filtrationFsm_.on, windowActive, winterMode_,
        winterCycleDemand, haveAirTemp, airTemp, winterStartTempC_, freezeHoldTempC_);
    bool filtrationDesired = filtrationDesiredBase;

    // Robot and SWG remain pure derived outputs in auto mode; manual mode keeps
    // the current state untouched unless another safety rule overrides it.
    bool robotDesired = robotFsm_.on;
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

    bool swgDesired = swgFsm_.on;
    if (autoMode_) {
        swgDesired = false;
        if (electrolyseMode_ && filtrationFsm_.on) {
            if (electroRunMode_) {
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
        const bool fastCycle = hasHeatAssistFlag(kHeatAssistFlagFastCycle);
        const uint16_t idleSec = fastCycle ? kHeatAssistIdleFastSec : kHeatAssistIdleSlowSec;

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
            if (!waterTempFresh) {
                heaterDesired = false;
                setHeatAssistReason(HeatAssistReason::TempUnavailable);
            } else if (waterTemp >= heaterStopThreshold) {
                setHeatAssistFlag(kHeatAssistFlagHeatingActive, false);
                setHeatAssistFlag(kHeatAssistFlagProbeRunning, false);
                setProbeStartSec(0U);
                setLastProbeEndSec(nowSec);
                setHeatAssistFlag(kHeatAssistFlagFastCycle, true);
                heaterDesired = false;
                // Ending heating must not cancel winter filtration or freeze protection.
                filtrationDesired = filtrationDesiredBase;
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
                if (!waterTempFresh) {
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
                    setHeatAssistReason(fastCycle ? HeatAssistReason::ProbeWait20m
                                                  : HeatAssistReason::ProbeWait30m);
                }
            } else {
                setHeatAssistReason(HeatAssistReason::ProbeRunning);
            }
        } else if (filtrationFsm_.on || filtrationDesiredBase) {
            filtrationDesired = filtrationDesiredBase;
            if (!waterTempFresh) {
                heaterDesired = false;
                setHeatAssistReason(HeatAssistReason::TempUnavailable);
            } else {
                if (heaterFsm_.on) {
                    heaterDesired = (waterTemp < heaterStopThreshold);
                } else {
                    heaterDesired = (waterTemp <= heaterStartThreshold);
                }
                setHeatAssistReason(heaterDesired ? HeatAssistReason::Heating
                                                  : HeatAssistReason::IdlePumpOn);
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
                setHeatAssistReason(fastCycle ? HeatAssistReason::ProbeWait20m
                                              : HeatAssistReason::ProbeWait30m);
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
                // ORP peristaltic dosing is disabled when electrolyse mode is active.
                const bool orpAllowed =
                    orpPidEnabled_ && haveOrp && !electrolyseMode_ && !psiError_ && !chlorineTankLowError_;
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
    applyDeviceControl_(orpPumpDeviceSlot_, "Chlorine Pump", orpPumpFsm_, orpPumpDesired, nowMs);
    applyDeviceControl_(robotDeviceSlot_, "Robot Pump", robotFsm_, robotDesired, nowMs);
    applyDeviceControl_(swgDeviceSlot_, "SWG Pump", swgFsm_, swgDesired, nowMs);
    applyDeviceControl_(heaterDeviceSlot_, "Water Heater", heaterFsm_, heaterDesired, nowMs);
    applyDeviceControl_(fillingDeviceSlot_, "Filling Pump", fillingFsm_, fillingDesired, nowMs);
}
