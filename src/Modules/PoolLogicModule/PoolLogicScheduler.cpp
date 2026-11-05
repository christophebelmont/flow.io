/**
 * @file PoolLogicScheduler.cpp
 * @brief Scheduler and filtration window logic for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Modules/PoolLogicModule/FiltrationWindow.h"

#include <cstring>
#include <math.h>
#include <time.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

void PoolLogicModule::ensureDailySlot_()
{
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("time.scheduler service unavailable");
        return;
    }

    // The daily recompute slot is the long-lived scheduler anchor; it does not
    // directly change outputs, it only asks the loop to rebuild the window.
    TimeSchedulerSlot recalc{};
    recalc.slot = SLOT_DAILY_RECALC;
    recalc.eventId = POOLLOGIC_EVENT_DAILY_RECALC;
    recalc.enabled = true;
    recalc.hasEnd = false;
    recalc.replayStartOnBoot = false;
    recalc.mode = TimeSchedulerMode::RecurringClock;
    recalc.weekdayMask = TIME_WEEKDAY_ALL;
    recalc.startHour = PoolDefaults::FiltrationPivotHour;
    recalc.startMinute = 0;
    recalc.endHour = 0;
    recalc.endMinute = 0;
    recalc.startEpochSec = 0;
    recalc.endEpochSec = 0;
    strncpy(recalc.label, "poollogic_daily_recalc", sizeof(recalc.label) - 1);
    recalc.label[sizeof(recalc.label) - 1] = '\0';

    if (!schedSvc_->setSlot(schedSvc_->ctx, &recalc)) {
        LOGW("Failed to set scheduler slot %u", (unsigned)SLOT_DAILY_RECALC);
    }
}

bool PoolLogicModule::computeFiltrationWindow_(float waterTemp, uint8_t& startHourOut, uint8_t& stopHourOut, uint8_t& durationOut)
{
    // The actual deterministic formula stays isolated in FiltrationWindow.cpp;
    // this method only adapts module config into that pure helper.
    FiltrationWindowInput in{};
    in.waterTemp = waterTemp;
    in.lowThreshold = waterTempLowThreshold_;
    in.setpoint = waterTempSetpoint_;
    in.startMinHour = filtrationStartMin_;
    in.stopMaxHour = filtrationStopMax_;

    FiltrationWindowOutput out{};
    if (!computeFiltrationWindowDeterministic(in, out)) return false;
    startHourOut = out.startHour;
    stopHourOut = out.stopHour;
    durationOut = out.durationHours;
    return true;
}

bool PoolLogicModule::currentFiltrationWindowActive_(uint8_t startHour, uint8_t stopHour, bool& activeOut) const
{
    if (!timeSvc_ || !timeSvc_->isSynced || !timeSvc_->epoch) return false;
    if (!timeSvc_->isSynced(timeSvc_->ctx)) return false;

    const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
    if (epoch < 1609459200ULL) return false;

    const time_t now = (time_t)epoch;
    struct tm localNow {};
    if (!localtime_r(&now, &localNow)) return false;

    const uint16_t minuteOfDay = (uint16_t)((localNow.tm_hour * 60) + localNow.tm_min);
    activeOut = isFiltrationWindowActiveAtMinute(startHour, stopHour, minuteOfDay);
    return true;
}

bool PoolLogicModule::applyFiltrationWindowSlot_(uint8_t startHour, uint8_t stopHour)
{
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("No time.scheduler service available");
        return false;
    }

    TimeSchedulerSlot window{};
    window.slot = SLOT_FILTR_WINDOW;
    window.eventId = POOLLOGIC_EVENT_FILTRATION_WINDOW;
    window.enabled = true;
    window.hasEnd = true;
    window.replayStartOnBoot = true;
    window.mode = TimeSchedulerMode::RecurringClock;
    window.weekdayMask = TIME_WEEKDAY_ALL;
    window.startHour = (startHour < 24U) ? startHour : 23U;
    window.startMinute = 0;
    window.endHour = (stopHour < 24U) ? stopHour : 23U;
    window.endMinute = 0;
    window.startEpochSec = 0;
    window.endEpochSec = 0;
    strncpy(window.label, "poollogic_filtration", sizeof(window.label) - 1);
    window.label[sizeof(window.label) - 1] = '\0';

    if (!schedSvc_->setSlot(schedSvc_->ctx, &window)) {
        LOGW("Failed to set filtration window slot=%u", (unsigned)SLOT_FILTR_WINDOW);
        return false;
    }

    bool windowActive = filtrationWindowActive_;
    if (currentFiltrationWindowActive_(startHour, stopHour, windowActive)) {
        // Keep PoolLogic deterministic during the short gap after setSlot(),
        // before TimeModule has rebuilt its active mask.
    } else if (schedSvc_->isActive) {
        windowActive = schedSvc_->isActive(schedSvc_->ctx, SLOT_FILTR_WINDOW);
    }

    portENTER_CRITICAL(&pendingMux_);
    filtrationWindowActive_ = windowActive;
    pendingFiltrationReconcile_ = true;
    portEXIT_CRITICAL(&pendingMux_);
    return true;
}

bool PoolLogicModule::recalcAndApplyFiltrationWindow_(uint8_t* startHourOut,
                                                      uint8_t* stopHourOut,
                                                      uint8_t* durationOut)
{
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("No time.scheduler service available");
        return false;
    }

    float waterTemp = NAN;
    bool hasWaterTemp = false;
    if (ioSvc_ && ioSvc_->readAnalog) {
        hasWaterTemp = loadAnalogSensor_(waterTempIoId_, waterTemp);
    }
    if (!ioSvc_ || !ioSvc_->readAnalog) {
        LOGW("No IOServiceV2 available for water temperature; using widest filtration window");
    } else if (!hasWaterTemp) {
        LOGW("Water temperature unavailable on ioId=%u; using widest filtration window", (unsigned)waterTempIoId_);
    }

    uint8_t startHour = 0;
    uint8_t stopHour = 0;
    uint8_t duration = 0;
    if (!computeFiltrationWindow_(waterTemp, startHour, stopHour, duration)) {
        LOGW("Invalid water temperature value");
        return false;
    }

    // PoolLogic stores the computed window back into the shared scheduler so
    // filtration state changes continue to arrive as regular scheduler events.
    if (!applyFiltrationWindowSlot_(startHour, stopHour)) return false;

    bool startStored = false;
    bool stopStored = false;
    if (cfgStore_) {
        startStored = cfgStore_->set(calcStartVar_, startHour);
        stopStored = cfgStore_->set(calcStopVar_, stopHour);
        if (!startStored || !stopStored) {
            LOGW("Failed to persist calculated filtration window start=%u stop=%u",
                 (unsigned)startHour,
                 (unsigned)stopHour);
        }
    }
    if (!cfgStore_ || !startStored) filtrationCalcStart_ = startHour;
    if (!cfgStore_ || !stopStored) filtrationCalcStop_ = stopHour;

    if (cfgMqttPub_) {
        // Recompute commands should always refresh MQTT cfg consumers, even if
        // computed values stayed identical and ConfigStore emitted no change.
        cfgMqttPub_->requestFullSync(MqttPublishPriority::Normal);
    }
    if (startHourOut) *startHourOut = startHour;
    if (stopHourOut) *stopHourOut = stopHour;
    if (durationOut) *durationOut = duration;

    if (hasWaterTemp) {
        LOGI("Filtration duration=%uh water=%.2fC start=%uh stop=%uh",
             (unsigned)duration,
             (double)waterTemp,
             (unsigned)startHour,
             (unsigned)stopHour);
        char detail[128] = {0};
        snprintf(detail,
                 sizeof(detail),
                 "Température eau %.2f °C, durée %u h, plage %02u:00-%02u:00.",
                 (double)waterTemp,
                 (unsigned)duration,
                 (unsigned)startHour,
                 (unsigned)stopHour);
        emitActivity_(ActivityCode::PoolLogicFiltrationWindowCalculated,
                      ActivitySource::Scheduler,
                      ActivitySeverity::Info,
                      ActivityRole::Filtration,
                      ActivityState::None,
                      ActivityReason::Scheduler,
                      filtrationDeviceSlot_,
                      "Plage de filtration recalculée",
                      detail,
                      "schedule");
    } else {
        LOGI("Filtration duration=%uh water=unavailable start=%uh stop=%uh",
             (unsigned)duration,
             (unsigned)startHour,
             (unsigned)stopHour);
        char detail[128] = {0};
        snprintf(detail,
                 sizeof(detail),
                 "Température eau indisponible, durée %u h, plage %02u:00-%02u:00.",
                 (unsigned)duration,
                 (unsigned)startHour,
                 (unsigned)stopHour);
        emitActivity_(ActivityCode::PoolLogicFiltrationWindowCalculated,
                      ActivitySource::Scheduler,
                      ActivitySeverity::Warning,
                      ActivityRole::Filtration,
                      ActivityState::None,
                      ActivityReason::Scheduler,
                      filtrationDeviceSlot_,
                      "Plage de filtration recalculée",
                      detail,
                      "schedule");
    }
    return true;
}
