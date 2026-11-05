/**
 * @file PoolDeviceCommands.cpp
 * @brief Command handlers for manual pool device actions.
 */

#include "PoolDeviceModule.h"
#include "Core/ErrorCodes.h"
#include "Domain/Pool/PoolIds.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolDeviceModule)
#include "Core/ModuleLog.h"
#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

namespace {
// Commands accept either a plain args object or a wrapped root payload.
bool parseCmdArgsObject_(const CommandRequest& req, JsonObjectConst& outObj)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::JsonCmdPoolDeviceBuf;
    static StaticJsonDocument<CMD_DOC_CAPACITY> doc;

    doc.clear();
    const char* json = req.args ? req.args : req.json;
    if (!json || json[0] == '\0') return false;

    const DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonObject>()) {
        outObj = doc.as<JsonObjectConst>();
        return true;
    }

    if (req.json && req.json[0] != '\0' && req.args != req.json) {
        doc.clear();
        const DeserializationError rootErr = deserializeJson(doc, req.json);
        if (rootErr || !doc.is<JsonObjectConst>()) return false;
        JsonVariantConst argsVar = doc["args"];
        if (argsVar.is<JsonObjectConst>()) {
            outObj = argsVar.as<JsonObjectConst>();
            return true;
        }
    }

    return false;
}

void writeCmdError_(char* reply, size_t replyLen, const char* where, ErrorCode code)
{
    if (!writeErrorJson(reply, replyLen, code, where)) {
        snprintf(reply, replyLen, "{\"ok\":false}");
    }
}

void writeCmdErrorSlot_(char* reply, size_t replyLen, const char* where, ErrorCode code, uint8_t slot)
{
    if (!writeErrorJsonWithSlot(reply, replyLen, code, where, slot)) {
        snprintf(reply, replyLen, "{\"ok\":false}");
    }
}

bool readConfigBool_(ConfigStore* cfgStore, const char* moduleName, const char* key, bool& out)
{
    if (!cfgStore || !moduleName || !key) return false;
    char json[192]{};
    bool truncated = false;
    if (!cfgStore->toJsonModule(moduleName, json, sizeof(json), &truncated) || truncated) return false;

    StaticJsonDocument<192> doc;
    const DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObjectConst>()) return false;
    JsonVariantConst value = doc.as<JsonObjectConst>()[key];
    if (!value.is<bool>()) return false;
    out = value.as<bool>();
    return true;
}

bool readConfigUInt8_(ConfigStore* cfgStore, const char* moduleName, const char* key, uint8_t& out)
{
    if (!cfgStore || !moduleName || !key) return false;
    char json[192]{};
    bool truncated = false;
    if (!cfgStore->toJsonModule(moduleName, json, sizeof(json), &truncated) || truncated) return false;

    StaticJsonDocument<192> doc;
    const DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObjectConst>()) return false;
    JsonVariantConst value = doc.as<JsonObjectConst>()[key];
    if (!value.is<uint8_t>()) return false;
    out = value.as<uint8_t>();
    return true;
}
} // namespace

bool PoolDeviceModule::cmdPoolWrite_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(userCtx);
    if (!self) return false;
    return self->handlePoolWrite_(req, reply, replyLen);
}

bool PoolDeviceModule::cmdPoolRefill_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(userCtx);
    if (!self) return false;
    return self->handlePoolRefill_(req, reply, replyLen);
}

bool PoolDeviceModule::cmdPoolResetUptime_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(userCtx);
    if (!self) return false;
    return self->handlePoolResetUptime_(req, reply, replyLen);
}

bool PoolDeviceModule::cmdPoolResetUptimeAll_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(userCtx);
    if (!self) return false;
    return self->handlePoolResetUptimeAll_(req, reply, replyLen);
}

ActivityRole PoolDeviceModule::activityRoleForSlot_(uint8_t slot) const
{
    if (slot == PoolIds::DeviceFiltrationPump) return ActivityRole::Filtration;
    if (slot == PoolIds::DeviceChlorineGenerator) return ActivityRole::Swg;
    if (slot == PoolIds::DeviceRobot) return ActivityRole::Robot;
    if (slot == PoolIds::DeviceFillPump) return ActivityRole::Filling;
    if (slot == PoolIds::DevicePhPump) return ActivityRole::Ph;
    if (slot == PoolIds::DeviceChlorinePump) return ActivityRole::Disinfection;
    if (slot == PoolIds::DeviceWaterHeater) return ActivityRole::Heater;
    return ActivityRole::None;
}

void PoolDeviceModule::emitActivity_(ActivityCode code,
                                     ActivitySource source,
                                     ActivitySeverity severity,
                                     ActivityRole role,
                                     ActivityState state,
                                     ActivityReason reason,
                                     uint8_t slot,
                                     const char* title,
                                     const char* detail,
                                     const char* icon) const
{
    if (!activityLogSvc_ || !activityLogSvc_->emit) return;
    ActivityEvent event{};
    event.code = (uint16_t)code;
    event.domain = (uint8_t)ActivityDomain::PoolDevice;
    event.source = (uint8_t)source;
    event.severity = (uint8_t)severity;
    event.role = (uint8_t)role;
    event.state = (uint8_t)state;
    event.reason = (uint8_t)reason;
    event.targetSlot = slot;
    snprintf(event.title, sizeof(event.title), "%s", title ? title : "Action piscine");
    snprintf(event.detail, sizeof(event.detail), "%s", detail ? detail : "");
    snprintf(event.icon, sizeof(event.icon), "%s", icon ? icon : "touch_app");
    (void)activityLogSvc_->emit(activityLogSvc_->ctx, &event);
}

void PoolDeviceModule::emitAutoModeDisabledByManualActivity_(ActivityRole role,
                                                             uint8_t slot,
                                                             const char* autoLabel) const
{
    const char* roleLabel = "équipement";
    const char* icon = "settings";
    if (role == ActivityRole::Ph) {
        roleLabel = "pompe pH";
        icon = "science";
    } else if (role == ActivityRole::Disinfection) {
        roleLabel = "pompe ORP";
        icon = "science";
    } else if (role == ActivityRole::Filtration) {
        roleLabel = "filtration";
        icon = "pool";
    }

    char title[48] = {0};
    char detail[128] = {0};
    snprintf(title, sizeof(title), "Mode auto %s désactivé", autoLabel ? autoLabel : roleLabel);
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
                  slot,
                  title,
                  detail,
                  icon);
}

bool PoolDeviceModule::handlePoolWrite_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingArgs);
        return false;
    }

    if (!args.containsKey("slot")) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingSlot);
        return false;
    }
    if (!args["slot"].is<uint8_t>()) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::BadSlot);
        return false;
    }
    const uint8_t slot = args["slot"].as<uint8_t>();
    if (slot >= POOL_DEVICE_MAX) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::BadSlot);
        return false;
    }

    if (!args.containsKey("value")) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingValue);
        return false;
    }

    JsonVariantConst value = args["value"];
    bool requested = false;
    if (value.is<bool>()) {
        requested = value.as<bool>();
    } else if (value.is<int32_t>() || value.is<uint32_t>() || value.is<float>()) {
        requested = (value.as<float>() != 0.0f);
    } else if (value.is<const char*>()) {
        const char* s = value.as<const char*>();
        if (!s) s = "0";
        if (strcmp(s, "true") == 0) requested = true;
        else if (strcmp(s, "false") == 0) requested = false;
        else requested = (atoi(s) != 0);
    } else {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingValue);
        return false;
    }

    const PoolDeviceSvcStatus st = svcWriteDesiredImpl_(slot, requested ? 1U : 0U);
    if (st != POOLDEV_SVC_OK) {
        ErrorCode code = ErrorCode::Failed;
        if (st == POOLDEV_SVC_ERR_UNKNOWN_SLOT) code = ErrorCode::UnknownSlot;
        else if (st == POOLDEV_SVC_ERR_NOT_READY) code = ErrorCode::NotReady;
        else if (st == POOLDEV_SVC_ERR_DISABLED) code = ErrorCode::Disabled;
        else if (st == POOLDEV_SVC_ERR_INTERLOCK) code = ErrorCode::InterlockBlocked;
        else if (st == POOLDEV_SVC_ERR_MAX_UPTIME) code = ErrorCode::MaxUptimeReached;
        else if (st == POOLDEV_SVC_ERR_IO) code = ErrorCode::IoError;
        writeCmdErrorSlot_(reply, replyLen, "pooldevice.write", code, slot);
        return false;
    }

    // Global business rule for manual starts:
    // if a dosing pump is manually forced ON, disable the corresponding auto mode
    // regardless of the command source (HMI, MQTT, Web, ...).
    if (requested && cfgStore_) {
        uint8_t phPumpSlot = PoolIds::DevicePhPump;
        uint8_t orpPumpSlot = PoolIds::DeviceChlorinePump;

        char modeJson[160]{};
        bool truncated = false;
        if (cfgStore_->toJsonModule("poollogic/devices", modeJson, sizeof(modeJson), &truncated) && !truncated) {
            StaticJsonDocument<192> modeDoc;
            const DeserializationError modeErr = deserializeJson(modeDoc, modeJson);
            if (!modeErr && modeDoc.is<JsonObjectConst>()) {
                const JsonObjectConst obj = modeDoc.as<JsonObjectConst>();
                if (obj["ph_pump_slot"].is<uint16_t>()) {
                    const uint16_t v = obj["ph_pump_slot"].as<uint16_t>();
                    if (v < POOL_DEVICE_MAX) phPumpSlot = (uint8_t)v;
                }
                if (obj["dis_pump_slot"].is<uint16_t>()) {
                    const uint16_t v = obj["dis_pump_slot"].as<uint16_t>();
                    if (v < POOL_DEVICE_MAX) orpPumpSlot = (uint8_t)v;
                }
            }
        }

        const char* modeKey = nullptr;
        if (slot == phPumpSlot) modeKey = "ph_auto_mode";
        else if (slot == orpPumpSlot) modeKey = "disinfection_type";

        if (modeKey) {
            bool shouldLogAutoDisabled = false;
            ActivityRole disabledRole = ActivityRole::None;
            const char* disabledLabel = nullptr;
            char patch[96]{};
            if (strcmp(modeKey, "disinfection_type") == 0) {
                uint8_t disinfectionType = 0;
                shouldLogAutoDisabled = !readConfigUInt8_(cfgStore_,
                                                           "poollogic/modes",
                                                           "disinfection_type",
                                                           disinfectionType) ||
                                        disinfectionType != 3U;
                disabledRole = ActivityRole::Disinfection;
                disabledLabel = "ORP";
                snprintf(patch, sizeof(patch), "{\"poollogic/modes\":{\"disinfection_type\":3}}");
            } else if (strcmp(modeKey, "ph_auto_mode") == 0) {
                bool phAutoMode = false;
                shouldLogAutoDisabled = !readConfigBool_(cfgStore_,
                                                         "poollogic/ph",
                                                         "ph_auto_mode",
                                                         phAutoMode) ||
                                        phAutoMode;
                disabledRole = ActivityRole::Ph;
                disabledLabel = "pH";
                snprintf(patch, sizeof(patch), "{\"poollogic/ph\":{\"ph_auto_mode\":false}}");
            } else {
                snprintf(patch, sizeof(patch), "{\"poollogic/modes\":{\"%s\":false}}", modeKey);
            }
            if (!cfgStore_->applyJson(patch)) {
                LOGW("Manual pump start slot=%u failed to clear %s",
                     (unsigned)slot,
                     modeKey);
            } else {
                if (strcmp(modeKey, "disinfection_type") == 0) {
                    const PoolDeviceSvcStatus rest = svcWriteDesiredImpl_(slot, 1U);
                    if (rest != POOLDEV_SVC_OK) {
                        writeCmdErrorSlot_(reply, replyLen, "pooldevice.write", ErrorCode::Failed, slot);
                        return false;
                    }
                    LOGI("Manual pump start slot=%u -> disinfection_type=3",
                         (unsigned)slot);
                } else {
                    LOGI("Manual pump start slot=%u -> %s=false",
                         (unsigned)slot,
                         modeKey);
                }
                if (shouldLogAutoDisabled) {
                    emitAutoModeDisabledByManualActivity_(disabledRole, slot, disabledLabel);
                }
            }
        }
    }

    char label[sizeof(PoolDeviceDefinition::label)] = {0};
    if (lockState_()) {
        if (slots_[slot].used) {
            snprintf(label,
                     sizeof(label),
                     "%s",
                     slots_[slot].def.label[0] ? slots_[slot].def.label : (slots_[slot].id ? slots_[slot].id : ""));
        }
        unlockState_();
    }
    LOGI("Manual %s %s (slot=%u)",
         requested ? "Start" : "Stop",
         label[0] ? label : "Pool Device",
         (unsigned)slot);
    char title[48] = {0};
    char detail[128] = {0};
    const char* display = label[0] ? label : "Équipement piscine";
    snprintf(title, sizeof(title), "%s %s", display, requested ? "démarré manuellement" : "arrêté manuellement");
    snprintf(detail, sizeof(detail), "Commande manuelle appliquée au slot %u.", (unsigned)slot);
    emitActivity_(requested ? ActivityCode::PoolDeviceManualStart : ActivityCode::PoolDeviceManualStop,
                  ActivitySource::Manual,
                  requested ? ActivitySeverity::Success : ActivitySeverity::Info,
                  activityRoleForSlot_(slot),
                  requested ? ActivityState::RequestedOn : ActivityState::RequestedOff,
                  ActivityReason::Manual,
                  slot,
                  title,
                  detail,
                  requested ? "play_arrow" : "stop");
    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u}", (unsigned)slot);
    return true;
}

bool PoolDeviceModule::handlePoolRefill_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::MissingArgs);
        return false;
    }

    if (!args.containsKey("slot")) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::MissingSlot);
        return false;
    }
    if (!args["slot"].is<uint8_t>()) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::BadSlot);
        return false;
    }
    const uint8_t slot = args["slot"].as<uint8_t>();
    if (slot >= POOL_DEVICE_MAX) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::BadSlot);
        return false;
    }

    float remaining = 0.0f;
    if (!lockState_()) {
        writeCmdErrorSlot_(reply, replyLen, "pool.refill", ErrorCode::NotReady, slot);
        return false;
    }
    if (!slots_[slot].used) {
        unlockState_();
        writeCmdErrorSlot_(reply, replyLen, "pool.refill", ErrorCode::UnknownSlot, slot);
        return false;
    }
    remaining = slots_[slot].def.tankCapacityMl;
    unlockState_();

    if (args.containsKey("remaining_ml")) {
        JsonVariantConst rem = args["remaining_ml"];
        if (rem.is<float>() || rem.is<double>() || rem.is<int32_t>() || rem.is<uint32_t>()) {
            remaining = rem.as<float>();
        } else if (rem.is<const char*>()) {
            const char* s = rem.as<const char*>();
            remaining = s ? (float)atof(s) : remaining;
        }
    }

    const PoolDeviceSvcStatus st = svcRefillTankImpl_(slot, remaining);
    if (st != POOLDEV_SVC_OK) {
        const ErrorCode code = (st == POOLDEV_SVC_ERR_UNKNOWN_SLOT) ? ErrorCode::UnknownSlot : ErrorCode::Failed;
        writeCmdErrorSlot_(reply, replyLen, "pool.refill", code, slot);
        return false;
    }

    float applied = 0.0f;
    char label[sizeof(PoolDeviceDefinition::label)] = {0};
    if (lockState_()) {
        if (slots_[slot].used) {
            applied = slots_[slot].tankRemainingMl;
            snprintf(label,
                     sizeof(label),
                     "%s",
                     slots_[slot].def.label[0] ? slots_[slot].def.label : (slots_[slot].id ? slots_[slot].id : ""));
        }
        unlockState_();
    }
    char title[48] = {0};
    char detail[128] = {0};
    snprintf(title,
             sizeof(title),
             "%s rempli",
             label[0] ? label : "Réservoir");
    snprintf(detail,
             sizeof(detail),
             "Niveau réservoir mis à %.1f ml pour le slot %u.",
             (double)applied,
             (unsigned)slot);
    emitActivity_(ActivityCode::PoolDeviceTankRefill,
                  ActivitySource::Manual,
                  ActivitySeverity::Success,
                  activityRoleForSlot_(slot),
                  ActivityState::None,
                  ActivityReason::Manual,
                  slot,
                  title,
                  detail,
                  "water_drop");
    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u,\"remaining_ml\":%.1f}", (unsigned)slot, (double)applied);
    return true;
}

bool PoolDeviceModule::handlePoolResetUptime_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "pool.uptime.reset", ErrorCode::MissingArgs);
        return false;
    }

    if (!args.containsKey("slot")) {
        writeCmdError_(reply, replyLen, "pool.uptime.reset", ErrorCode::MissingSlot);
        return false;
    }
    if (!args["slot"].is<uint8_t>()) {
        writeCmdError_(reply, replyLen, "pool.uptime.reset", ErrorCode::BadSlot);
        return false;
    }

    const uint8_t slot = args["slot"].as<uint8_t>();
    if (slot >= POOL_DEVICE_MAX) {
        writeCmdError_(reply, replyLen, "pool.uptime.reset", ErrorCode::BadSlot);
        return false;
    }
    if (!resetUptimeSlot_(slot)) {
        writeCmdErrorSlot_(reply, replyLen, "pool.uptime.reset", ErrorCode::UnknownSlot, slot);
        return false;
    }

    char label[sizeof(PoolDeviceDefinition::label)] = {0};
    if (lockState_()) {
        tickDevices_(millis(), false);
        if (slots_[slot].used) {
            snprintf(label,
                     sizeof(label),
                     "%s",
                     slots_[slot].def.label[0] ? slots_[slot].def.label : (slots_[slot].id ? slots_[slot].id : ""));
        }
        unlockState_();
    }
    LOGI("Reset uptime %s (slot=%u)",
         label[0] ? label : "Pool Device",
         (unsigned)slot);
    char title[48] = {0};
    char detail[128] = {0};
    snprintf(title,
             sizeof(title),
             "Compteurs %s remis à zéro",
             label[0] ? label : "équipement");
    snprintf(detail, sizeof(detail), "Compteurs journaliers/hebdomadaires/mensuels remis à zéro pour le slot %u.", (unsigned)slot);
    emitActivity_(ActivityCode::PoolDeviceUptimeReset,
                  ActivitySource::Manual,
                  ActivitySeverity::Info,
                  activityRoleForSlot_(slot),
                  ActivityState::None,
                  ActivityReason::Manual,
                  slot,
                  title,
                  detail,
                  "restart_alt");
    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u}", (unsigned)slot);
    return true;
}

bool PoolDeviceModule::handlePoolResetUptimeAll_(const CommandRequest&, char* reply, size_t replyLen)
{
    const uint8_t resetCount = resetUptimeAll_();
    if (lockState_()) {
        tickDevices_(millis(), false);
        unlockState_();
    }
    LOGI("Reset uptime all (count=%u)", (unsigned)resetCount);
    char detail[96] = {0};
    snprintf(detail, sizeof(detail), "%u équipement(s) remis à zéro.", (unsigned)resetCount);
    emitActivity_(ActivityCode::PoolDeviceUptimeReset,
                  ActivitySource::Manual,
                  ActivitySeverity::Info,
                  ActivityRole::None,
                  ActivityState::None,
                  ActivityReason::Manual,
                  ACTIVITY_TARGET_NONE,
                  "Tous les compteurs piscine remis à zéro",
                  detail,
                  "restart_alt");
    snprintf(reply, replyLen, "{\"ok\":true,\"reset\":%u}", (unsigned)resetCount);
    return true;
}

bool PoolDeviceModule::resetUptimeSlot_(uint8_t slot)
{
    if (!lockState_()) return false;
    if (slot >= POOL_DEVICE_MAX) {
        unlockState_();
        return false;
    }
    PoolDeviceSlot& s = slots_[slot];
    if (!s.used) {
        unlockState_();
        return false;
    }

    PeriodKeys keys{};
    const bool hasKeys = currentPeriodKeys_(keys);

    s.runningMsDay = 0;
    s.runningMsWeek = 0;
    s.runningMsMonth = 0;
    s.injectedMlDay = 0.0f;
    s.injectedMlWeek = 0.0f;
    s.injectedMlMonth = 0.0f;
    if (hasKeys) {
        s.dayKey = keys.day;
        s.weekKey = keys.week;
        s.monthKey = keys.month;
    }

    // Clear local max-uptime latch immediately; tickDevices_ will re-evaluate
    // interlocks and publish updated runtime state/metrics right away.
    if (s.blockReason == POOL_DEVICE_BLOCK_MAX_UPTIME) {
        s.blockReason = POOL_DEVICE_BLOCK_NONE;
    }
    s.forceMetricsCommit = true;
    s.persistDirty = true;
    s.persistImmediate = true;
    unlockState_();
    return true;
}

uint8_t PoolDeviceModule::resetUptimeAll_()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (resetUptimeSlot_(i)) ++count;
    }
    return count;
}
