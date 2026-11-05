/**
 * @file HAModule.cpp
 * @brief Implementation file.
 */

#include "HAModule.h"
#include "Core/BufferUsageTracker.h"
#include "Core/FirmwareVersion.h"
#include "Modules/Network/HAModule/HARuntime.h"
#include "Core/MqttTopics.h"
#include "Core/SystemLimits.h"
#include <ArduinoJson.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <ctype.h>
#include <math.h>
#include <new>
#include <stdarg.h>
#include <string.h>

#ifndef FLOW_HA_BOOT_TRACE
#define FLOW_HA_BOOT_TRACE 0
#endif

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HAModule)
#include "Core/ModuleLog.h"

#if FLOW_HA_BOOT_TRACE
#define HA_BOOT_TRACE_D(...) LOGD(__VA_ARGS__)
#define HA_BOOT_TRACE_I(...) LOGI(__VA_ARGS__)
#else
#define HA_BOOT_TRACE_D(...) do {} while (0)
#define HA_BOOT_TRACE_I(...) do {} while (0)
#endif

namespace {
static constexpr uint8_t kHaCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kHaCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Ha, kHaCfgBranch}, "ha", "ha", (uint8_t)MqttPublishPriority::Normal, nullptr},
};
static constexpr const char* kHaDeviceConfigUrl = "http://flowio.local";
}

static void buildAvailabilityField(const MqttService* mqttSvc_,
                                   char* out,
                                   size_t outLen,
                                   const char* stateTopic = nullptr,
                                   const char* stateAvailabilityTemplate = nullptr)
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!mqttSvc_ || !mqttSvc_->formatTopic) return;

    char availabilityTopic[192] = {0};
    mqttSvc_->formatTopic(mqttSvc_->ctx, MqttTopics::SuffixStatus, availabilityTopic, sizeof(availabilityTopic));
    const bool hasStateAvailability =
        stateTopic && stateTopic[0] != '\0' &&
        stateAvailabilityTemplate && stateAvailabilityTemplate[0] != '\0';

    if (availabilityTopic[0] == '\0') {
        if (!hasStateAvailability) return;
        snprintf(
            out,
            outLen,
            ",\"avty\":[{\"t\":\"%s\",\"val_tpl\":\"%s\"}],"
            "\"avty_mode\":\"all\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"",
            stateTopic,
            stateAvailabilityTemplate
        );
        return;
    }

    if (hasStateAvailability) {
        snprintf(
            out,
            outLen,
            ",\"avty\":[{\"t\":\"%s\",\"val_tpl\":\"%s\"},"
            "{\"t\":\"%s\",\"val_tpl\":\"{{ 'online' if value_json.online else 'offline' }}\"}],"
            "\"avty_mode\":\"all\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"",
            stateTopic,
            stateAvailabilityTemplate,
            availabilityTopic
        );
        return;
    }

    snprintf(
        out,
        outLen,
        ",\"avty\":[{\"t\":\"%s\",\"val_tpl\":\"{{ 'online' if value_json.online else 'offline' }}\"}],"
        "\"avty_mode\":\"all\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"",
        availabilityTopic
    );
}

static bool formatChecked(char* out, size_t outLen, const char* fmt, ...)
{
    if (!out || outLen == 0 || !fmt) return false;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out, outLen, fmt, ap);
    va_end(ap);
    return (n >= 0) && ((size_t)n < outLen);
}

void HAModule::makeDeviceId(char* out, size_t len)
{
    if (!out || len == 0) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void HAModule::makeHexNodeId(char* out, size_t len)
{
    if (!out || len == 0) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "0x%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void HAModule::sanitizeId(const char* in, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!in) return;

    size_t w = 0;
    for (size_t i = 0; in[i] != '\0' && w + 1 < outLen; ++i) {
        char c = in[i];
        if (isalnum((unsigned char)c)) {
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            out[w++] = c;
        } else {
            out[w++] = '_';
        }
    }
    out[w] = '\0';
}

bool HAModule::addSensorSvc_(const HASensorEntry* entry)
{
    if (!entry) return false;
    return addSensorEntry(*entry);
}

bool HAModule::addBinarySensorSvc_(const HABinarySensorEntry* entry)
{
    if (!entry) return false;
    return addBinarySensorEntry(*entry);
}

bool HAModule::addSwitchSvc_(const HASwitchEntry* entry)
{
    if (!entry) return false;
    return addSwitchEntry(*entry);
}

bool HAModule::addNumberSvc_(const HANumberEntry* entry)
{
    if (!entry) return false;
    return addNumberEntry(*entry);
}

bool HAModule::addSelectSvc_(const HASelectEntry* entry)
{
    if (!entry) return false;
    return addSelectEntry(*entry);
}

bool HAModule::addButtonSvc_(const HAButtonEntry* entry)
{
    if (!entry) return false;
    return addButtonEntry(*entry);
}

bool HAModule::requestRefreshSvc_()
{
    requestAutoconfigRefresh();
    return true;
}

static bool isIntegralHaNumberValue(float value)
{
    return fabsf(value - roundf(value)) <= 0.0005f;
}

static bool buildHaNumberRangeField(char* out, size_t outLen, float minValue, float maxValue, float step)
{
    if (!out || outLen == 0) return false;
    if (isIntegralHaNumberValue(minValue) &&
        isIntegralHaNumberValue(maxValue) &&
        isIntegralHaNumberValue(step)) {
        return formatChecked(
            out,
            outLen,
            "\"min\":%ld,\"max\":%ld,\"step\":%ld",
            (long)lroundf(minValue),
            (long)lroundf(maxValue),
            (long)lroundf(step)
        );
    }
    return formatChecked(
        out,
        outLen,
        "\"min\":%.3f,\"max\":%.3f,\"step\":%.3f",
        (double)minValue,
        (double)maxValue,
        (double)step
    );
}

bool HAModule::ensureStorage_()
{
#if FLOW_HA_ONESHOT_DISCOVERY
    if (oneShotResourcesReleased_) return false;
    if (sensors_ && binarySensors_ && switches_ && numbers_ && buttons_ && selects_ && pendingBits_) return true;

    if (!sensors_) sensors_ = new (std::nothrow) HASensorEntry[MAX_HA_SENSORS]{};
    if (!binarySensors_) binarySensors_ = new (std::nothrow) HABinarySensorEntry[MAX_HA_BINARY_SENSORS]{};
    if (!switches_) switches_ = new (std::nothrow) HASwitchEntry[MAX_HA_SWITCHES]{};
    if (!numbers_) numbers_ = new (std::nothrow) HANumberEntry[MAX_HA_NUMBERS]{};
    if (!buttons_) buttons_ = new (std::nothrow) HAButtonEntry[MAX_HA_BUTTONS]{};
    if (!selects_) selects_ = new (std::nothrow) HASelectEntry[MAX_HA_SELECTS]{};
    if (!pendingBits_) pendingBits_ = new (std::nothrow) uint32_t[HA_PENDING_WORDS]{};

    if (sensors_ && binarySensors_ && switches_ && numbers_ && buttons_ && selects_ && pendingBits_) {
        HA_BOOT_TRACE_D("ha boot trace: oneshot storage ready table_cap=%u pending_bytes=%u",
                        (unsigned)entityTableCapacityBytes_(),
                        (unsigned)(sizeof(uint32_t) * HA_PENDING_WORDS));
        return true;
    }
    HA_BOOT_TRACE_I("ha boot trace: oneshot storage allocation failed, releasing partial storage");
    releaseOneShotResources_();
    return false;
#else
    return true;
#endif
}

size_t HAModule::entityTableUsedBytes_() const
{
    return (size_t)sensorCount_ * sizeof(HASensorEntry) +
           (size_t)binarySensorCount_ * sizeof(HABinarySensorEntry) +
           (size_t)switchCount_ * sizeof(HASwitchEntry) +
           (size_t)numberCount_ * sizeof(HANumberEntry) +
           (size_t)buttonCount_ * sizeof(HAButtonEntry) +
           (size_t)selectCount_ * sizeof(HASelectEntry);
}

size_t HAModule::entityTableCapacityBytes_() const
{
    return (size_t)MAX_HA_SENSORS * sizeof(HASensorEntry) +
           (size_t)MAX_HA_BINARY_SENSORS * sizeof(HABinarySensorEntry) +
           (size_t)MAX_HA_SWITCHES * sizeof(HASwitchEntry) +
           (size_t)MAX_HA_NUMBERS * sizeof(HANumberEntry) +
           (size_t)MAX_HA_BUTTONS * sizeof(HAButtonEntry) +
           (size_t)MAX_HA_SELECTS * sizeof(HASelectEntry);
}

void HAModule::releaseOneShotResources_()
{
#if FLOW_HA_ONESHOT_DISCOVERY
    const uint16_t releasedEntityCount = entityCount_();
    const size_t releasedEntityUsedBytes = entityTableUsedBytes_();
    const size_t releasedEntityCapBytes = entityTableCapacityBytes_();
    const size_t releasedPendingBytes = sizeof(uint32_t) * HA_PENDING_WORDS;
    delete[] sensors_;
    delete[] binarySensors_;
    delete[] switches_;
    delete[] numbers_;
    delete[] buttons_;
    delete[] selects_;
    delete[] pendingBits_;
    sensors_ = nullptr;
    binarySensors_ = nullptr;
    switches_ = nullptr;
    numbers_ = nullptr;
    buttons_ = nullptr;
    selects_ = nullptr;
    pendingBits_ = nullptr;
    sensorCount_ = 0;
    binarySensorCount_ = 0;
    switchCount_ = 0;
    numberCount_ = 0;
    buttonCount_ = 0;
    selectCount_ = 0;
    startupReady_ = false;
    published_ = true;
    oneShotCompleted_ = true;
    oneShotResourcesReleased_ = true;
    HA_BOOT_TRACE_I("ha boot trace: oneshot release entities=%u used=%u cap=%u pending_bytes=%u",
                    (unsigned)releasedEntityCount,
                    (unsigned)releasedEntityUsedBytes,
                    (unsigned)releasedEntityCapBytes,
                    (unsigned)releasedPendingBytes);
#endif
}

bool HAModule::addSensorEntry(const HASensorEntry& entry)
{
    if (oneShotCompleted_ || !ensureStorage_()) return false;
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.stateTopicSuffix || !entry.valueTemplate) {
        return false;
    }

    for (uint8_t i = 0; i < sensorCount_; ++i) {
        if (strcmp(sensors_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(sensors_[i].objectSuffix, entry.objectSuffix) == 0) {
            sensors_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (sensorCount_ >= MAX_HA_SENSORS) return false;
    sensors_[sensorCount_++] = entry;
    BufferUsageTracker::note(TrackedBufferId::HaEntityTables,
                             entityTableUsedBytes_(),
                             entityTableCapacityBytes_(),
                             entry.objectSuffix,
                             nullptr);
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::addBinarySensorEntry(const HABinarySensorEntry& entry)
{
    if (oneShotCompleted_ || !ensureStorage_()) return false;
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.stateTopicSuffix || !entry.valueTemplate) {
        return false;
    }

    for (uint8_t i = 0; i < binarySensorCount_; ++i) {
        if (strcmp(binarySensors_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(binarySensors_[i].objectSuffix, entry.objectSuffix) == 0) {
            binarySensors_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (binarySensorCount_ >= MAX_HA_BINARY_SENSORS) return false;
    binarySensors_[binarySensorCount_++] = entry;
    BufferUsageTracker::note(TrackedBufferId::HaEntityTables,
                             entityTableUsedBytes_(),
                             entityTableCapacityBytes_(),
                             entry.objectSuffix,
                             nullptr);
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::addSwitchEntry(const HASwitchEntry& entry)
{
    if (oneShotCompleted_ || !ensureStorage_()) return false;
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.stateTopicSuffix ||
        !entry.valueTemplate || !entry.commandTopicSuffix || !entry.payloadOn || !entry.payloadOff) {
        return false;
    }

    for (uint8_t i = 0; i < switchCount_; ++i) {
        if (strcmp(switches_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(switches_[i].objectSuffix, entry.objectSuffix) == 0) {
            switches_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (switchCount_ >= MAX_HA_SWITCHES) return false;
    switches_[switchCount_++] = entry;
    BufferUsageTracker::note(TrackedBufferId::HaEntityTables,
                             entityTableUsedBytes_(),
                             entityTableCapacityBytes_(),
                             entry.objectSuffix,
                             nullptr);
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::addNumberEntry(const HANumberEntry& entry)
{
    if (oneShotCompleted_ || !ensureStorage_()) return false;
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.stateTopicSuffix ||
        !entry.valueTemplate || !entry.commandTopicSuffix || !entry.commandTemplate) {
        return false;
    }

    for (uint8_t i = 0; i < numberCount_; ++i) {
        if (strcmp(numbers_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(numbers_[i].objectSuffix, entry.objectSuffix) == 0) {
            numbers_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (numberCount_ >= MAX_HA_NUMBERS) {
        LOGW("HA number table full (%u/%u) reject=%s/%s",
             (unsigned)numberCount_,
             (unsigned)MAX_HA_NUMBERS,
             entry.ownerId ? entry.ownerId : "?",
             entry.objectSuffix ? entry.objectSuffix : "?");
        return false;
    }
    numbers_[numberCount_++] = entry;
    BufferUsageTracker::note(TrackedBufferId::HaEntityTables,
                             entityTableUsedBytes_(),
                             entityTableCapacityBytes_(),
                             entry.objectSuffix,
                             nullptr);
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::addButtonEntry(const HAButtonEntry& entry)
{
    if (oneShotCompleted_ || !ensureStorage_()) return false;
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.commandTopicSuffix || !entry.payloadPress) {
        return false;
    }

    for (uint8_t i = 0; i < buttonCount_; ++i) {
        if (strcmp(buttons_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(buttons_[i].objectSuffix, entry.objectSuffix) == 0) {
            buttons_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (buttonCount_ >= MAX_HA_BUTTONS) {
        LOGW("HA button table full (%u/%u) reject=%s/%s",
             (unsigned)buttonCount_,
             (unsigned)MAX_HA_BUTTONS,
             entry.ownerId ? entry.ownerId : "?",
             entry.objectSuffix ? entry.objectSuffix : "?");
        return false;
    }
    buttons_[buttonCount_++] = entry;
    BufferUsageTracker::note(TrackedBufferId::HaEntityTables,
                             entityTableUsedBytes_(),
                             entityTableCapacityBytes_(),
                             entry.objectSuffix,
                             nullptr);
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::addSelectEntry(const HASelectEntry& entry)
{
    if (oneShotCompleted_ || !ensureStorage_()) return false;
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.stateTopicSuffix ||
        !entry.valueTemplate || !entry.commandTopicSuffix || !entry.commandTemplate ||
        !entry.optionsJson) {
        return false;
    }

    for (uint8_t i = 0; i < selectCount_; ++i) {
        if (strcmp(selects_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(selects_[i].objectSuffix, entry.objectSuffix) == 0) {
            selects_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (selectCount_ >= MAX_HA_SELECTS) {
        LOGW("HA select table full (%u/%u) reject=%s/%s",
             (unsigned)selectCount_,
             (unsigned)MAX_HA_SELECTS,
             entry.ownerId ? entry.ownerId : "?",
             entry.objectSuffix ? entry.objectSuffix : "?");
        return false;
    }
    selects_[selectCount_++] = entry;
    BufferUsageTracker::note(TrackedBufferId::HaEntityTables,
                             entityTableUsedBytes_(),
                             entityTableCapacityBytes_(),
                             entry.objectSuffix,
                             nullptr);
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::buildObjectId(const char* suffix, char* out, size_t outLen) const
{
    if (!suffix || !out || outLen == 0) return false;
    char cleanPrefix[sizeof(cfgData_.entityPrefix)] = {0};
    sanitizeId(cfgData_.entityPrefix, cleanPrefix, sizeof(cleanPrefix));
    char cleanSuffix[96] = {0};
    sanitizeId(suffix, cleanSuffix, sizeof(cleanSuffix));
    if (cleanSuffix[0] == '\0') return false;

    size_t prefixLen = strnlen(cleanPrefix, sizeof(cleanPrefix));
    while (prefixLen > 0 && cleanPrefix[prefixLen - 1] == '_') {
        cleanPrefix[--prefixLen] = '\0';
    }

    char raw[256] = {0};
    if (cleanPrefix[0] != '\0') {
        snprintf(raw, sizeof(raw), "%s_%s", cleanPrefix, cleanSuffix);
    } else {
        snprintf(raw, sizeof(raw), "%s", cleanSuffix);
    }
    sanitizeId(raw, out, outLen);
    return out[0] != '\0';
}

bool HAModule::buildDefaultEntityId(const char* component, const char* objectId, char* out, size_t outLen) const
{
    if (!component || !objectId || !out || outLen == 0) return false;
    snprintf(out, outLen, "%s.%s", component, objectId);
    return out[0] != '\0';
}

bool HAModule::buildUniqueId(const char* objectId, const char* name, char* out, size_t outLen) const
{
    if (!objectId || !out || outLen == 0) return false;
    char cleanName[96] = {0};
    sanitizeId(name ? name : "", cleanName, sizeof(cleanName));
    if (cleanName[0] != '\0') {
        snprintf(out, outLen, "%s_%s_%s", deviceId_, objectId, cleanName);
    } else {
        snprintf(out, outLen, "%s_%s", deviceId_, objectId);
    }
    return out[0] != '\0';
}

bool HAModule::publishDiscovery(const char* component, const char* objectId, MqttBuildContext& outCtx)
{
    if (!component || !objectId) return false;
    if (!outCtx.topic || outCtx.topicCapacity == 0 || !outCtx.payload || outCtx.payloadCapacity == 0) return false;
    if (!formatChecked(outCtx.topic,
                       outCtx.topicCapacity,
                       "%s/%s/%s/%s/config",
                       cfgData_.discoveryPrefix,
                       component,
                       nodeTopicId_,
                       objectId)) {
        LOGW("HA discovery topic truncated component=%s object=%s", component, objectId);
        return false;
    }
    outCtx.topicLen = (uint16_t)strnlen(outCtx.topic, outCtx.topicCapacity);
    outCtx.payloadLen = (uint16_t)strnlen(outCtx.payload, outCtx.payloadCapacity);
    outCtx.qos = 0;
    outCtx.retain = true;
    return true;
}

bool HAModule::publishSensor(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* entityCategory, const char* icon, const char* unit,
                             bool hasEntityName,
                             const char* availabilityTemplate,
                             bool isText,
                             MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !stateTopic || !valueTemplate) return false;

    char unitField[64] = {0};
    if (unit) {
        snprintf(unitField, sizeof(unitField), ",\"unit_of_meas\":\"%s\"", unit);
    }
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"ent_cat\":\"%s\"", entityCategory);
    }
    char iconField[64] = {0};
    if (icon && icon[0] != '\0') {
        snprintf(iconField, sizeof(iconField), ",\"ic\":\"%s\"", icon);
    }
    (void)hasEntityName;
    const char* hasEntityNameField = ",\"has_entity_name\":false";
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("sensor", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[768] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField), stateTopic, availabilityTemplate);
    const char* stateClassField = isText ? "" : ",\"stat_cla\":\"measurement\"";

    if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
             "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\","
             "\"stat_t\":\"%s\",\"val_tpl\":\"%s\"%s%s%s%s%s%s,"
             "\"o\":{\"name\":\"%s\"},"
             "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
             "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
             name, objectId, defaultEntityId, uniqueId,
             stateTopic, valueTemplate,
             stateClassField, entityCategoryField, iconField, unitField, hasEntityNameField, availabilityField,
             originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
        LOGW("HA sensor payload truncated object=%s", objectId);
        return false;
    }

    return publishDiscovery("sensor", objectId, buildCtx);
}

bool HAModule::publishBinarySensor(const char* objectId, const char* name,
                                   const char* stateTopic, const char* valueTemplate,
                                   const char* deviceClass, const char* entityCategory,
                                   const char* icon,
                                   MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !stateTopic || !valueTemplate) return false;

    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("binary_sensor", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField));
    char deviceClassField[64] = {0};
    if (deviceClass && deviceClass[0] != '\0') {
        snprintf(deviceClassField, sizeof(deviceClassField), ",\"dev_cla\":\"%s\"", deviceClass);
    }
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"ent_cat\":\"%s\"", entityCategory);
    }
    char iconField[64] = {0};
    if (icon && icon[0] != '\0') {
        snprintf(iconField, sizeof(iconField), ",\"ic\":\"%s\"", icon);
    }

    if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
             "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\","
             "\"stat_t\":\"%s\",\"val_tpl\":\"%s\",\"pl_on\":\"True\",\"pl_off\":\"False\","
             "\"has_entity_name\":false%s%s%s%s,"
             "\"o\":{\"name\":\"%s\"},"
             "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
             "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
             name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate,
             deviceClassField, entityCategoryField, iconField, availabilityField,
             originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
        LOGW("HA binary_sensor payload truncated object=%s", objectId);
        return false;
    }

    return publishDiscovery("binary_sensor", objectId, buildCtx);
}

bool HAModule::publishSwitch(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* commandTopic,
                             const char* payloadOn, const char* payloadOff,
                             const char* icon,
                             const char* entityCategory,
                             MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !stateTopic || !valueTemplate || !commandTopic || !payloadOn || !payloadOff) return false;
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("switch", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField));
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"ent_cat\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\",\"stat_t\":\"%s\","
                 "\"val_tpl\":\"%s\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\","
                 "\"cmd_t\":\"%s\",\"pl_on\":\"%s\",\"pl_off\":\"%s\","
                 "\"ic\":\"%s\"%s%s,"
                 "\"o\":{\"name\":\"%s\"},"
                 "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
                 "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate,
                 commandTopic, payloadOn, payloadOff, icon,
                 entityCategoryField, availabilityField,
                 originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
            LOGW("HA switch payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\",\"stat_t\":\"%s\","
                 "\"val_tpl\":\"%s\",\"stat_on\":\"ON\",\"stat_off\":\"OFF\","
                 "\"cmd_t\":\"%s\",\"pl_on\":\"%s\",\"pl_off\":\"%s\"%s%s,"
                 "\"o\":{\"name\":\"%s\"},"
                 "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
                 "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate,
                 commandTopic, payloadOn, payloadOff, entityCategoryField, availabilityField,
                 originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
            LOGW("HA switch payload truncated object=%s", objectId);
            return false;
        }
    }
    return publishDiscovery("switch", objectId, buildCtx);
}

bool HAModule::publishNumber(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* commandTopic, const char* commandTemplate,
                             float minValue, float maxValue, float step,
                             const char* mode, const char* entityCategory, const char* icon, const char* unit,
                             MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !stateTopic || !valueTemplate || !commandTopic || !commandTemplate) return false;
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("number", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField));

    char unitField[48] = {0};
    if (unit && unit[0] != '\0') {
        snprintf(unitField, sizeof(unitField), ",\"unit_of_meas\":\"%s\"", unit);
    }
    char entityCategoryField[48] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"ent_cat\":\"%s\"", entityCategory);
    }
    char rangeField[80] = {0};
    if (!buildHaNumberRangeField(rangeField, sizeof(rangeField), minValue, maxValue, step)) {
        LOGW("HA number range payload truncated object=%s", objectId);
        return false;
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\",\"stat_t\":\"%s\","
                 "\"val_tpl\":\"%s\",\"cmd_t\":\"%s\",\"cmd_tpl\":\"%s\","
                 "%s,\"mode\":\"%s\",\"ic\":\"%s\"%s%s%s,"
                 "\"o\":{\"name\":\"%s\"},"
                 "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
                 "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate, commandTopic, commandTemplate,
                 rangeField, mode ? mode : "slider", icon, entityCategoryField, unitField, availabilityField,
                 originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
            LOGW("HA number payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\",\"stat_t\":\"%s\","
                 "\"val_tpl\":\"%s\",\"cmd_t\":\"%s\",\"cmd_tpl\":\"%s\","
                 "%s,\"mode\":\"%s\"%s%s%s,"
                 "\"o\":{\"name\":\"%s\"},"
                 "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
                 "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate, commandTopic, commandTemplate,
                 rangeField, mode ? mode : "slider", entityCategoryField, unitField, availabilityField,
                 originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
            LOGW("HA number payload truncated object=%s", objectId);
            return false;
        }
    }
    return publishDiscovery("number", objectId, buildCtx);
}

bool HAModule::publishSelect(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* commandTopic, const char* commandTemplate,
                             const char* optionsJson,
                             const char* icon,
                             const char* entityCategory,
                             MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !stateTopic || !valueTemplate || !commandTopic ||
        !commandTemplate || !optionsJson) {
        return false;
    }
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("select", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField));
    char entityCategoryField[48] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"ent_cat\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\","
                 "\"stat_t\":\"%s\",\"val_tpl\":\"%s\",\"cmd_t\":\"%s\",\"cmd_tpl\":\"%s\","
                 "\"options\":%s,\"ic\":\"%s\"%s%s,"
                 "\"o\":{\"name\":\"%s\"},"
                 "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
                 "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId,
                 stateTopic, valueTemplate, commandTopic, commandTemplate,
                 optionsJson, icon, entityCategoryField, availabilityField,
                 originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
            LOGW("HA select payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\","
                 "\"stat_t\":\"%s\",\"val_tpl\":\"%s\",\"cmd_t\":\"%s\",\"cmd_tpl\":\"%s\","
                 "\"options\":%s%s%s,"
                 "\"o\":{\"name\":\"%s\"},"
                 "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
                 "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId,
                 stateTopic, valueTemplate, commandTopic, commandTemplate,
                 optionsJson, entityCategoryField, availabilityField,
                 originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
            LOGW("HA select payload truncated object=%s", objectId);
            return false;
        }
    }

    return publishDiscovery("select", objectId, buildCtx);
}

bool HAModule::publishButton(const char* objectId, const char* name,
                             const char* commandTopic, const char* payloadPress,
                             const char* entityCategory, const char* icon,
                             MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !commandTopic || !payloadPress) return false;
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("button", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField));
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"ent_cat\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\","
                 "\"cmd_t\":\"%s\",\"pl_prs\":\"%s\",\"ic\":\"%s\"%s%s,"
                 "\"o\":{\"name\":\"%s\"},"
                 "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
                 "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId,
                 commandTopic, payloadPress, icon,
                 entityCategoryField, availabilityField,
                 originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
            LOGW("HA button payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"obj_id\":\"%s\",\"def_ent_id\":\"%s\",\"uniq_id\":\"%s\","
                 "\"cmd_t\":\"%s\",\"pl_prs\":\"%s\"%s%s,"
                 "\"o\":{\"name\":\"%s\"},"
                 "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
                 "\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId,
                 commandTopic, payloadPress,
                 entityCategoryField, availabilityField,
                 originName_, deviceIdent_, deviceName_, cfgData_.vendor, cfgData_.model, FirmwareVersion::Full, kHaDeviceConfigUrl)) {
            LOGW("HA button payload truncated object=%s", objectId);
            return false;
        }
    }

    return publishDiscovery("button", objectId, buildCtx);
}

void HAModule::setStartupReady(bool ready)
{
    startupReady_ = ready;
    HA_BOOT_TRACE_D("ha boot trace: startup ready=%d pending=%d",
                    ready ? 1 : 0,
                    anyPending_() ? 1 : 0);
    if (ready) {
        const bool queued = enqueuePending_(MqttPublishPriority::Low);
        HA_BOOT_TRACE_D("ha boot trace: startup enqueue=%d", queued ? 1 : 0);
    }
}

void HAModule::setBranding(const char* objectPrefix,
                           const char* originName,
                           const char* vendor,
                           const char* model)
{
    if (objectPrefix && objectPrefix[0] != '\0') {
        snprintf(objectPrefix_, sizeof(objectPrefix_), "%s", objectPrefix);
        sanitizeId(objectPrefix_, objectPrefix_, sizeof(objectPrefix_));
        if (objectPrefix_[0] == '\0') snprintf(objectPrefix_, sizeof(objectPrefix_), "fio");
        snprintf(cfgData_.entityPrefix, sizeof(cfgData_.entityPrefix), "%s", objectPrefix_);
    }
    if (originName && originName[0] != '\0') {
        snprintf(originName_, sizeof(originName_), "%s", originName);
    }
    if (vendor && vendor[0] != '\0') {
        snprintf(cfgData_.vendor, sizeof(cfgData_.vendor), "%s", vendor);
    }
    if (model && model[0] != '\0') {
        snprintf(cfgData_.model, sizeof(cfgData_.model), "%s", model);
    }
}

void HAModule::refreshIdentityFromConfig()
{
    if (cfgData_.deviceId[0] != '\0') {
        strncpy(deviceId_, cfgData_.deviceId, sizeof(deviceId_) - 1);
        deviceId_[sizeof(deviceId_) - 1] = '\0';
    } else {
        makeHexNodeId(deviceId_, sizeof(deviceId_));
    }
    sanitizeId(deviceId_, nodeTopicId_, sizeof(nodeTopicId_));
    if (nodeTopicId_[0] == '\0') {
        snprintf(nodeTopicId_, sizeof(nodeTopicId_), "flowio");
    }

    char mqttTopicDeviceId[64] = {0};
    const char* idForEntityPrefix = deviceId_;
    if (resolveMqttTopicDeviceId_(mqttTopicDeviceId, sizeof(mqttTopicDeviceId))) {
        idForEntityPrefix = mqttTopicDeviceId;
    }

    // HA `device.identifiers` should follow the effective MQTT topic device id when available.
    snprintf(deviceIdent_, sizeof(deviceIdent_), "%s-%s", cfgData_.vendor, idForEntityPrefix);
    refreshDeviceNameFromMqttConfig_();
}

void HAModule::refreshDeviceNameFromMqttConfig_()
{
    snprintf(deviceName_, sizeof(deviceName_), "%s", originName_);
    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return;

    char mqttJson[Limits::Mqtt::Buffers::StateCfg] = {0};
    bool truncated = false;
    if (!cfgSvc_->toJsonModule(cfgSvc_->ctx, "mqtt", mqttJson, sizeof(mqttJson), &truncated)) return;

    StaticJsonDocument<512> doc;
    const DeserializationError err = deserializeJson(doc, mqttJson);
    if (err || !doc.is<JsonObjectConst>()) return;

    const char* configuredName = doc["deviceName"] | "";
    if (configuredName && configuredName[0] != '\0') {
        snprintf(deviceName_, sizeof(deviceName_), "%s", configuredName);
    }
}

bool HAModule::resolveMqttTopicDeviceId_(char* out, size_t outLen) const
{
    if (!out || outLen == 0U) return false;
    out[0] = '\0';
    if (!mqttSvc_ || !mqttSvc_->formatTopic) return false;

    char topic[192] = {0};
    mqttSvc_->formatTopic(mqttSvc_->ctx, MqttTopics::SuffixStatus, topic, sizeof(topic));
    if (topic[0] == '\0') return false;

    // Topic format: <base>/<deviceId>/status. Keep deviceId extraction robust when base contains slashes.
    char* lastSlash = strrchr(topic, '/');
    if (!lastSlash || lastSlash == topic) return false;
    *lastSlash = '\0'; // Strip trailing "/status"

    char* deviceSlash = strrchr(topic, '/');
    if (!deviceSlash || *(deviceSlash + 1) == '\0') return false;

    const int n = snprintf(out, outLen, "%s", deviceSlash + 1);
    return n > 0 && (size_t)n < outLen;
}

uint16_t HAModule::entityCount_() const
{
    return (uint16_t)sensorCount_ +
           (uint16_t)binarySensorCount_ +
           (uint16_t)switchCount_ +
           (uint16_t)numberCount_ +
           (uint16_t)selectCount_ +
           (uint16_t)buttonCount_;
}

uint16_t HAModule::messageCount_() const
{
    return entityCount_();
}

bool HAModule::isPending_(uint16_t messageId) const
{
    if (!pendingBits_) return false;
    if (messageId >= MAX_HA_MESSAGES) return false;
    const uint16_t word = (uint16_t)(messageId / 32U);
    const uint16_t bit = (uint16_t)(messageId % 32U);
    return (pendingBits_[word] & (1UL << bit)) != 0UL;
}

void HAModule::setPending_(uint16_t messageId, bool pending)
{
    if (!pendingBits_) return;
    if (messageId >= MAX_HA_MESSAGES) return;
    const uint16_t word = (uint16_t)(messageId / 32U);
    const uint16_t bit = (uint16_t)(messageId % 32U);
    const uint32_t mask = (1UL << bit);
    if (pending) pendingBits_[word] |= mask;
    else pendingBits_[word] &= ~mask;
}

void HAModule::markAllPending_()
{
    if (oneShotCompleted_ || !ensureStorage_()) return;
    memset(pendingBits_, 0, sizeof(uint32_t) * HA_PENDING_WORDS);
    const uint16_t count = messageCount_();
    for (uint16_t i = 0; i < count; ++i) {
        setPending_(i, true);
    }
    HA_BOOT_TRACE_D("ha boot trace: mark all pending messages=%u entities=%u",
                    (unsigned)count,
                    (unsigned)entityCount_());
}

bool HAModule::anyPending_() const
{
    if (!pendingBits_) return false;
    for (uint16_t i = 0; i < HA_PENDING_WORDS; ++i) {
        if (pendingBits_[i] != 0U) return true;
    }
    return false;
}

bool HAModule::enqueuePending_(MqttPublishPriority prio)
{
    if (oneShotCompleted_) return false;
    if (!mqttSvc_ || !mqttSvc_->enqueue) return false;
    if (!discoveryReady_()) return false;

    const uint16_t count = messageCount_();
    constexpr uint8_t kFlags = (uint8_t)MqttEnqueueFlags::SilentRejectLog;
    for (uint16_t i = 0; i < count; ++i) {
        if (!isPending_(i)) continue;
        const bool queued = mqttSvc_->enqueue(mqttSvc_->ctx, ProducerId, i, (uint8_t)prio, kFlags);
        HA_BOOT_TRACE_D("ha boot trace: enqueue message=%u/%u prio=%u queued=%d",
                        (unsigned)i,
                        (unsigned)count,
                        (unsigned)prio,
                        queued ? 1 : 0);
        return queued;
    }
    HA_BOOT_TRACE_D("ha boot trace: enqueue skipped (no pending)");
    return false;
}

bool HAModule::discoveryReady_() const
{
    if (!startupReady_) return false;
    if (!dsSvc_ || !dsSvc_->store) return false;
    const DataStore& ds = *dsSvc_->store;
    return networkReady(ds) && mqttReady(ds);
}

MqttBuildResult HAModule::producerBuildStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    return self ? self->buildMessage_(messageId, buildCtx) : MqttBuildResult::PermanentError;
}

void HAModule::producerPublishedStatic_(void* ctx, uint16_t messageId)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (self) self->onMessagePublished_(messageId);
}

void HAModule::producerDroppedStatic_(void* ctx, uint16_t messageId)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (self) self->onMessageDropped_(messageId);
}

bool HAModule::buildEntityMessage_(uint16_t messageId, MqttBuildContext& buildCtx)
{
    if (!sensors_ || !binarySensors_ || !switches_ || !numbers_ || !buttons_ || !selects_) return false;
    bool ok = false;
    uint16_t cursor = 0;

    if (messageId < (uint16_t)(cursor + sensorCount_)) {
        const HASensorEntry& e = sensors_[messageId - cursor];
        if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
            mqttSvc_->formatTopic(mqttSvc_->ctx, e.stateTopicSuffix, stateTopicBuf_, sizeof(stateTopicBuf_));
            ok = publishSensor(objectIdBuf_, e.name, stateTopicBuf_, e.valueTemplate,
                               e.entityCategory, e.icon, e.unit, e.hasEntityName, e.availabilityTemplate, e.isText, &buildCtx);
        }
    } else {
        cursor = (uint16_t)(cursor + sensorCount_);
        if (messageId < (uint16_t)(cursor + binarySensorCount_)) {
            const HABinarySensorEntry& e = binarySensors_[messageId - cursor];
            if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
                mqttSvc_->formatTopic(mqttSvc_->ctx, e.stateTopicSuffix, stateTopicBuf_, sizeof(stateTopicBuf_));
                ok = publishBinarySensor(objectIdBuf_, e.name, stateTopicBuf_, e.valueTemplate, e.deviceClass, e.entityCategory, e.icon, &buildCtx);
            }
        } else {
            cursor = (uint16_t)(cursor + binarySensorCount_);
            if (messageId < (uint16_t)(cursor + switchCount_)) {
                const HASwitchEntry& e = switches_[messageId - cursor];
                if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
                    mqttSvc_->formatTopic(mqttSvc_->ctx, e.stateTopicSuffix, stateTopicBuf_, sizeof(stateTopicBuf_));
                    mqttSvc_->formatTopic(mqttSvc_->ctx, e.commandTopicSuffix, commandTopicBuf_, sizeof(commandTopicBuf_));
                    ok = publishSwitch(objectIdBuf_, e.name, stateTopicBuf_, e.valueTemplate,
                                       commandTopicBuf_, e.payloadOn, e.payloadOff, e.icon, e.entityCategory, &buildCtx);
                }
            } else {
                cursor = (uint16_t)(cursor + switchCount_);
                if (messageId < (uint16_t)(cursor + numberCount_)) {
                    const HANumberEntry& e = numbers_[messageId - cursor];
                    if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
                        mqttSvc_->formatTopic(mqttSvc_->ctx, e.stateTopicSuffix, stateTopicBuf_, sizeof(stateTopicBuf_));
                        mqttSvc_->formatTopic(mqttSvc_->ctx, e.commandTopicSuffix, commandTopicBuf_, sizeof(commandTopicBuf_));
                        ok = publishNumber(objectIdBuf_, e.name, stateTopicBuf_, e.valueTemplate,
                                           commandTopicBuf_, e.commandTemplate,
                                           e.minValue, e.maxValue, e.step,
                                           e.mode, e.entityCategory, e.icon, e.unit, &buildCtx);
                    }
                } else {
                    cursor = (uint16_t)(cursor + numberCount_);
                    if (messageId < (uint16_t)(cursor + selectCount_)) {
                        const HASelectEntry& e = selects_[messageId - cursor];
                        if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
                            mqttSvc_->formatTopic(mqttSvc_->ctx, e.stateTopicSuffix, stateTopicBuf_, sizeof(stateTopicBuf_));
                            mqttSvc_->formatTopic(mqttSvc_->ctx, e.commandTopicSuffix, commandTopicBuf_, sizeof(commandTopicBuf_));
                            ok = publishSelect(objectIdBuf_, e.name, stateTopicBuf_, e.valueTemplate,
                                               commandTopicBuf_, e.commandTemplate, e.optionsJson,
                                               e.icon, e.entityCategory, &buildCtx);
                        }
                    } else {
                        cursor = (uint16_t)(cursor + selectCount_);
                        if (messageId < (uint16_t)(cursor + buttonCount_)) {
                            const HAButtonEntry& e = buttons_[messageId - cursor];
                            if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
                                mqttSvc_->formatTopic(mqttSvc_->ctx, e.commandTopicSuffix, commandTopicBuf_, sizeof(commandTopicBuf_));
                                ok = publishButton(objectIdBuf_, e.name, commandTopicBuf_, e.payloadPress, e.entityCategory, e.icon, &buildCtx);
                            }
                        }
                    }
                }
            }
        }
    }

    if (!ok) return false;
    return true;
}

MqttBuildResult HAModule::buildMessage_(uint16_t messageId, MqttBuildContext& buildCtx)
{
    if (oneShotCompleted_) return MqttBuildResult::NoLongerNeeded;
    if (!startupReady_) return MqttBuildResult::RetryLater;
    if (!cfgData_.enabled) return MqttBuildResult::NoLongerNeeded;
    if (!mqttSvc_ || !mqttSvc_->formatTopic) return MqttBuildResult::PermanentError;
    if (!dsSvc_ || !dsSvc_->store) return MqttBuildResult::RetryLater;
    if (!networkReady(*dsSvc_->store)) return MqttBuildResult::RetryLater;
    if (!mqttReady(*dsSvc_->store)) return MqttBuildResult::RetryLater;
    if (!isPending_(messageId)) return MqttBuildResult::NoLongerNeeded;

    refreshIdentityFromConfig();
    setHaVendor(*dsSvc_->store, cfgData_.vendor);
    setHaDeviceId(*dsSvc_->store, deviceId_);

    const uint16_t entityCount = entityCount_();
    HA_BOOT_TRACE_D("ha boot trace: build message=%u entity_count=%u total=%u",
                    (unsigned)messageId,
                    (unsigned)entityCount,
                    (unsigned)messageCount_());
    if (messageId >= entityCount) return MqttBuildResult::NoLongerNeeded;
    if (!buildEntityMessage_(messageId, buildCtx)) return MqttBuildResult::PermanentError;
    return MqttBuildResult::Ready;
}

void HAModule::onMessagePublished_(uint16_t messageId)
{
    setPending_(messageId, false);
    const bool done = !anyPending_();
    if (dsSvc_ && dsSvc_->store) {
        setHaAutoconfigPublished(*dsSvc_->store, done);
    }
    published_ = done;
    HA_BOOT_TRACE_D("ha boot trace: published message=%u done=%d",
                    (unsigned)messageId,
                    done ? 1 : 0);
#if !FLOW_HA_ONESHOT_DISCOVERY
    if (done) markBootLogCaptureCompleteAfterDiscovery_();
#endif
}

void HAModule::onMessageDropped_(uint16_t messageId)
{
    setPending_(messageId, false);
    const bool done = !anyPending_();
    if (dsSvc_ && dsSvc_->store) {
        setHaAutoconfigPublished(*dsSvc_->store, done);
    }
    published_ = done;
    HA_BOOT_TRACE_I("ha boot trace: dropped message=%u done=%d",
                    (unsigned)messageId,
                    done ? 1 : 0);
#if !FLOW_HA_ONESHOT_DISCOVERY
    if (done) markBootLogCaptureCompleteAfterDiscovery_();
#endif
}

void HAModule::markBootLogCaptureCompleteAfterDiscovery_()
{
#if FLOW_ENABLE_BOOT_LOG_CAPTURE
    markBootLogCaptureComplete();
#endif
}

void HAModule::onEventStatic(const Event& e, void* user)
{
    HAModule* self = static_cast<HAModule*>(user);
    if (self) self->onEvent(e);
}

void HAModule::onEvent(const Event& e)
{
    if (oneShotCompleted_) return;
    if (e.id != EventId::DataChanged) return;
    const DataChangedPayload* payload = static_cast<const DataChangedPayload*>(e.payload);
    if (!payload) return;
    if (!dsSvc_ || !dsSvc_->store) return;

    if (payload->id == DATAKEY_NETWORK_READY) {
        HA_BOOT_TRACE_D("ha boot trace: event wifi_ready=%d mqtt_ready=%d",
                        networkReady(*dsSvc_->store) ? 1 : 0,
                        mqttReady(*dsSvc_->store) ? 1 : 0);
        if (discoveryReady_()) {
            (void)enqueuePending_(MqttPublishPriority::Low);
        }
        return;
    }

    if (payload->id == DATAKEY_MQTT_READY) {
        HA_BOOT_TRACE_D("ha boot trace: event mqtt_ready=%d wifi_ready=%d",
                        mqttReady(*dsSvc_->store) ? 1 : 0,
                        networkReady(*dsSvc_->store) ? 1 : 0);
        if (discoveryReady_()) {
            (void)enqueuePending_(MqttPublishPriority::Low);
        }
        return;
    }
}

void HAModule::requestAutoconfigRefresh()
{
    if (oneShotCompleted_) return;
    published_ = false;
    markAllPending_();
    HA_BOOT_TRACE_D("ha boot trace: refresh requested");
    if (dsSvc_ && dsSvc_->store) {
        setHaAutoconfigPublished(*dsSvc_->store, false);
    }
}

void HAModule::loop()
{
#if FLOW_HA_ONESHOT_DISCOVERY
    if (published_ && !anyPending_()) {
        LOGI("HA one-shot discovery complete, releasing resources");
        HA_BOOT_TRACE_I("ha boot trace: loop detected completion, deleting task");
        markBootLogCaptureCompleteAfterDiscovery_();
        releaseOneShotResources_();
        vTaskDelete(nullptr);
    }
#endif
    if (anyPending_()) {
        (void)enqueuePending_(MqttPublishPriority::Low);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
}

void HAModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
#if FLOW_HA_ONESHOT_DISCOVERY
    (void)services;
    return;
#else
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               ProducerIdCfg,
                               kHaCfgRoutes,
                               (uint8_t)(sizeof(kHaCfgRoutes) / sizeof(kHaCfgRoutes[0])),
                               services);
    }
#endif
}

void HAModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Ha;
    constexpr uint8_t kCfgBranchId = kHaCfgBranch;
    cfg.registerVar(enabledVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(vendorVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(deviceIdVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(entityPrefixVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(prefixVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(modelVar, kCfgModuleId, kCfgBranchId);
    HA_BOOT_TRACE_I("ha boot trace: init oneshot=%d max_entities=%u max_messages=%u",
                    FLOW_HA_ONESHOT_DISCOVERY ? 1 : 0,
                    (unsigned)MAX_HA_ENTITIES,
                    (unsigned)MAX_HA_MESSAGES);

    eventBusSvc_ = services.get<EventBusService>(ServiceId::EventBus);
    dsSvc_ = services.get<DataStoreService>(ServiceId::DataStore);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    if (!ensureStorage_()) {
        LOGE("HA storage allocation failed");
    }

    if (!services.add(ServiceId::Ha, &haSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Ha));
    }

    producer_.producerId = ProducerId;
    producer_.ctx = this;
    producer_.buildMessage = HAModule::producerBuildStatic_;
    producer_.onMessagePublished = HAModule::producerPublishedStatic_;
    producer_.onMessageDropped = HAModule::producerDroppedStatic_;
    if (mqttSvc_ && mqttSvc_->registerProducer) {
        producerRegistered_ = mqttSvc_->registerProducer(mqttSvc_->ctx, &producer_);
    }

    if (dsSvc_ && dsSvc_->store) {
        setHaAutoconfigPublished(*dsSvc_->store, false);
    }

    if (eventBusSvc_ && eventBusSvc_->bus) {
        eventBusSvc_->bus->subscribe(EventId::DataChanged, &HAModule::onEventStatic, this);
    }

    markAllPending_();
}
