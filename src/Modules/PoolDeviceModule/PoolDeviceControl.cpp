/**
 * @file PoolDeviceControl.cpp
 * @brief Service adapter, counter rollover and control-loop logic for pool devices.
 */

#include "PoolDeviceModule.h"
#include "Core/BufferUsageTracker.h"
#include "Domain/Pool/PoolDeviceSlots.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolDeviceModule)
#include "Core/ModuleLog.h"
#include "Modules/Network/TimeModule/TimeRuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include <Arduino.h>
#include <climits>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {
struct Fixed3 {
    unsigned long long whole = 0ULL;
    unsigned frac = 0U;
};

bool isFiniteNonNegative_(float value)
{
    return isfinite(value) && value >= 0.0f;
}

Fixed3 fixed3FromMl_(float value)
{
    if (!isFiniteNonNegative_(value)) return Fixed3{};

    const double scaled = ((double)value * 1000.0) + 0.5;
    const unsigned long long milli = (scaled >= (double)ULLONG_MAX)
        ? ULLONG_MAX
        : (unsigned long long)scaled;
    return Fixed3{milli / 1000ULL, (unsigned)(milli % 1000ULL)};
}
} // namespace

uint8_t PoolDeviceModule::activeCount_() const
{
    if (!lockState_()) return 0;
    uint8_t count = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (slots_[i].used) ++count;
    }
    unlockState_();
    return count;
}

PoolDeviceSvcStatus PoolDeviceModule::svcMetaImpl_(uint8_t slot, PoolDeviceSvcMeta* outMeta) const
{
    if (!outMeta) return POOLDEV_SVC_ERR_INVALID_ARG;
    if (!lockState_()) return POOLDEV_SVC_ERR_NOT_READY;
    if (slot >= POOL_DEVICE_MAX) {
        unlockState_();
        return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    }
    const PoolDeviceSlot& s = slots_[slot];
    if (!s.used) {
        unlockState_();
        return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    }

    *outMeta = PoolDeviceSvcMeta{};
    outMeta->slot = slot;
    outMeta->used = 1;
    outMeta->type = s.def.type;
    outMeta->enabled = s.def.enabled ? 1U : 0U;
    outMeta->blockReason = s.blockReason;
    outMeta->ioId = s.ioId;
    strncpy(outMeta->runtimeId, s.id, sizeof(outMeta->runtimeId) - 1);
    outMeta->runtimeId[sizeof(outMeta->runtimeId) - 1] = '\0';
    strncpy(outMeta->label, s.def.label, sizeof(outMeta->label) - 1);
    outMeta->label[sizeof(outMeta->label) - 1] = '\0';
    unlockState_();
    return POOLDEV_SVC_OK;
}

PoolDeviceSvcStatus PoolDeviceModule::svcReadActualOnImpl_(uint8_t slot, uint8_t* outOn, uint32_t* outTsMs) const
{
    if (!outOn) return POOLDEV_SVC_ERR_INVALID_ARG;
    if (!lockState_()) return POOLDEV_SVC_ERR_NOT_READY;
    if (slot >= POOL_DEVICE_MAX) {
        unlockState_();
        return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    }
    const PoolDeviceSlot& s = slots_[slot];
    if (!s.used) {
        unlockState_();
        return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    }
    if (!runtimeReady_) {
        unlockState_();
        return POOLDEV_SVC_ERR_NOT_READY;
    }

    *outOn = s.actualOn ? 1U : 0U;
    if (outTsMs) *outTsMs = s.stateTsMs;
    unlockState_();
    return POOLDEV_SVC_OK;
}

PoolDeviceSvcStatus PoolDeviceModule::svcWriteDesiredImpl_(uint8_t slot, uint8_t on)
{
    if (!lockState_()) return POOLDEV_SVC_ERR_NOT_READY;
    if (slot >= POOL_DEVICE_MAX) {
        unlockState_();
        return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    }
    PoolDeviceSlot& s = slots_[slot];
    if (!s.used) {
        unlockState_();
        return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    }
    if (!runtimeReady_) {
        unlockState_();
        return POOLDEV_SVC_ERR_NOT_READY;
    }

    const bool requested = (on != 0U);
    const bool maxUptimeReached = maxUptimeReached_(s);
    if (requested) {
        if (!s.def.enabled) {
            s.blockReason = POOL_DEVICE_BLOCK_DISABLED;
            unlockState_();
            return POOLDEV_SVC_ERR_DISABLED;
        }
        if (s.blockReason == POOL_DEVICE_BLOCK_UNBOUND) {
            unlockState_();
            return POOLDEV_SVC_ERR_IO;
        }
        if (s.blockReason == POOL_DEVICE_BLOCK_IO_DISABLED) {
            unlockState_();
            return POOLDEV_SVC_ERR_DISABLED;
        }
        if (maxUptimeReached) {
            s.blockReason = POOL_DEVICE_BLOCK_MAX_UPTIME;
            logStartInterlock_(slot, s.blockReason);
            unlockState_();
            return POOLDEV_SVC_ERR_MAX_UPTIME;
        }
        if (!dependenciesSatisfied_(slot)) {
            s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
            logStartInterlock_(slot, s.blockReason);
            unlockState_();
            return POOLDEV_SVC_ERR_INTERLOCK;
        }
    }

    s.desiredOn = requested;
    if (!requested && !maxUptimeReached) s.blockReason = POOL_DEVICE_BLOCK_NONE;

    if (!writesEnabled_) {
        tickDevices_(millis(), false);
        unlockState_();
        return POOLDEV_SVC_OK;
    }

    if (s.actualOn != requested) {
        if (writeIo_(s.ioId, requested)) {
            s.actualOn = requested;
            s.blockReason = POOL_DEVICE_BLOCK_NONE;
        } else {
            s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            tickDevices_(millis(), false);
            unlockState_();
            return POOLDEV_SVC_ERR_IO;
        }
    }

    tickDevices_(millis(), false);
    unlockState_();
    return POOLDEV_SVC_OK;
}

PoolDeviceSvcStatus PoolDeviceModule::svcSetWritesEnabledImpl_(uint8_t enabled)
{
    if (!lockState_()) return POOLDEV_SVC_ERR_NOT_READY;
    if (!runtimeReady_) {
        unlockState_();
        return POOLDEV_SVC_ERR_NOT_READY;
    }

    const bool newValue = (enabled != 0U);
    if (writesEnabled_ == newValue) {
        unlockState_();
        return POOLDEV_SVC_OK;
    }

    writesEnabled_ = newValue;
    LOGI("Pool actuator writes %s", writesEnabled_ ? "enabled" : "frozen");
    tickDevices_(millis(), false);
    unlockState_();
    return POOLDEV_SVC_OK;
}

uint8_t PoolDeviceModule::svcWritesEnabledImpl_() const
{
    if (!lockState_()) return 0U;
    const uint8_t out = writesEnabled_ ? 1U : 0U;
    unlockState_();
    return out;
}

PoolDeviceSvcStatus PoolDeviceModule::svcRefillTankImpl_(uint8_t slot, float remainingMl)
{
    if (!lockState_()) return POOLDEV_SVC_ERR_NOT_READY;
    if (slot >= POOL_DEVICE_MAX) {
        unlockState_();
        return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    }
    PoolDeviceSlot& s = slots_[slot];
    if (!s.used) {
        unlockState_();
        return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    }

    float remaining = remainingMl;
    if (remaining < 0.0f) remaining = 0.0f;
    if (s.def.tankCapacityMl > 0.0f && remaining > s.def.tankCapacityMl) {
        remaining = s.def.tankCapacityMl;
    }

    s.tankRemainingMl = remaining;
    s.forceMetricsCommit = true;
    s.persistDirty = true;
    s.persistImmediate = true;
    if (runtimeReady_) tickDevices_(millis(), false);
    unlockState_();
    return POOLDEV_SVC_OK;
}

void PoolDeviceModule::requestPeriodReconcile_()
{
    portENTER_CRITICAL(&resetMux_);
    periodReconcilePending_ = true;
    portEXIT_CRITICAL(&resetMux_);
}

bool PoolDeviceModule::currentPeriodKeys_(PeriodKeys& out) const
{
    if (!dataStore_) return false;
    if (!timeReady(*dataStore_)) return false;

    time_t now = 0;
    time(&now);
    if (now < (time_t)MIN_VALID_EPOCH_SEC) return false;

    struct tm localNow{};
    if (!localtime_r(&now, &localNow)) return false;

    const uint32_t year = (uint32_t)(localNow.tm_year + 1900);
    const uint32_t month = (uint32_t)(localNow.tm_mon + 1);
    const uint32_t day = (uint32_t)localNow.tm_mday;
    out.day = (year * 10000UL) + (month * 100UL) + day;
    out.month = (year * 100UL) + month;

    const bool weekStartMonday = weekStartMondayFromConfig_();
    const int weekOffset = weekStartMonday ? ((localNow.tm_wday + 6) % 7) : localNow.tm_wday;

    struct tm weekStart = localNow;
    weekStart.tm_hour = 12;
    weekStart.tm_min = 0;
    weekStart.tm_sec = 0;
    weekStart.tm_mday -= weekOffset;
    time_t weekEpoch = mktime(&weekStart);
    if (weekEpoch < (time_t)MIN_VALID_EPOCH_SEC) return false;

    struct tm weekTm{};
    if (!localtime_r(&weekEpoch, &weekTm)) return false;

    const uint32_t weekYear = (uint32_t)(weekTm.tm_year + 1900);
    const uint32_t weekMonth = (uint32_t)(weekTm.tm_mon + 1);
    const uint32_t weekDay = (uint32_t)weekTm.tm_mday;
    out.week = (weekYear * 10000UL) + (weekMonth * 100UL) + weekDay;
    return true;
}

bool PoolDeviceModule::weekStartMondayFromConfig_() const
{
    if (timeSvc_ && timeSvc_->weekStartMonday) {
        return timeSvc_->weekStartMonday(timeSvc_->ctx);
    }
    return true;
}

bool PoolDeviceModule::loadPersistedMetrics_(uint8_t slotIdx, PoolDeviceSlot& slot)
{
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!slot.used) return false;
    if (!cfgStore_) return false;

    const char* runtimeKey = PoolDeviceSlots::kSlots[slotIdx].runtimeKey;
    if (!runtimeKey || runtimeKey[0] == '\0') return false;

    runtimePersistBuf_[slotIdx][0] = '\0';
    size_t actualLen = 0U;
    bool readOk = false;
    if (cfgSvc_ && cfgSvc_->readRuntimeBlob) {
        readOk = cfgSvc_->readRuntimeBlob(cfgSvc_->ctx,
                                          runtimeKey,
                                          runtimePersistBuf_[slotIdx],
                                          sizeof(runtimePersistBuf_[slotIdx]) - 1U,
                                          &actualLen);
    }
    if (!readOk) {
        readOk = cfgStore_->readRuntimeBlob(runtimeKey,
                                            runtimePersistBuf_[slotIdx],
                                            sizeof(runtimePersistBuf_[slotIdx]) - 1U,
                                            &actualLen);
    }
    if (!readOk) {
        return false;
    }
    runtimePersistBuf_[slotIdx][sizeof(runtimePersistBuf_[slotIdx]) - 1U] = '\0';
    if (actualLen >= sizeof(runtimePersistBuf_[slotIdx])) {
        LOGW("Pool device %s runtime blob too large len=%u", slot.id, (unsigned)actualLen);
        runtimePersistBuf_[slotIdx][0] = '\0';
        return false;
    }

    char parseBuf[sizeof(runtimePersistBuf_[0])] = {0};
    strncpy(parseBuf, runtimePersistBuf_[slotIdx], sizeof(parseBuf) - 1);
    parseBuf[sizeof(parseBuf) - 1] = '\0';

    char* save = nullptr;
    auto nextTok = [&save](char* s) -> char* { return strtok_r(s, ",", &save); };

    char* tok = nextTok(parseBuf);
    if (!tok || strcmp(tok, "v1") != 0) {
        LOGW("Pool device %s runtime blob invalid header", slot.id);
        return false;
    }

    auto parseU64 = [&](uint64_t& out) -> bool {
        char* t = nextTok(nullptr);
        if (!t || t[0] == '\0') return false;
        char* end = nullptr;
        out = strtoull(t, &end, 10);
        return end && *end == '\0';
    };
    auto parseU32 = [&](uint32_t& out) -> bool {
        char* t = nextTok(nullptr);
        if (!t || t[0] == '\0') return false;
        char* end = nullptr;
        out = (uint32_t)strtoul(t, &end, 10);
        return end && *end == '\0';
    };
    auto parseF32 = [&](float& out) -> bool {
        char* t = nextTok(nullptr);
        if (!t || t[0] == '\0') return false;
        char* end = nullptr;
        out = strtof(t, &end);
        return end && *end == '\0';
    };

    uint64_t runningDay = 0ULL;
    uint64_t runningWeek = 0ULL;
    uint64_t runningMonth = 0ULL;
    uint64_t runningTotal = 0ULL;
    float injectedDay = 0.0f;
    float injectedWeek = 0.0f;
    float injectedMonth = 0.0f;
    float injectedTotal = 0.0f;
    float tank = 0.0f;
    uint32_t dayKey = 0U;
    uint32_t weekKey = 0U;
    uint32_t monthKey = 0U;

    if (!parseU64(runningDay) ||
        !parseU64(runningWeek) ||
        !parseU64(runningMonth) ||
        !parseU64(runningTotal) ||
        !parseF32(injectedDay) ||
        !parseF32(injectedWeek) ||
        !parseF32(injectedMonth) ||
        !parseF32(injectedTotal) ||
        !parseF32(tank) ||
        !parseU32(dayKey) ||
        !parseU32(weekKey) ||
        !parseU32(monthKey)) {
        LOGW("Pool device %s runtime blob parse failed", slot.id);
        return false;
    }

    if (nextTok(nullptr) != nullptr) {
        LOGW("Pool device %s runtime blob has trailing fields", slot.id);
        return false;
    }

    slot.runningMsDay = runningDay;
    slot.runningMsWeek = runningWeek;
    slot.runningMsMonth = runningMonth;
    slot.runningMsTotal = runningTotal;
    slot.injectedMlDay = isFiniteNonNegative_(injectedDay) ? injectedDay : 0.0f;
    slot.injectedMlWeek = isFiniteNonNegative_(injectedWeek) ? injectedWeek : 0.0f;
    slot.injectedMlMonth = isFiniteNonNegative_(injectedMonth) ? injectedMonth : 0.0f;
    slot.injectedMlTotal = isFiniteNonNegative_(injectedTotal) ? injectedTotal : 0.0f;
    slot.tankRemainingMl = isFiniteNonNegative_(tank) ? tank : 0.0f;
    slot.dayKey = dayKey;
    slot.weekKey = weekKey;
    slot.monthKey = monthKey;
    slot.hasPersistedMetrics = true;
    slot.persistDirty = false;
    slot.persistImmediate = false;
    return true;
}

bool PoolDeviceModule::persistMetrics_(uint8_t slotIdx, PoolDeviceSlot& slot, uint32_t nowMs)
{
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!slot.used) return false;
    if (!cfgStore_) return false;

    const char* runtimeKey = PoolDeviceSlots::kSlots[slotIdx].runtimeKey;
    if (!runtimeKey || runtimeKey[0] == '\0') return false;

    const Fixed3 injectedDay = fixed3FromMl_(slot.injectedMlDay);
    const Fixed3 injectedWeek = fixed3FromMl_(slot.injectedMlWeek);
    const Fixed3 injectedMonth = fixed3FromMl_(slot.injectedMlMonth);
    const Fixed3 injectedTotal = fixed3FromMl_(slot.injectedMlTotal);
    const Fixed3 tankRemaining = fixed3FromMl_(slot.tankRemainingMl);

    char encoded[sizeof(runtimePersistBuf_[0])] = {0};
    const int wrote = snprintf(
        encoded, sizeof(encoded),
        "v1,%llu,%llu,%llu,%llu,%llu.%03u,%llu.%03u,%llu.%03u,%llu.%03u,%llu.%03u,%lu,%lu,%lu",
        (unsigned long long)slot.runningMsDay,
        (unsigned long long)slot.runningMsWeek,
        (unsigned long long)slot.runningMsMonth,
        (unsigned long long)slot.runningMsTotal,
        injectedDay.whole,
        injectedDay.frac,
        injectedWeek.whole,
        injectedWeek.frac,
        injectedMonth.whole,
        injectedMonth.frac,
        injectedTotal.whole,
        injectedTotal.frac,
        tankRemaining.whole,
        tankRemaining.frac,
        (unsigned long)slot.dayKey,
        (unsigned long)slot.weekKey,
        (unsigned long)slot.monthKey);
    if (wrote <= 0 || (size_t)wrote >= sizeof(encoded)) {
        LOGW("Pool device %s runtime persist failed", slot.id);
        return false;
    }
    const size_t encodedLen = strnlen(encoded, sizeof(encoded)) + 1U;
    bool persisted = false;
    if (cfgSvc_ && cfgSvc_->writeRuntimeBlobAsync) {
        persisted = cfgSvc_->writeRuntimeBlobAsync(cfgSvc_->ctx, runtimeKey, encoded, encodedLen);
    }
    if (!persisted) return false;

    strncpy(runtimePersistBuf_[slotIdx], encoded, sizeof(runtimePersistBuf_[slotIdx]) - 1U);
    runtimePersistBuf_[slotIdx][sizeof(runtimePersistBuf_[slotIdx]) - 1U] = '\0';
    BufferUsageTracker::note(TrackedBufferId::PoolDeviceRuntimePersistTable,
                             runtimePersistUsage_(),
                             runtimePersistCapacity_(),
                             slot.id,
                             encoded);

    slot.lastPersistMs = nowMs;
    slot.persistDirty = false;
    slot.persistImmediate = false;
    slot.hasPersistedMetrics = true;
    return true;
}

bool PoolDeviceModule::reconcilePeriodCountersFromClock_()
{
    PeriodKeys keys{};
    if (!currentPeriodKeys_(keys)) return false;

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;

        bool changed = false;
        if (s.dayKey == 0U) {
            s.dayKey = keys.day;
            changed = true;
        } else if (s.dayKey != keys.day) {
            s.runningMsDay = 0;
            s.injectedMlDay = 0.0f;
            s.dayKey = keys.day;
            changed = true;
        }
        if (s.weekKey == 0U) {
            s.weekKey = keys.week;
            changed = true;
        } else if (s.weekKey != keys.week) {
            s.runningMsWeek = 0;
            s.injectedMlWeek = 0.0f;
            s.weekKey = keys.week;
            changed = true;
        }
        if (s.monthKey == 0U) {
            s.monthKey = keys.month;
            changed = true;
        } else if (s.monthKey != keys.month) {
            s.runningMsMonth = 0;
            s.injectedMlMonth = 0.0f;
            s.monthKey = keys.month;
            changed = true;
        }

        if (changed) {
            s.forceMetricsCommit = true;
            s.persistDirty = true;
            s.persistImmediate = true;
        }
    }

    return true;
}

void PoolDeviceModule::resetDailyCounters_()
{
    PeriodKeys keys{};
    const bool hasKeys = currentPeriodKeys_(keys);
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        s.runningMsDay = 0;
        s.injectedMlDay = 0.0f;
        if (hasKeys) s.dayKey = keys.day;
        s.forceMetricsCommit = true;
        s.persistDirty = true;
        s.persistImmediate = true;
    }
}

void PoolDeviceModule::resetWeeklyCounters_()
{
    PeriodKeys keys{};
    const bool hasKeys = currentPeriodKeys_(keys);
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        s.runningMsWeek = 0;
        s.injectedMlWeek = 0.0f;
        if (hasKeys) s.weekKey = keys.week;
        s.forceMetricsCommit = true;
        s.persistDirty = true;
        s.persistImmediate = true;
    }
}

void PoolDeviceModule::resetMonthlyCounters_()
{
    PeriodKeys keys{};
    const bool hasKeys = currentPeriodKeys_(keys);
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        s.runningMsMonth = 0;
        s.injectedMlMonth = 0.0f;
        if (hasKeys) s.monthKey = keys.month;
        s.forceMetricsCommit = true;
        s.persistDirty = true;
        s.persistImmediate = true;
    }
}

void PoolDeviceModule::tickDevices_(uint32_t nowMs, bool allowPersist)
{
    uint8_t pending = 0;
    bool reconcilePending = false;
    portENTER_CRITICAL(&resetMux_);
    pending = resetPendingMask_;
    resetPendingMask_ = 0;
    reconcilePending = periodReconcilePending_;
    periodReconcilePending_ = false;
    portEXIT_CRITICAL(&resetMux_);

    if (pending & RESET_PENDING_DAY) resetDailyCounters_();
    if (pending & RESET_PENDING_WEEK) resetWeeklyCounters_();
    if (pending & RESET_PENDING_MONTH) resetMonthlyCounters_();
    if (reconcilePending && !reconcilePeriodCountersFromClock_()) {
        requestPeriodReconcile_();
    }

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        if (runtimeReady_ && !s.runtimePublishable) continue;
        const bool wasActualOn = s.actualOn;
        bool stateChanged = false;
        bool metricsChanged = (pending != 0U) || s.forceMetricsCommit;
        s.forceMetricsCommit = false;

        if (s.lastTickMs == 0) s.lastTickMs = nowMs;
        const uint32_t deltaMs = (uint32_t)(nowMs - s.lastTickMs);
        s.lastTickMs = nowMs;

        bool ioOn = false;
        if (readIoState_(s, ioOn)) {
            if (s.actualOn != ioOn) stateChanged = true;
            s.actualOn = ioOn;
        }

        if (s.def.tankCapacityMl <= 0.0f) {
            if (s.tankRemainingMl != 0.0f) {
                s.tankRemainingMl = 0.0f;
                metricsChanged = true;
            }
        } else if (s.tankRemainingMl > s.def.tankCapacityMl) {
            s.tankRemainingMl = s.def.tankCapacityMl;
            metricsChanged = true;
        }

        if (!s.def.enabled && s.desiredOn) {
            s.desiredOn = false;
            s.blockReason = POOL_DEVICE_BLOCK_DISABLED;
            stateChanged = true;
        }

        const bool maxUptimeReached = maxUptimeReached_(s);
        if (s.def.enabled && maxUptimeReached) {
            if (s.desiredOn) {
                s.desiredOn = false;
                stateChanged = true;
            }
            if (s.actualOn && writesEnabled_) {
                if (writeIo_(s.ioId, false)) {
                    s.actualOn = false;
                    s.blockReason = POOL_DEVICE_BLOCK_MAX_UPTIME;
                } else {
                    s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
                }
                stateChanged = true;
            } else if (s.blockReason != POOL_DEVICE_BLOCK_IO_ERROR &&
                       s.blockReason != POOL_DEVICE_BLOCK_MAX_UPTIME) {
                s.blockReason = POOL_DEVICE_BLOCK_MAX_UPTIME;
                stateChanged = true;
            }
        } else if (s.blockReason == POOL_DEVICE_BLOCK_MAX_UPTIME) {
            s.blockReason = POOL_DEVICE_BLOCK_NONE;
            stateChanged = true;
        }

        // Interlocks are re-evaluated every tick so manual writes cannot bypass them.
        if (s.actualOn && !dependenciesSatisfied_(i) && writesEnabled_) {
            s.desiredOn = false;
            if (writeIo_(s.ioId, false)) {
                s.actualOn = false;
                s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
            } else {
                s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            }
            stateChanged = true;
        }

        if (s.desiredOn && !s.actualOn && writesEnabled_) {
            if (dependenciesSatisfied_(i)) {
                if (writeIo_(s.ioId, true)) {
                    s.actualOn = true;
                    s.blockReason = POOL_DEVICE_BLOCK_NONE;
                } else {
                    s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
                }
                stateChanged = true;
            } else {
                s.desiredOn = false;
                s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
                stateChanged = true;
            }
        } else if (!s.desiredOn && s.actualOn && writesEnabled_) {
            if (writeIo_(s.ioId, false)) {
                s.actualOn = false;
                s.blockReason = maxUptimeReached_(s) ? POOL_DEVICE_BLOCK_MAX_UPTIME : POOL_DEVICE_BLOCK_NONE;
            } else {
                s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            }
            stateChanged = true;
        }

        if (s.actualOn && deltaMs > 0) {
            s.runningMsDay += deltaMs;
            s.runningMsWeek += deltaMs;
            s.runningMsMonth += deltaMs;
            s.runningMsTotal += deltaMs;

            // Convert L/h to ml/ms for injected volume accumulation.
            const float flowPerMs = s.def.flowLPerHour / 3600.0f;
            const float injectedDelta = flowPerMs * (float)deltaMs;
            if (injectedDelta > 0.0f) {
                s.injectedMlDay += injectedDelta;
                s.injectedMlWeek += injectedDelta;
                s.injectedMlMonth += injectedDelta;
                s.injectedMlTotal += injectedDelta;

                if (s.def.tankCapacityMl > 0.0f) {
                    s.tankRemainingMl -= injectedDelta;
                    if (s.tankRemainingMl < 0.0f) s.tankRemainingMl = 0.0f;
                }
            }
            if ((uint32_t)(nowMs - s.lastRuntimeCommitMs) >= 1000U) {
                metricsChanged = true;
            }
        }
        if (wasActualOn && !s.actualOn) {
            s.persistDirty = true;
            s.persistImmediate = true;
        }

        if (dataStore_) {
            PoolDeviceRuntimeStateEntry prevState{};
            if (poolDeviceRuntimeState(*dataStore_, i, prevState) && prevState.valid) {
                if ((prevState.enabled != s.def.enabled) ||
                    (prevState.desiredOn != s.desiredOn) ||
                    (prevState.actualOn != s.actualOn) ||
                    (prevState.type != s.def.type) ||
                    (prevState.blockReason != s.blockReason)) {
                    stateChanged = true;
                }
            } else {
                stateChanged = true;
            }
        }

        if (stateChanged) {
            s.stateTsMs = nowMs;
        }
        if (metricsChanged) {
            s.metricsTsMs = nowMs;
            s.lastRuntimeCommitMs = nowMs;
            s.persistDirty = true;
        }

        if (dataStore_) {
            PoolDeviceRuntimeStateEntry rtState{};
            rtState.valid = true;
            rtState.enabled = s.def.enabled;
            rtState.desiredOn = s.desiredOn;
            rtState.actualOn = s.actualOn;
            rtState.type = s.def.type;
            rtState.blockReason = s.blockReason;
            rtState.tsMs = s.stateTsMs;
            (void)setPoolDeviceRuntimeState(*dataStore_, i, rtState);

            if (metricsChanged) {
                PoolDeviceRuntimeMetricsEntry rtMetrics{};
                rtMetrics.valid = true;
                rtMetrics.runningSecDay = toSeconds_(s.runningMsDay);
                rtMetrics.runningSecWeek = toSeconds_(s.runningMsWeek);
                rtMetrics.runningSecMonth = toSeconds_(s.runningMsMonth);
                rtMetrics.runningSecTotal = toSeconds_(s.runningMsTotal);
                rtMetrics.injectedMlDay = s.injectedMlDay;
                rtMetrics.injectedMlWeek = s.injectedMlWeek;
                rtMetrics.injectedMlMonth = s.injectedMlMonth;
                rtMetrics.injectedMlTotal = s.injectedMlTotal;
                rtMetrics.tankRemainingMl = s.tankRemainingMl;
                rtMetrics.tsMs = s.metricsTsMs;
                (void)setPoolDeviceRuntimeMetrics(*dataStore_, i, rtMetrics);
            }
        }

        bool shouldPersist = false;
        if (s.persistDirty) {
            if (s.persistImmediate) {
                shouldPersist = true;
            } else if (!s.actualOn) {
                shouldPersist = true;
            } else if ((uint32_t)(nowMs - s.lastPersistMs) >= RUNTIME_PERSIST_INTERVAL_MS) {
                shouldPersist = true;
            }
        }
        if (allowPersist && shouldPersist) {
            (void)persistMetrics_(i, s, nowMs);
        }
    }
}

bool PoolDeviceModule::dependenciesSatisfied_(uint8_t slotIdx) const
{
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    const PoolDeviceSlot& s = slots_[slotIdx];
    if (!s.used) return false;
    if (s.def.dependsOnMask == 0) return true;

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if ((s.def.dependsOnMask & (uint8_t)(1u << i)) == 0) continue;
        if (i == slotIdx) continue;
        const PoolDeviceSlot& dep = slots_[i];
        if (!dep.used || !dep.actualOn) return false;
    }
    return true;
}

void PoolDeviceModule::logStartInterlock_(uint8_t slotIdx, uint8_t reason) const
{
    if (slotIdx >= POOL_DEVICE_MAX) return;
    const PoolDeviceSlot& s = slots_[slotIdx];
    if (!s.used) return;

    LOGW("PoolDev start blk sl=%u id=%s rsn=%u(%s) dep=0x%02X en=%u a=%u d=%u dayS=%lu maxS=%ld",
         (unsigned)slotIdx,
         s.id,
         (unsigned)reason,
         blockReasonStr_(reason),
         (unsigned)s.def.dependsOnMask,
         s.def.enabled ? 1U : 0U,
         s.actualOn ? 1U : 0U,
         s.desiredOn ? 1U : 0U,
         (unsigned long)toSeconds_(s.runningMsDay),
         (long)s.def.maxUptimeDaySec);

    if (reason != POOL_DEVICE_BLOCK_INTERLOCK) return;

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if ((s.def.dependsOnMask & (uint8_t)(1u << i)) == 0) continue;
        if (i == slotIdx) continue;
        const PoolDeviceSlot& dep = slots_[i];
        LOGW("PoolDev dep sl=%u used=%u id=%s en=%u a=%u d=%u blk=%u(%s)",
             (unsigned)i,
             dep.used ? 1U : 0U,
             dep.id,
             dep.def.enabled ? 1U : 0U,
             dep.actualOn ? 1U : 0U,
             dep.desiredOn ? 1U : 0U,
             (unsigned)dep.blockReason,
             blockReasonStr_(dep.blockReason));
    }
}

bool PoolDeviceModule::maxUptimeReached_(const PoolDeviceSlot& slot)
{
    if (slot.def.maxUptimeDaySec <= 0) return false;
    const uint64_t limitMs = (uint64_t)(uint32_t)slot.def.maxUptimeDaySec * 1000ULL;
    return slot.runningMsDay >= limitMs;
}

bool PoolDeviceModule::readIoState_(const PoolDeviceSlot& slot, bool& onOut) const
{
    if (!ioSvc_ || !ioSvc_->readDigital) return false;
    uint8_t on = 0;
    if (ioSvc_->readDigital(ioSvc_->ctx, slot.ioId, &on, nullptr, nullptr) != IO_OK) return false;
    onOut = (on != 0);
    return true;
}

bool PoolDeviceModule::writeIo_(IoId ioId, bool on)
{
    if (!ioSvc_ || !ioSvc_->writeDigital) return false;
    return ioSvc_->writeDigital(ioSvc_->ctx, ioId, on ? 1U : 0U, millis()) == IO_OK;
}

uint32_t PoolDeviceModule::toSeconds_(uint64_t ms)
{
    const uint64_t sec = ms / 1000ULL;
    return (sec > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)sec;
}
