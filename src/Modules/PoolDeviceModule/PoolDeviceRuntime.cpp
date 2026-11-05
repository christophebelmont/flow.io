/**
 * @file PoolDeviceRuntime.cpp
 * @brief Runtime snapshots, config publications and runtime bootstrap helpers.
 */

#include "PoolDeviceModule.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"
#include "Domain/Pool/PoolIds.h"
#include "Domain/Pool/PoolDeviceSlots.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolDeviceModule)
#include "Core/ModuleLog.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace {
static constexpr const char* kPoolDeviceCfgTopicBase = "cfg/pdm";

bool isFiniteNonNegative_(float value)
{
    return isfinite(value) && value >= 0.0f;
}
} // namespace

bool PoolDeviceModule::defineDevice(const PoolDeviceDefinition& def)
{
    if (!ensureStorage_()) return false;
    if (!lockState_()) return false;
    if (def.slot >= POOL_DEVICE_MAX) {
        LOGW("Pool device definition missing explicit valid slot");
        unlockState_();
        return false;
    }

    PoolDeviceSlot& s = slots_[def.slot];
    if (s.used) {
        LOGW("Pool device slot %u already defined", (unsigned)def.slot);
        unlockState_();
        return false;
    }

    s.used = true;
    s.id = PoolDeviceSlots::kSlots[def.slot].id;
    s.def = def;

    if (s.def.label[0] == '\0') {
        strncpy(s.def.label, s.id, sizeof(s.def.label) - 1);
        s.def.label[sizeof(s.def.label) - 1] = '\0';
    }
    if (s.def.ioSlot == IO_SLOT_INVALID) {
        LOGW("Pool device %s missing IO slot binding", s.id);
        s.used = false;
        unlockState_();
        return false;
    }
    if (ioSlotKind(s.def.ioSlot) != IO_SLOT_DIGITAL_OUTPUT) {
        LOGW("Pool device %s IO slot is not a digital output", s.id);
        s.used = false;
        unlockState_();
        return false;
    }
    s.ioId = ioIdFromSlot(s.def.ioSlot);
    if (s.ioId == IO_ID_INVALID) {
        LOGW("Pool device %s IO slot cannot resolve ioId", s.id);
        s.used = false;
        unlockState_();
        return false;
    }
    if (s.def.maxUptimeDaySec < 0) {
        s.def.maxUptimeDaySec = 0;
    }
    s.desiredOn = false;
    s.actualOn = false;
    s.blockReason = POOL_DEVICE_BLOCK_NONE;
    s.runtimePublishable = false;

    if (s.def.tankCapacityMl > 0.0f) {
        float initial = (s.def.tankInitialMl > 0.0f) ? s.def.tankInitialMl : s.def.tankCapacityMl;
        if (initial > s.def.tankCapacityMl) initial = s.def.tankCapacityMl;
        if (initial < 0.0f) initial = 0.0f;
        s.tankRemainingMl = initial;
    } else {
        s.tankRemainingMl = 0.0f;
    }
    s.dayKey = 0;
    s.weekKey = 0;
    s.monthKey = 0;
    s.hasPersistedMetrics = false;
    s.persistDirty = false;
    s.persistImmediate = false;

    unlockState_();
    return true;
}

const char* PoolDeviceModule::deviceLabel(uint8_t idx) const
{
    if (idx >= POOL_DEVICE_MAX) return nullptr;
    const PoolDeviceSlot& s = slots_[idx];
    if (!s.used) return nullptr;
    return (s.def.label[0] != '\0') ? s.def.label : s.id;
}

const char* PoolDeviceModule::blockReasonStr_(uint8_t reason)
{
    if (reason == POOL_DEVICE_BLOCK_DISABLED) return "disabled";
    if (reason == POOL_DEVICE_BLOCK_INTERLOCK) return "interlock";
    if (reason == POOL_DEVICE_BLOCK_IO_ERROR) return "io_error";
    if (reason == POOL_DEVICE_BLOCK_MAX_UPTIME) return "max_uptime";
    if (reason == POOL_DEVICE_BLOCK_UNBOUND) return "unbound";
    if (reason == POOL_DEVICE_BLOCK_IO_DISABLED) return "io_disabled";
    return "none";
}

uint8_t PoolDeviceModule::runtimeSnapshotCount() const
{
    return (uint8_t)(activeCount_() * 2U);
}

bool PoolDeviceModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    if (!dataStore_) return writer.writeUnavailable(makeRuntimeUiId(moduleId(), valueId));

    uint8_t slotIdx = 0xFF;
    switch (valueId) {
        case RuntimeUiFiltrationOn:
            slotIdx = PoolIds::DeviceFiltrationPump;
            break;
        case RuntimeUiPhPumpOn:
            slotIdx = PoolIds::DevicePhPump;
            break;
        case RuntimeUiChlorinePumpOn:
            slotIdx = PoolIds::DeviceChlorinePump;
            break;
        case RuntimeUiRobotOn:
            slotIdx = PoolIds::DeviceRobot;
            break;
        default:
            return false;
    }

    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);
    PoolDeviceRuntimeStateEntry state{};
    if (!poolDeviceRuntimeState(*dataStore_, slotIdx, state)) {
        return writer.writeUnavailable(runtimeId);
    }
    return writer.writeBool(runtimeId, state.actualOn);
}

bool PoolDeviceModule::snapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& slotIdxOut, bool& metricsOut) const
{
    if (!lockState_()) return false;
    uint8_t seen = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (!slots_[i].used) continue;
        if (seen == snapshotIdx) {
            slotIdxOut = i;
            metricsOut = false;
            unlockState_();
            return true;
        }
        ++seen;
        if (seen == snapshotIdx) {
            slotIdxOut = i;
            metricsOut = true;
            unlockState_();
            return true;
        }
        ++seen;
    }
    unlockState_();
    return false;
}

bool PoolDeviceModule::slotRuntimePublishable_(uint8_t slotIdx) const
{
    if (!lockState_()) return false;
    if (slotIdx >= POOL_DEVICE_MAX) {
        unlockState_();
        return false;
    }
    const PoolDeviceSlot& s = slots_[slotIdx];
    const bool out = s.used && (runtimeReady_ ? s.runtimePublishable : true);
    unlockState_();
    return out;
}

bool PoolDeviceModule::buildStateSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;
    if (!dataStore_) return false;
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!lockState_()) return false;
    if (!slotRuntimePublishable_(slotIdx)) {
        unlockState_();
        return false;
    }

    PoolDeviceRuntimeStateEntry entry{};
    if (!poolDeviceRuntimeState(*dataStore_, slotIdx, entry)) {
        unlockState_();
        return false;
    }

    char label[sizeof(PoolDeviceDefinition::label)] = {0};
    const PoolDeviceSlot& s = slots_[slotIdx];
    snprintf(label, sizeof(label), "%s", (s.def.label[0] != '\0') ? s.def.label : (s.id ? s.id : "pd"));
    const char* blockReason = blockReasonStr_(entry.blockReason);
    const int wrote = snprintf(
        out, len,
        "{\"id\":\"pd%u\",\"name\":\"%s\",\"enabled\":%s,\"desired\":%s,\"on\":%s,"
        "\"block\":\"%s\",\"ts\":%lu}",
        (unsigned)slotIdx,
        label[0] ? label : "pd",
        entry.enabled ? "true" : "false",
        entry.desiredOn ? "true" : "false",
        entry.actualOn ? "true" : "false",
        blockReason,
        (unsigned long)entry.tsMs
    );
    if (wrote < 0 || (size_t)wrote >= len) {
        unlockState_();
        return false;
    }

    maxTsOut = (entry.tsMs == 0U) ? 1U : entry.tsMs;
    unlockState_();
    return true;
}

bool PoolDeviceModule::buildMetricsSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;
    if (!dataStore_) return false;
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!lockState_()) return false;
    if (!slotRuntimePublishable_(slotIdx)) {
        unlockState_();
        return false;
    }

    PoolDeviceRuntimeMetricsEntry entry{};
    if (!poolDeviceRuntimeMetrics(*dataStore_, slotIdx, entry)) {
        unlockState_();
        return false;
    }

    char label[sizeof(PoolDeviceDefinition::label)] = {0};
    const PoolDeviceSlot& s = slots_[slotIdx];
    snprintf(label, sizeof(label), "%s", (s.def.label[0] != '\0') ? s.def.label : (s.id ? s.id : "pd"));
    const int wrote = snprintf(
        out, len,
        "{\"id\":\"pd%u\",\"name\":\"%s\","
        "\"running\":{\"day_s\":%lu,\"week_s\":%lu,\"month_s\":%lu,\"total_s\":%lu},"
        "\"injected\":{\"day_ml\":%.3f,\"week_ml\":%.3f,\"month_ml\":%.3f,\"total_ml\":%.3f},"
        "\"tank\":{\"remaining_ml\":%.3f},\"ts\":%lu}",
        (unsigned)slotIdx,
        label[0] ? label : "pd",
        (unsigned long)entry.runningSecDay,
        (unsigned long)entry.runningSecWeek,
        (unsigned long)entry.runningSecMonth,
        (unsigned long)entry.runningSecTotal,
        (double)entry.injectedMlDay,
        (double)entry.injectedMlWeek,
        (double)entry.injectedMlMonth,
        (double)entry.injectedMlTotal,
        (double)entry.tankRemainingMl,
        (unsigned long)entry.tsMs
    );
    if (wrote < 0 || (size_t)wrote >= len) {
        unlockState_();
        return false;
    }

    maxTsOut = (entry.tsMs == 0U) ? 1U : entry.tsMs;
    unlockState_();
    return true;
}

const char* PoolDeviceModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) return nullptr;
    if (!slotRuntimePublishable_(slotIdx)) return nullptr;

    static char suffix[32];
    if (metrics) {
        snprintf(suffix, sizeof(suffix), "rt/pdm/metrics/pd%u", (unsigned)slotIdx);
    } else {
        snprintf(suffix, sizeof(suffix), "rt/pdm/state/pd%u", (unsigned)slotIdx);
    }
    return suffix;
}

RuntimeRouteClass PoolDeviceModule::runtimeSnapshotClass(uint8_t idx) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) {
        return RuntimeRouteClass::NumericThrottled;
    }
    if (!slotRuntimePublishable_(slotIdx)) {
        return RuntimeRouteClass::NumericThrottled;
    }
    return metrics ? RuntimeRouteClass::NumericThrottled : RuntimeRouteClass::ActuatorImmediate;
}

bool PoolDeviceModule::runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) return false;
    if (!slotRuntimePublishable_(slotIdx)) return false;

    const DataKey expected = metrics
        ? (DataKey)(DATAKEY_POOL_DEVICE_METRICS_BASE + slotIdx)
        : (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + slotIdx);
    return key == expected;
}

bool PoolDeviceModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) return false;
    return metrics
        ? buildMetricsSnapshot_(slotIdx, out, len, maxTsOut)
        : buildStateSnapshot_(slotIdx, out, len, maxTsOut);
}

bool PoolDeviceModule::configureRuntime_()
{
    if (runtimeReady_) return true;
    if (!ioSvc_) return false;

    const uint32_t now = millis();
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;

        bool ioReady = false;
        bool ioManuallyDisabled = false;
        IoEndpointMeta meta{};
        const IoStatus metaStatus = ioSvc_->meta(ioSvc_->ctx, s.ioId, &meta);
        if (metaStatus == IO_OK && ioSvc_->runtimeStatus) {
            IoRuntimeStatus runtime{};
            ioManuallyDisabled =
                ioSvc_->runtimeStatus(ioSvc_->ctx, s.ioId, &runtime) == IO_OK &&
                runtime.state == IO_RUNTIME_MANUALLY_DISABLED;
        }
        if (!ioManuallyDisabled &&
            metaStatus == IO_OK &&
            meta.kind == IO_KIND_DIGITAL_OUT &&
            (meta.capabilities & IO_CAP_W) != 0) {
            ioReady = true;
        } else {
            LOGD("Pool device %s sleeping ioId=%u status=%u kind=%u caps=0x%02X",
                 s.id,
                 (unsigned)s.ioId,
                 (unsigned)metaStatus,
                 (unsigned)meta.kind,
                 (unsigned)meta.capabilities);
            s.actualOn = false;
            s.desiredOn = false;
            s.blockReason = !s.def.enabled
                ? POOL_DEVICE_BLOCK_DISABLED
                : (ioManuallyDisabled ? POOL_DEVICE_BLOCK_IO_DISABLED : POOL_DEVICE_BLOCK_UNBOUND);
        }
        s.runtimePublishable = ioReady;

        if (s.def.tankCapacityMl <= 0.0f) {
            s.tankRemainingMl = 0.0f;
        } else if (!s.hasPersistedMetrics) {
            float initial = (s.def.tankInitialMl > 0.0f) ? s.def.tankInitialMl : s.def.tankCapacityMl;
            if (initial > s.def.tankCapacityMl) initial = s.def.tankCapacityMl;
            if (initial < 0.0f) initial = 0.0f;
            s.tankRemainingMl = initial;
        } else {
            if (!isFiniteNonNegative_(s.tankRemainingMl)) s.tankRemainingMl = 0.0f;
            if (s.tankRemainingMl > s.def.tankCapacityMl) s.tankRemainingMl = s.def.tankCapacityMl;
        }

        bool initialIoOn = false;
        if (ioReady && readIoState_(s, initialIoOn)) {
            s.actualOn = initialIoOn;
            s.desiredOn = initialIoOn;
            if (initialIoOn) {
                LOGI("Pool device %s boot sync: hardware ON adopted as desired", s.id);
            }
        }

        s.lastTickMs = now;
        s.stateTsMs = now;
        s.metricsTsMs = now;
        s.lastRuntimeCommitMs = now;
        s.lastPersistMs = now;

        PoolDeviceRuntimeStateEntry rtState{};
        rtState.valid = true;
        rtState.enabled = s.def.enabled;
        rtState.desiredOn = s.desiredOn;
        rtState.actualOn = s.actualOn;
        rtState.type = s.def.type;
        rtState.blockReason = s.blockReason;
        rtState.tsMs = s.stateTsMs;

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
        if (dataStore_ && s.runtimePublishable) {
            (void)setPoolDeviceRuntimeState(*dataStore_, i, rtState);
            (void)setPoolDeviceRuntimeMetrics(*dataStore_, i, rtMetrics);
        }
    }

    runtimeReady_ = true;
    return true;
}

MqttBuildResult PoolDeviceModule::buildCfgBasePdmStatic_(void* ctx, uint16_t, MqttBuildContext& buildCtx)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(ctx);
    return self ? self->buildCfgBasePdm_(buildCtx) : MqttBuildResult::PermanentError;
}

MqttBuildResult PoolDeviceModule::buildCfgBasePdm_(MqttBuildContext& buildCtx)
{
    if (!cfgStore_) return MqttBuildResult::RetryLater;
    if (!buildCtx.topic || buildCtx.topicCapacity == 0U || !buildCtx.payload || buildCtx.payloadCapacity == 0U) {
        return MqttBuildResult::PermanentError;
    }
    if (!mqttSvc_ || !mqttSvc_->formatTopic) return MqttBuildResult::RetryLater;

    char relativeTopic[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    size_t topicLen = 0U;
    if (!MqttConfigRouteProducer::buildRelativeTopic(relativeTopic,
                                                     sizeof(relativeTopic),
                                                     kPoolDeviceCfgTopicBase,
                                                     "",
                                                     topicLen)) {
        return MqttBuildResult::PermanentError;
    }
    mqttSvc_->formatTopic(mqttSvc_->ctx, relativeTopic, buildCtx.topic, buildCtx.topicCapacity);
    if (buildCtx.topic[0] == '\0') return MqttBuildResult::PermanentError;
    topicLen = strnlen(buildCtx.topic, buildCtx.topicCapacity);

    buildCtx.payload[0] = '{';
    buildCtx.payload[1] = '\0';
    size_t pos = 1U;
    bool any = false;
    bool truncatedPayload = false;

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        char slotId[8] = {0};
        bool includeSlot = false;
        if (lockState_()) {
            const PoolDeviceSlot& s = slots_[i];
            includeSlot = s.used && (!runtimeReady_ || s.runtimePublishable);
            if (includeSlot) {
                snprintf(slotId, sizeof(slotId), "%s", s.id ? s.id : "");
            }
            unlockState_();
        }
        if (!includeSlot) continue;

        char moduleJson[640] = {0};
        bool truncatedModule = false;
        const bool hasAny = cfgStore_->toJsonModule(PoolDeviceSlots::kSlots[i].configModuleName,
                                                    moduleJson,
                                                    sizeof(moduleJson),
                                                    &truncatedModule);
        if (truncatedModule) {
            truncatedPayload = true;
            break;
        }
        if (!hasAny) continue;

        const char* prefix = any ? "," : "";
        const size_t prefixLen = strlen(prefix);
        const size_t idLen = strlen(slotId);
        const size_t jsonLen = strlen(moduleJson);
        const size_t needed = prefixLen + 1U + idLen + 2U + jsonLen;
        if (pos + needed + 1U > buildCtx.payloadCapacity) {
            truncatedPayload = true;
            break;
        }

        memcpy(buildCtx.payload + pos, prefix, prefixLen);
        pos += prefixLen;
        buildCtx.payload[pos++] = '"';
        memcpy(buildCtx.payload + pos, slotId, idLen);
        pos += idLen;
        buildCtx.payload[pos++] = '"';
        buildCtx.payload[pos++] = ':';
        memcpy(buildCtx.payload + pos, moduleJson, jsonLen);
        pos += jsonLen;
        buildCtx.payload[pos] = '\0';
        any = true;
    }

    if (truncatedPayload || pos + 2U > buildCtx.payloadCapacity) {
        if (!writeErrorJson(buildCtx.payload, buildCtx.payloadCapacity, ErrorCode::CfgTruncated, "cfg/pdm")) {
            snprintf(buildCtx.payload, buildCtx.payloadCapacity, "{\"ok\":false}");
        }
        buildCtx.topicLen = (uint16_t)topicLen;
        buildCtx.payloadLen = (uint16_t)strnlen(buildCtx.payload, buildCtx.payloadCapacity);
        buildCtx.qos = 1;
        buildCtx.retain = true;
        return MqttBuildResult::Ready;
    }

    if (!any) {
        LOGW("cfg base skipped: no data for %s", kPoolDeviceCfgTopicBase);
        return MqttBuildResult::NoLongerNeeded;
    }

    buildCtx.payload[pos++] = '}';
    buildCtx.payload[pos] = '\0';
    buildCtx.topicLen = (uint16_t)topicLen;
    buildCtx.payloadLen = (uint16_t)pos;
    buildCtx.qos = 1;
    buildCtx.retain = true;
    return MqttBuildResult::Ready;
}
