/**
 * @file ConfigStoreModule.cpp
 * @brief Implementation file.
 */
#include "ConfigStoreModule.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::ConfigStoreModule)
#include "Core/ModuleLog.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {
bool copyNvsKey_(char (&dst)[Limits::MaxNvsKeyLen + 1], const char* key)
{
    if (!key || key[0] == '\0') return false;
    const size_t len = strlen(key);
    if (len > Limits::MaxNvsKeyLen) return false;
    memcpy(dst, key, len + 1U);
    return true;
}

void appendModuleName_(char* out, size_t outLen, const char* moduleName)
{
    if (!out || outLen == 0U || !moduleName || moduleName[0] == '\0') return;
    const size_t used = strnlen(out, outLen);
    if (used >= outLen - 1U) return;

    size_t pos = used;
    if (pos > 0U) {
        if (pos + 2U >= outLen) return;
        out[pos++] = ',';
        out[pos++] = ' ';
    }

    for (const char* p = moduleName; *p != '\0' && pos + 1U < outLen; ++p) {
        const char c = *p;
        out[pos++] = (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    out[pos] = '\0';
}

uint16_t summarizePatch_(const char* json, char* modulesOut, size_t modulesOutLen)
{
    if (modulesOut && modulesOutLen > 0U) modulesOut[0] = '\0';
    if (!json || json[0] == '\0') return 0;

    DynamicJsonDocument doc(Limits::JsonConfigApplyBuf);
    const DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObjectConst>()) return 0;

    uint16_t fieldCount = 0;
    JsonObjectConst root = doc.as<JsonObjectConst>();
    for (JsonPairConst moduleKv : root) {
        const char* moduleName = moduleKv.key().c_str();
        JsonVariantConst moduleVar = moduleKv.value();
        if (!moduleVar.is<JsonObjectConst>()) continue;

        uint16_t moduleFieldCount = 0;
        JsonObjectConst moduleObj = moduleVar.as<JsonObjectConst>();
        for (JsonPairConst valueKv : moduleObj) {
            (void)valueKv;
            if (fieldCount < UINT16_MAX) ++fieldCount;
            if (moduleFieldCount < UINT16_MAX) ++moduleFieldCount;
        }
        if (moduleFieldCount > 0U) {
            appendModuleName_(modulesOut, modulesOutLen, moduleName);
        }
    }
    return fieldCount;
}
}

bool ConfigStoreModule::applyJson_(const char* json) {
    if (!registry) return false;
    const bool ok = registry->applyJson(json);
    if (ok) emitApplyJsonActivity_(json);
    return ok;
}

void ConfigStoreModule::emitApplyJsonActivity_(const char* json)
{
    if (!activityLog_ && services_) {
        activityLog_ = services_->get<ActivityLogService>(ServiceId::ActivityLog);
    }
    if (!activityLog_) return;
    if (!activityLog_->emit) return;

    char modules[72] = {0};
    const uint16_t fieldCount = summarizePatch_(json, modules, sizeof(modules));
    if (fieldCount == 0U) return;

    ActivityEvent event{};
    event.code = (uint16_t)ActivityCode::SystemConfigChanged;
    event.domain = (uint8_t)ActivityDomain::System;
    event.source = (uint8_t)ActivitySource::Manual;
    event.severity = (uint8_t)ActivitySeverity::Info;
    event.role = (uint8_t)ActivityRole::None;
    event.state = (uint8_t)ActivityState::None;
    event.reason = (uint8_t)ActivityReason::Manual;
    event.targetSlot = ACTIVITY_TARGET_NONE;
    snprintf(event.title, sizeof(event.title), "Configuration modifiée");
    if (modules[0] != '\0') {
        snprintf(event.detail,
                 sizeof(event.detail),
                 "Interface de configuration: %u champ(s) appliqué(s) dans %s.",
                 (unsigned)fieldCount,
                 modules);
    } else {
        snprintf(event.detail,
                 sizeof(event.detail),
                 "Interface de configuration: %u champ(s) appliqué(s).",
                 (unsigned)fieldCount);
    }
    snprintf(event.icon, sizeof(event.icon), "settings");
    (void)activityLog_->emit(activityLog_->ctx, &event);
}

void ConfigStoreModule::toJson_(char* out, size_t outLen) {
    if (!registry) return;
    registry->toJson(out, outLen);
}

bool ConfigStoreModule::toJsonModule_(const char* module, char* out, size_t outLen, bool* truncated) {
    return registry ? registry->toJsonModule(module, out, outLen, truncated) : false;
}

uint8_t ConfigStoreModule::listModules_(const char** out, uint8_t max) {
    return registry ? registry->listModules(out, max) : 0;
}

bool ConfigStoreModule::erase_() {
    return registry ? registry->erasePersistent() : false;
}

bool ConfigStoreModule::readRuntimeBlob_(const char* key, void* out, size_t outLen, size_t* actualLen) {
    return registry ? registry->readRuntimeBlob(key, out, outLen, actualLen) : false;
}

bool ConfigStoreModule::writeRuntimeBlob_(const char* key, const void* value, size_t len) {
    return registry ? registry->writeRuntimeBlob(key, value, len) : false;
}

bool ConfigStoreModule::eraseKey_(const char* key) {
    return registry ? registry->eraseKey(key) : false;
}

bool ConfigStoreModule::writeRuntimeBlobAsync_(const char* key, const void* value, size_t len) {
    if (!value || len == 0U || len > kPersistenceBlobMax) return false;
    PersistenceRequest req{};
    req.op = PersistenceOp::WriteBlob;
    req.len = (uint8_t)len;
    if (!copyNvsKey_(req.key, key)) return false;
    memcpy(req.bytes, value, len);
    return enqueuePersistence_(req);
}

bool ConfigStoreModule::eraseKeyAsync_(const char* key) {
    PersistenceRequest req{};
    req.op = PersistenceOp::EraseKey;
    if (!copyNvsKey_(req.key, key)) return false;
    return enqueuePersistence_(req);
}

bool ConfigStoreModule::persistFloatAsync_(const char* key,
                                           float value,
                                           const char* moduleName,
                                           uint8_t moduleId,
                                           uint8_t localBranchId) {
    PersistenceRequest req{};
    req.op = PersistenceOp::PersistFloat;
    req.floatValue = value;
    req.moduleId = moduleId;
    req.localBranchId = localBranchId;
    if (!copyNvsKey_(req.key, key)) return false;
    if (moduleName && moduleName[0] != '\0') {
        strncpy(req.moduleName, moduleName, sizeof(req.moduleName) - 1U);
        req.moduleName[sizeof(req.moduleName) - 1U] = '\0';
    }
    return enqueuePersistence_(req);
}

bool ConfigStoreModule::enqueuePersistence_(const PersistenceRequest& req) {
    if (!persistenceQ_) return false;
    const BaseType_t ok = xQueueSend(persistenceQ_, &req, 0);
    if (ok != pdTRUE) {
        LOGW("persistence queue full op=%u key=%s", (unsigned)req.op, req.key);
        return false;
    }
    return true;
}

void ConfigStoreModule::processPersistence_(const PersistenceRequest& req) {
    if (!registry) return;

    bool ok = false;
    switch (req.op) {
        case PersistenceOp::WriteBlob:
            ok = registry->writeRuntimeBlob(req.key, req.bytes, req.len);
            break;
        case PersistenceOp::EraseKey:
            ok = registry->eraseKey(req.key);
            break;
        case PersistenceOp::PersistFloat:
            ok = registry->persistFloatValue(req.key, req.floatValue);
            if (ok) {
                registry->notifyStoredValueChanged(req.key,
                                                   req.moduleName,
                                                   req.moduleId,
                                                   req.localBranchId);
            }
            break;
    }

    if (!ok) {
        LOGW("persistence op failed op=%u key=%s", (unsigned)req.op, req.key);
    }
}

void ConfigStoreModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    registry = &cfg;
    services_ = &services;
    if (!persistenceQ_) {
#if defined(FLOW_PROFILE_WAVESHARE)
        const size_t storageBytes = (size_t)kPersistenceQueueLen * sizeof(PersistenceRequest);
        persistenceQStorage_ = static_cast<uint8_t*>(
            heap_caps_malloc(storageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
        if (persistenceQStorage_) {
            persistenceQStorageInPsram_ = true;
        } else {
            persistenceQStorage_ = static_cast<uint8_t*>(
                heap_caps_malloc(storageBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
            );
        }
#else
        persistenceQStorage_ = persistenceQStorageLocal_;
#endif
        if (persistenceQStorage_) {
        persistenceQ_ = xQueueCreateStatic(kPersistenceQueueLen,
                                           sizeof(PersistenceRequest),
                                           persistenceQStorage_,
                                           &persistenceQStatic_);
        }
    }

    /// récupérer service loghub (log async)
    logHub = services.get<LogHubService>(ServiceId::LogHub);
    activityLog_ = services.get<ActivityLogService>(ServiceId::ActivityLog);

    if (!services.add(ServiceId::ConfigStore, &svc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::ConfigStore));
    }
    LOGI("ConfigStoreService registered persist_queue=%luB/%s",
         (unsigned long)((size_t)kPersistenceQueueLen * sizeof(PersistenceRequest)),
         persistenceQStorageInPsram_ ? "psram" : (persistenceQStorage_ ? "internal" : "none"));
}

void ConfigStoreModule::loop() {
    if (!persistenceQ_) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }

    PersistenceRequest req{};
    if (xQueueReceive(persistenceQ_, &req, portMAX_DELAY) == pdTRUE) {
        processPersistence_(req);
    }
}
