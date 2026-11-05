/**
 * @file PoolLogicRuntime.cpp
 * @brief Runtime and config publication helpers for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Core/CommandRegistry.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"

#include <Arduino.h>
#include <cstring>
#include <stdio.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

namespace {
// The aggregated cfg payload republishes the same branch split used by the
// config routes so MQTT consumers can fetch one coherent snapshot.
static constexpr const char* kPoolLogicCfgTopicBase = "cfg/poollogic";
static constexpr const char* kCfgModuleModes = "poollogic/modes";
static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
static constexpr const char* kCfgModuleSafety = "poollogic/safety";
static constexpr const char* kCfgModuleRegulation = "poollogic/regulation";
static constexpr const char* kCfgModulePh = "poollogic/ph";
static constexpr const char* kCfgModuleChlorine = "poollogic/chlorine";
static constexpr const char* kCfgModuleSwg = "poollogic/swg";
static constexpr const char* kCfgModuleO2 = "poollogic/o2";
static constexpr const char* kCfgModuleDevices = "poollogic/devices";
static constexpr const char* kCfgModuleHeater = "poollogic/heater";
static constexpr const char* kCfgModuleRobot = "poollogic/robot";
static constexpr const char* kCfgModuleRefill = "poollogic/refill";
}

MqttBuildResult PoolLogicModule::buildCfgBaseStatic_(void* ctx, uint16_t, MqttBuildContext& buildCtx)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    return self ? self->buildCfgBase_(buildCtx) : MqttBuildResult::PermanentError;
}

MqttBuildResult PoolLogicModule::buildCfgBase_(MqttBuildContext& buildCtx)
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
                                                     kPoolLogicCfgTopicBase,
                                                     "",
                                                     topicLen)) {
        return MqttBuildResult::PermanentError;
    }
    mqttSvc_->formatTopic(mqttSvc_->ctx, relativeTopic, buildCtx.topic, buildCtx.topicCapacity);
    if (buildCtx.topic[0] == '\0') return MqttBuildResult::PermanentError;
    topicLen = strnlen(buildCtx.topic, buildCtx.topicCapacity);

    struct Entry {
        const char* key;
        const char* moduleName;
    };
    static constexpr Entry kEntries[] = {
        {"modes", kCfgModuleModes},
        {"filtration", kCfgModuleFiltration},
        {"sensors", kCfgModuleSensors},
        {"safety", kCfgModuleSafety},
        {"regulation", kCfgModuleRegulation},
        {"ph", kCfgModulePh},
        {"chlorine", kCfgModuleChlorine},
        {"swg", kCfgModuleSwg},
        {"o2", kCfgModuleO2},
        {"devices", kCfgModuleDevices},
        {"heater", kCfgModuleHeater},
        {"robot", kCfgModuleRobot},
        {"refill", kCfgModuleRefill},
    };

    buildCtx.payload[0] = '{';
    buildCtx.payload[1] = '\0';
    size_t pos = 1U;
    bool any = false;
    bool truncatedPayload = false;

    // Rebuild the aggregate JSON from per-branch module snapshots so the
    // payload stays a pure projection of the config store.
    for (uint8_t i = 0; i < (uint8_t)(sizeof(kEntries) / sizeof(kEntries[0])); ++i) {
        char moduleJson[640] = {0};
        bool truncatedModule = false;
        const bool hasAny = cfgStore_->toJsonModule(kEntries[i].moduleName,
                                                    moduleJson,
                                                    sizeof(moduleJson),
                                                    &truncatedModule);
        if (truncatedModule) {
            truncatedPayload = true;
            break;
        }
        if (!hasAny) continue;

        const int w = snprintf(buildCtx.payload + pos,
                               buildCtx.payloadCapacity - pos,
                               "%s\"%s\":%s",
                               any ? "," : "",
                               kEntries[i].key,
                               moduleJson);
        if (!(w > 0 && (size_t)w < (buildCtx.payloadCapacity - pos))) {
            truncatedPayload = true;
            break;
        }
        pos += (size_t)w;
        any = true;
    }

    if (truncatedPayload || pos + 2U > buildCtx.payloadCapacity) {
        if (!writeErrorJson(buildCtx.payload, buildCtx.payloadCapacity, ErrorCode::CfgTruncated, "cfg/poollogic")) {
            snprintf(buildCtx.payload, buildCtx.payloadCapacity, "{\"ok\":false}");
        }
        buildCtx.topicLen = (uint16_t)topicLen;
        buildCtx.payloadLen = (uint16_t)strnlen(buildCtx.payload, buildCtx.payloadCapacity);
        buildCtx.qos = 1;
        buildCtx.retain = true;
        return MqttBuildResult::Ready;
    }

    if (!any) {
        LOGW("cfg base skipped: no data for %s", kPoolLogicCfgTopicBase);
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

uint8_t PoolLogicModule::runtimeSnapshotCount() const
{
    return 4;
}

bool PoolLogicModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);

    switch (valueId) {
        case RuntimeUiAutoMode:
            return writer.writeBool(runtimeId, autoMode_);
        case RuntimeUiWinterMode:
            return writer.writeBool(runtimeId, winterMode_);
        case RuntimeUiPhAutoMode:
            return writer.writeBool(runtimeId, phAutoMode_);
        case RuntimeUiOrpAutoMode:
            return writer.writeBool(runtimeId, orpAutoMode_);
        default:
            return false;
    }
}

const char* PoolLogicModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    if (idx == 0) return "rt/poollogic/ph";
    if (idx == 1) return "rt/poollogic/orp";
    if (idx == 2) return "rt/poollogic/heat_assist";
    if (idx == 3) return "rt/poollogic/disinfection";
    return nullptr;
}

RuntimeRouteClass PoolLogicModule::runtimeSnapshotClass(uint8_t idx) const
{
    (void)idx;
    return RuntimeRouteClass::NumericThrottled;
}

bool PoolLogicModule::runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const
{
    if (idx > 3) return false;
    if (key >= DATAKEY_IO_BASE && key < (DataKey)(DATAKEY_IO_BASE + IO_MAX_ENDPOINTS)) return true;
    if (key >= DATAKEY_POOL_DEVICE_STATE_BASE &&
        key < (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + POOL_DEVICE_MAX)) return true;
    return false;
}

bool PoolLogicModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;

    const uint32_t nowMs = millis();
    if (idx == 3) {
        const int wrote = snprintf(out,
                                   len,
                                   "{\"dt\":%u,\"dts\":\"%s\",\"swgm\":%u,\"swgms\":\"%s\","
                                   "\"o2\":{\"state\":%u,\"state_s\":\"%s\",\"block\":%u,\"block_s\":\"%s\","
                                   "\"last_day\":%u,\"done_ml\":%.1f,\"pending_ml\":%.1f,"
                                   "\"plan_ml\":%.1f,\"flow_l_h\":%.2f},"
                                   "\"t\":%lu}",
                                   (unsigned)disinfectionType_,
                                   disinfectionTypeStr_(disinfectionType_),
                                   (unsigned)swgControlMode_,
                                   swgControlModeStr_(swgControlMode_),
                                   (unsigned)o2ProtocolState_,
                                   o2ProtocolStateStr_(o2ProtocolState_),
                                   (unsigned)o2BlockReason_,
                                   o2BlockReasonStr_(o2BlockReason_),
                                   (unsigned)o2LastDoseDay_,
                                   (double)o2WeeklyDoneMl_,
                                   (double)o2PendingMl_,
                                   (double)o2LastPlannedDoseMl_,
                                   (double)o2LastFlowLh_,
                                   (unsigned long)nowMs);
        if (wrote < 0 || (size_t)wrote >= len) return false;
        maxTsOut = nowMs ? nowMs : 1U;
        return true;
    }

    if (idx == 2) {
        constexpr uint8_t kHeatAssistFlagProbeRunning = (1U << 0);
        constexpr uint8_t kHeatAssistFlagHeatingActive = (1U << 1);
#if !defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
        constexpr uint8_t kHeatAssistFlagFastCycle = (1U << 2);
#endif
        const bool probeRunning = (heatAssistFlags_ & kHeatAssistFlagProbeRunning) != 0U;
        const bool heatingActive = (heatAssistFlags_ & kHeatAssistFlagHeatingActive) != 0U;
#if !defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
        const bool fastCycle = (heatAssistFlags_ & kHeatAssistFlagFastCycle) != 0U;
#endif
        const uint16_t nowSec = (uint16_t)((nowMs / 1000UL) & 0xFFFFU);
        const uint16_t probeStartSec = (uint16_t)(heatAssistTimingPacked_ & 0xFFFFU);
        const uint16_t lastProbeEndSec = (uint16_t)((heatAssistTimingPacked_ >> 16) & 0xFFFFU);
        const auto reasonToStr = [this]() -> const char* {
            switch (heatAssistReason_) {
                case HeatAssistReason::Disabled: return "DISABLED";
                case HeatAssistReason::ManualMode: return "MANUAL_MODE";
                case HeatAssistReason::PsiBlocked: return "PSI_BLOCKED";
                case HeatAssistReason::SetpointInvalid: return "SETPOINT_INVALID";
                case HeatAssistReason::TempUnavailable: return "TEMP_UNAVAILABLE";
                case HeatAssistReason::ProbeWait30m: return "PROBE_WAIT_30M";
                case HeatAssistReason::ProbeWait20m: return "PROBE_WAIT_20M";
                case HeatAssistReason::ProbeWaitAdaptive: return "PROBE_WAIT_ADAPTIVE";
                case HeatAssistReason::ProbeRunning: return "PROBE_RUNNING";
                case HeatAssistReason::Heating: return "HEATING";
                case HeatAssistReason::IdlePumpOn: return "IDLE_PUMP_ON";
                case HeatAssistReason::SetpointReached: return "SETPOINT_REACHED";
                default: return "UNKNOWN";
            }
        };
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
        const uint32_t measurementIntervalMin = heatAssistIntervalMin_;
        const uint32_t measurementIntervalSec = measurementIntervalMin * 60U;
        const uint32_t idleIntervalSec =
            (measurementIntervalSec > 5U * 60U) ? (measurementIntervalSec - 5U * 60U) : 0U;
#else
        const uint32_t measurementIntervalMin = fastCycle ? 20U : 30U;
        const uint32_t idleIntervalSec = measurementIntervalMin * 60U;
#endif
        const uint32_t probeRemainMs = probeRunning
                                           ? ((((uint16_t)(nowSec - probeStartSec)) >= 5U * 60U)
                                                  ? 0U
                                                  : ((5U * 60U) - (uint32_t)((uint16_t)(nowSec - probeStartSec))) * 1000U)
                                           : 0U;
        const uint32_t idleElapsedSec = (lastProbeEndSec == 0U) ? idleIntervalSec : (uint32_t)((uint16_t)(nowSec - lastProbeEndSec));
        const uint32_t idleRemainMs = probeRunning
                                          ? 0U
                                          : ((idleElapsedSec >= idleIntervalSec) ? 0U : (idleIntervalSec - idleElapsedSec) * 1000U);
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
        const int wrote = snprintf(out,
                                   len,
                                   "{\"en\":%s,\"pr\":%s,\"ha\":%s,\"fc\":false,"
                                   "\"ri\":\"%s\",\"prm\":%lu,\"irm\":%lu,\"t\":%lu}",
                                   (autoMode_ && heaterAutoMode_) ? "true" : "false",
                                   probeRunning ? "true" : "false",
                                   heatingActive ? "true" : "false",
                                   reasonToStr(),
                                   (unsigned long)probeRemainMs,
                                   (unsigned long)idleRemainMs,
                                   (unsigned long)nowMs);
#else
        const int wrote = snprintf(out,
                                   len,
                                   "{\"en\":%s,\"pr\":%s,\"ha\":%s,\"fc\":%s,"
                                   "\"ri\":\"%s\",\"prm\":%lu,\"irm\":%lu,\"t\":%lu}",
                                   (autoMode_ && heaterAutoMode_) ? "true" : "false",
                                   probeRunning ? "true" : "false",
                                   heatingActive ? "true" : "false",
                                   fastCycle ? "true" : "false",
                                   reasonToStr(),
                                   (unsigned long)probeRemainMs,
                                   (unsigned long)idleRemainMs,
                                   (unsigned long)nowMs);
#endif
        if (wrote < 0 || (size_t)wrote >= len) return false;
        maxTsOut = nowMs ? nowMs : 1U;
        return true;
    }

    const bool isPh = (idx == 0);
    const bool isOrp = (idx == 1);
    if (!isPh && !isOrp) return false;

    const TemporalPidState& st = isPh ? phPidState_ : orpPidState_;
    const DeviceFsm& pumpFsm = isPh ? phPumpFsm_ : orpPumpFsm_;
    const float kp = isPh ? phKp_ : orpKp_;
    const float ki = isPh ? phKi_ : orpKi_;
    const float kd = isPh ? phKd_ : orpKd_;
    const int32_t windowMsCfg = isPh ? phWindowMs_ : orpWindowMs_;
    const uint32_t windowMs = (windowMsCfg > 1000) ? (uint32_t)windowMsCfg : 1000U;

    uint32_t elapsedMs = 0;
    if (st.initialized) {
        const uint32_t rawElapsed = nowMs - st.windowStartMs;
        elapsedMs = (rawElapsed < windowMs) ? rawElapsed : windowMs;
    }

    // Runtime snapshots expose the last PID sample plus the derived on-window
    // so MQTT/UI consumers can inspect why dosing is currently active or idle.
    const bool regulationEnabled = isPh ? phPidEnabled_ : orpPidEnabled_;
    int wrote = 0;
    if (st.sampleValid) {
        wrote = snprintf(
            out, len,
            "{\"i\":\"%s\",\"in\":%.3f,\"sp\":%.3f,\"er\":%.3f,"
            "\"en\":%s,\"dm\":%s,\"ac\":%s,"
            "\"kp\":%.6f,\"ki\":%.6f,\"kd\":%.6f,"
            "\"w\":%ld,\"sm\":%ld,\"mo\":%ld,"
            "\"on\":%lu,\"we\":%lu,\"ct\":%lu,"
            "\"dt\":%u,\"dts\":\"%s\",\"swgm\":%u,\"swgms\":\"%s\",\"t\":%lu}",
            isPh ? "ph" : "orp",
            (double)st.sampleInput,
            (double)st.sampleSetpoint,
            (double)st.sampleError,
            regulationEnabled ? "true" : "false",
            st.lastDemandOn ? "true" : "false",
            pumpFsm.on ? "true" : "false",
            (double)kp,
            (double)ki,
            (double)kd,
            (long)windowMsCfg,
            (long)pidSampleMs_,
            (long)pidMinOnMs_,
            (unsigned long)st.outputOnMs,
            (unsigned long)elapsedMs,
            (unsigned long)st.sampleTsMs,
            (unsigned)disinfectionType_,
            disinfectionTypeStr_(disinfectionType_),
            (unsigned)swgControlMode_,
            swgControlModeStr_(swgControlMode_),
            (unsigned long)nowMs
        );
    } else {
        wrote = snprintf(
            out, len,
            "{\"i\":\"%s\",\"in\":null,\"sp\":%.3f,\"er\":null,"
            "\"en\":%s,\"dm\":%s,\"ac\":%s,"
            "\"kp\":%.6f,\"ki\":%.6f,\"kd\":%.6f,"
            "\"w\":%ld,\"sm\":%ld,\"mo\":%ld,"
            "\"on\":%lu,\"we\":%lu,\"ct\":0,"
            "\"dt\":%u,\"dts\":\"%s\",\"swgm\":%u,\"swgms\":\"%s\",\"t\":%lu}",
            isPh ? "ph" : "orp",
            (double)(isPh ? phSetpoint_ : orpSetpoint_),
            regulationEnabled ? "true" : "false",
            st.lastDemandOn ? "true" : "false",
            pumpFsm.on ? "true" : "false",
            (double)kp,
            (double)ki,
            (double)kd,
            (long)windowMsCfg,
            (long)pidSampleMs_,
            (long)pidMinOnMs_,
            (unsigned long)st.outputOnMs,
            (unsigned long)elapsedMs,
            (unsigned)disinfectionType_,
            disinfectionTypeStr_(disinfectionType_),
            (unsigned)swgControlMode_,
            swgControlModeStr_(swgControlMode_),
            (unsigned long)nowMs
        );
    }
    if (wrote < 0 || (size_t)wrote >= len) return false;

    maxTsOut = st.runtimeTsMs ? st.runtimeTsMs : 1U;
    return true;
}
