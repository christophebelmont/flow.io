#pragma once
/**
 * @file ConfigStore.h
 * @brief Persistent configuration store with JSON import/export.
 */

// ConfigStore = persistent configuration store (previously ConfigRegistry).
//
// This version integrates with the EventBus:
// - When a config value changes (set() or applyJson()), it posts an
//   EventId::ConfigChanged event.
//
// Design goals (ESP32):
// - no heap allocations in normal runtime path
// - post() to EventBus is thread-safe (queue-based EventBus)

#include <Preferences.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <type_traits>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ConfigTypes.h"
#include "Core/BufferUsageTracker.h"
#include "Core/NvsKeys.h"
#include "Core/Log.h"
#include "Core/SystemLimits.h"
#include "Core/Services/IEventBus.h"
#include "Core/EventBus/EventBus.h"
#include "Core/EventBus/EventPayloads.h"

/**
 * @brief Holds config variables, persistence, and JSON import/export.
 */
class ConfigStore {
public:
    static constexpr size_t JSON_BUFFER_SIZE = Limits::JsonCfgBuf;
    static constexpr size_t MAX_CONFIG_VARS = Limits::MaxConfigVars;

    //explicit ConfigStore(Preferences& prefs);
    ConfigStore() = default;

    /** @brief Inject EventBus dependency for change notifications. */
    void setEventBus(EventBus* bus) { _eventBus = bus; }
    /** @brief Inject Preferences for NVS persistence. */
    void setPreferences(Preferences& prefs) { _prefs = &prefs; }

    /** @brief Register a config variable definition. */
    template<typename T, size_t H>
    void registerVar(ConfigVariable<T, H>& var);
    /** @brief Register a config variable with explicit module/branch ids. */
    template<typename T, size_t H>
    void registerVar(ConfigVariable<T, H>& var, uint8_t moduleId, uint8_t localBranchId);

    /** @brief Set a typed config value and persist if needed. */
    template<typename T, size_t H>
    bool set(ConfigVariable<T, H>& var, const T& value);

    /** @brief Set a char array config value and persist if needed. */
    template<size_t H>
    bool set(ConfigVariable<char, H>& var, const char* str);

    /** @brief Load persistent values from NVS into registered variables. */
    void loadPersistent();
    /** @brief Load a single persistent variable from NVS immediately. */
    template<typename T, size_t H>
    void loadPersistentVar(ConfigVariable<T, H>& var);
    template<size_t H>
    void loadPersistentVar(ConfigVariable<char, H>& var);
    /** @brief Save persistent values from registered variables into NVS. */
    void savePersistent();
    /** @brief Erase all persistent keys in the active Preferences namespace. */
    bool erasePersistent();
    /** @brief Read a small runtime blob guarded by the ConfigStore NVS exclusion. */
    bool readRuntimeBlob(const char* key, void* out, size_t outLen, size_t* actualLen = nullptr);
    /** @brief Write a small runtime blob guarded by the ConfigStore NVS exclusion. */
    bool writeRuntimeBlob(const char* key, const void* value, size_t len);
    /** @brief Remove one key guarded by the ConfigStore NVS exclusion. */
    bool eraseKey(const char* key);
    /** @brief Persist one float key without mutating registered RAM state. */
    bool persistFloatValue(const char* key, float value);
    /** @brief Post a config-changed event for an already-mutated registered value. */
    void notifyStoredValueChanged(const char* nvsKey,
                                  const char* moduleName,
                                  uint8_t moduleId,
                                  uint8_t localBranchId);

    /** @brief Serialize all registered config to JSON. */
    void toJson(char* out, size_t outLen) const;
    /** @brief Serialize a single module's config (flat object). */
    bool toJsonModule(const char* module,
                      char* out,
                      size_t outLen,
                      bool* truncated = nullptr,
                      bool maskSecrets = true) const;
    /** @brief List unique module names present in config metadata. */
    uint8_t listModules(const char** out, uint8_t max) const;
    /** @brief Apply JSON patch to registered config variables. */
    bool applyJson(const char* json);

    /** @brief Log NVS write summary when the configured period elapsed. */
    void logNvsWriteSummaryIfDue(uint32_t nowMs, uint32_t periodMs = 60000U);

private:
    Preferences* _prefs = nullptr;
    EventBus* _eventBus = nullptr;
    ConfigMeta* _meta = nullptr;
    uint16_t _metaCount = 0;
    bool _metaNearCapacityWarned = false;

    bool ensureMetaStorage_();
    void notifyChanged(const char* nvsKey, const char* moduleName, uint8_t moduleId, uint8_t localBranchId);
    bool writePersistent(const ConfigMeta& m);
    void recordNvsWrite_(size_t bytesWritten);
    bool putInt_(const char* key, int32_t value);
    bool putUChar_(const char* key, uint8_t value);
    bool putUShort_(const char* key, uint16_t value);
    bool putBool_(const char* key, bool value);
    bool putFloat_(const char* key, float value);
    bool putBytes_(const char* key, const void* value, size_t len);
    bool putString_(const char* key, const char* value);
    void ensureMutex_();
    bool lockPrefs_();
    void unlockPrefs_();

    const ConfigMeta* findByJsonName(const char* jsonName) const;
    ConfigMeta* findByJsonName(const char* jsonName);

    std::atomic<uint32_t> _nvsWriteTotal{0};
    std::atomic<uint32_t> _nvsWriteWindow{0};
    std::atomic<uint32_t> _nvsLastSummaryMs{0};
    StaticSemaphore_t _prefsMutexBuf{};
    SemaphoreHandle_t _prefsMutex = nullptr;
};

// -------------------------
// Template implementation
// -------------------------
template<typename T, size_t H>
void ConfigStore::registerVar(ConfigVariable<T, H>& var)
{
    if (!ensureMetaStorage_()) {
        Log::error((LogModuleId)LogModuleIdValue::CoreConfigStore,
                   "Config meta storage unavailable, dropping module='%s' key='%s'",
                   var.moduleName ? var.moduleName : "?",
                   var.nvsKey ? var.nvsKey : "?");
        return;
    }
    if (_metaCount >= MAX_CONFIG_VARS) {
        Log::error((LogModuleId)LogModuleIdValue::CoreConfigStore,
                   "Config var capacity reached (%u), dropping module='%s' key='%s'",
                   (unsigned)MAX_CONFIG_VARS,
                   var.moduleName ? var.moduleName : "?",
                   var.nvsKey ? var.nvsKey : "?");
        return;
    }
    /*if (var.nvsKey && strlen(var.nvsKey) > MAX_NVS_KEY_LEN) {
        Log::warn((LogModuleId)LogModuleIdValue::CoreConfigStore, "NVS key too long (%s)", var.nvsKey);
        return;
    }*/

    // ✅ champ par champ (évite initializer list incompatible)
    ConfigMeta& m = _meta[_metaCount++];
    BufferUsageTracker::note(TrackedBufferId::ConfigMetaTable,
                             (size_t)_metaCount * sizeof(ConfigMeta),
                             MAX_CONFIG_VARS * sizeof(ConfigMeta),
                             var.moduleName,
                             var.jsonName);

    if (!_metaNearCapacityWarned) {
        const uint16_t warnMargin = 5U;
        const uint16_t warnAt = (MAX_CONFIG_VARS > warnMargin)
            ? (uint16_t)(MAX_CONFIG_VARS - warnMargin)
            : 0U;
        if (_metaCount >= warnAt) {
            _metaNearCapacityWarned = true;
            Log::warn((LogModuleId)LogModuleIdValue::CoreConfigStore,
                      "Config var capacity low headroom: %u/%u (remaining=%u)",
                      (unsigned)_metaCount,
                      (unsigned)MAX_CONFIG_VARS,
                      (unsigned)(MAX_CONFIG_VARS - _metaCount));
        }
    }

    m.module      = var.moduleName;
    m.name        = var.jsonName;
    m.nvsKey      = var.nvsKey;
    m.type        = var.type;
    m.persistence = var.persistence;
    m.valuePtr    = (void*)var.value;
    m.size        = var.size;
    m.moduleId    = var.moduleId;
    m.localBranchId = var.localBranchId;
}

template<typename T, size_t H>
void ConfigStore::registerVar(ConfigVariable<T, H>& var, uint8_t moduleId, uint8_t localBranchId)
{
    var.moduleId = moduleId;
    var.localBranchId = localBranchId;
    registerVar(var);
}

template<typename T, size_t H>
bool ConfigStore::set(ConfigVariable<T, H>& var, const T& value)
{
    if (!var.value) return false;

    bool changed = false;
    const T oldValue = *(var.value);

    // Comparaison selon type
    if constexpr (std::is_same<T, char>::value) {
        // cas char array géré ailleurs (normalement ConfigVariable<char,...>)
        changed = true;
    } else {
        if (*(var.value) != value) {
            *(var.value) = value;
            changed = true;
        }
    }

    if (!changed) return true;

    // persist
    if (var.persistence == ConfigPersistence::Persistent && var.nvsKey && _prefs) {
        bool persisted = true;
        switch (var.type) {
        case ConfigType::Int32:  persisted = putInt_(var.nvsKey, *(int32_t*)var.value); break;
        case ConfigType::UInt16: persisted = putUShort_(var.nvsKey, *(uint16_t*)var.value); break;
        case ConfigType::UInt8:  persisted = putUChar_(var.nvsKey, *(uint8_t*)var.value); break;
        case ConfigType::Bool:   persisted = putBool_(var.nvsKey, *(bool*)var.value); break;
        case ConfigType::Float:  persisted = putFloat_(var.nvsKey, *(float*)var.value); break;
        case ConfigType::Double: persisted = putBytes_(var.nvsKey, var.value, sizeof(double)); break;
        default: persisted = false; break;
        }
        if (!persisted) {
            *(var.value) = oldValue;
            return false;
        }
    }

    // notify handlers
    var.notify();

    // EventBus
    notifyChanged(var.nvsKey, var.moduleName, var.moduleId, var.localBranchId);

    return true;
}

template<typename T, size_t H>
void ConfigStore::loadPersistentVar(ConfigVariable<T, H>& var)
{
    if (!_prefs || !var.value || var.persistence != ConfigPersistence::Persistent || !var.nvsKey) return;

    if (!lockPrefs_()) return;
    switch (var.type) {
        case ConfigType::Int32:
            *(int32_t*)var.value = _prefs->getInt(var.nvsKey, *(int32_t*)var.value);
            break;
        case ConfigType::UInt16:
            *(uint16_t*)var.value = _prefs->getUShort(var.nvsKey, *(uint16_t*)var.value);
            break;
        case ConfigType::UInt8:
            *(uint8_t*)var.value = _prefs->getUChar(var.nvsKey, *(uint8_t*)var.value);
            break;
        case ConfigType::Bool:
            *(bool*)var.value = _prefs->getBool(var.nvsKey, *(bool*)var.value);
            break;
        case ConfigType::Float:
            *(float*)var.value = _prefs->getFloat(var.nvsKey, *(float*)var.value);
            break;
        case ConfigType::Double: {
            double tmp = *(double*)var.value;
            _prefs->getBytes(var.nvsKey, &tmp, sizeof(double));
            *(double*)var.value = tmp;
            break;
        }
        case ConfigType::CharArray:
        default:
            break;
    }
    unlockPrefs_();
}

template<size_t H>
bool ConfigStore::set(ConfigVariable<char, H>& var, const char* str)
{
    if (!var.value || !str || var.size == 0) return false;

    size_t len = strlen(str);
    if (len >= var.size) len = var.size - 1;

    bool changed = false;
    if (strncmp(var.value, str, len) != 0 || var.value[len] != '\0') {
        memcpy(var.value, str, len);
        var.value[len] = '\0';
        changed = true;
    }

    if (!changed) return true;

    if (var.persistence == ConfigPersistence::Persistent && var.nvsKey && _prefs) {
        if (!putString_(var.nvsKey, var.value)) {
            // Keep memory and NVS consistent when persistence fails.
            if (lockPrefs_()) {
                _prefs->getString(var.nvsKey, var.value, var.size);
                unlockPrefs_();
            }
            return false;
        }
    }

    var.notify();
    notifyChanged(var.nvsKey, var.moduleName, var.moduleId, var.localBranchId);
    return true;
}

template<size_t H>
void ConfigStore::loadPersistentVar(ConfigVariable<char, H>& var)
{
    if (!_prefs || !var.value || var.persistence != ConfigPersistence::Persistent || !var.nvsKey || var.size == 0) return;
    if (!lockPrefs_()) return;
    _prefs->getString(var.nvsKey, var.value, var.size);
    unlockPrefs_();
}
