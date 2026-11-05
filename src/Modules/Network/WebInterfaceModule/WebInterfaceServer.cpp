/**
 * @file WebInterfaceServer.cpp
 * @brief HTTP server wiring and network-facing endpoints for WebInterfaceModule.
 */

#include "WebInterfaceModule.h"

#include "Board/BoardSpec.h"
#include "App/BuildFlags.h"
#include "Core/FirmwareVersion.h"
#include "Core/Generated/RuntimeUiManifest_Generated.h"
#include "Core/Generated/RuntimeUiManifestJson_Generated.h"
#include "Core/I2cCfgProtocol.h"
#include "Core/Services/IAlarm.h"
#include "Core/SystemLimits.h"
#include "Core/SystemStats.h"
#if !defined(FLOW_PROFILE_MICRONOVA) && !defined(FLOW_PROFILE_WAVESHARE)
#include "Modules/Network/I2CCfgClientModule/I2CCfgClientRuntime.h"
#endif
#if defined(FLOW_PROFILE_MICRONOVA)
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerModuleDataModel.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#endif
#if defined(FLOW_PROFILE_WAVESHARE)
#include "Domain/Pool/PoolIds.h"
#include "Domain/Pool/PoolDomain.h"
#include "Core/Services/IPoolDevice.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#include "Profiles/Waveshare/WaveshareIoLayout.h"
#endif

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::WebInterfaceModule)
#include "Core/ModuleLog.h"

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include <memory>
#include <stdarg.h>
#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Modules/Network/TimeModule/TimeRuntime.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"

#ifndef FLOW_WEB_UNIFY_STATUS_CARD_ICONS
#define FLOW_WEB_UNIFY_STATUS_CARD_ICONS 0
#endif

#ifndef FLOW_WEB_HEAP_FORENSICS
#define FLOW_WEB_HEAP_FORENSICS 0
#endif

#ifndef TFT_FIRMW
#define TFT_FIRMW "0.0.0"
#endif

static void sanitizeJsonString_(char* s)
{
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') {
            s[i] = ' ';
        }
    }
}

static void printJsonEscaped_(Print& out, const char* s)
{
    out.print('\"');
    if (s) {
        for (const char* p = s; *p != '\0'; ++p) {
            switch (*p) {
            case '\"': out.print("\\\""); break;
            case '\\': out.print("\\\\"); break;
            case '\b': out.print("\\b"); break;
            case '\f': out.print("\\f"); break;
            case '\n': out.print("\\n"); break;
            case '\r': out.print("\\r"); break;
            case '\t': out.print("\\t"); break;
            default:
                if ((uint8_t)*p < 0x20U) {
                    out.print('?');
                } else {
                    out.print(*p);
                }
                break;
            }
        }
    }
    out.print('\"');
}

static bool parseBoolParam_(const char* in, bool fallback)
{
    if (!in || in[0] == '\0') return fallback;
    if (strcasecmp(in, "1") == 0 || strcasecmp(in, "true") == 0 || strcasecmp(in, "yes") == 0 ||
        strcasecmp(in, "on") == 0) {
        return true;
    }
    if (strcasecmp(in, "0") == 0 || strcasecmp(in, "false") == 0 || strcasecmp(in, "no") == 0 ||
        strcasecmp(in, "off") == 0) {
        return false;
    }
    return fallback;
}

static bool copyRequestParamValue_(AsyncWebServerRequest* request,
                                   const char* name,
                                   bool post,
                                   char* out,
                                   size_t outLen,
                                   const char* fallback = "")
{
    if (!out || outLen == 0U) return false;
    out[0] = '\0';
    if (!request || !name) {
        snprintf(out, outLen, "%s", fallback ? fallback : "");
        return false;
    }
    if (!request->hasParam(name, post)) {
        snprintf(out, outLen, "%s", fallback ? fallback : "");
        return false;
    }
    const AsyncWebParameter* param = request->getParam(name, post);
    if (!param) {
        snprintf(out, outLen, "%s", fallback ? fallback : "");
        return false;
    }
    const String& value = param->value();
    snprintf(out, outLen, "%s", value.c_str());
    return true;
}

static int32_t requestIntParam_(AsyncWebServerRequest* request,
                                const char* name,
                                int32_t fallback)
{
    if (!request || !name || !request->hasParam(name)) return fallback;
    const AsyncWebParameter* param = request->getParam(name);
    if (!param) return fallback;
    const String value = param->value();
    const char* raw = value.c_str();
    char* end = nullptr;
    const long parsed = strtol(raw, &end, 10);
    if (!end || end == raw) return fallback;
    return (int32_t)parsed;
}

#if defined(FLOW_PROFILE_WAVESHARE)
struct WebJsonBuffer final : public Print {
    explicit WebJsonBuffer(size_t requestedCapacity)
        : capacity_(requestedCapacity)
    {
        if (capacity_ == 0U || !psramFound()) return;
        data_ = static_cast<char*>(
            heap_caps_malloc(capacity_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
    }

    ~WebJsonBuffer() override
    {
        if (data_) heap_caps_free(data_);
    }

    WebJsonBuffer(const WebJsonBuffer&) = delete;
    WebJsonBuffer& operator=(const WebJsonBuffer&) = delete;

    size_t write(uint8_t value) override
    {
        return write(&value, 1U);
    }

    size_t write(const uint8_t* data, size_t size) override
    {
        if (!data_ || !data || size == 0U) return 0U;
        if (overflow_ || length_ >= capacity_ || size > (capacity_ - length_ - 1U)) {
            overflow_ = true;
            return 0U;
        }
        memcpy(data_ + length_, data, size);
        length_ += size;
        return size;
    }

    bool finish()
    {
        if (!data_ || overflow_ || length_ >= capacity_) return false;
        data_[length_] = '\0';
        return true;
    }

    void reset()
    {
        length_ = 0U;
        overflow_ = false;
        if (data_ && capacity_ > 0U) data_[0] = '\0';
    }

    bool valid() const { return data_ != nullptr; }
    bool overflowed() const { return overflow_; }
    size_t length() const { return length_; }
    size_t capacity() const { return capacity_; }

    size_t fillAt(uint8_t* out, size_t maxLen, size_t index) const
    {
        if (!out || maxLen == 0U || !data_ || index >= length_) return 0U;
        const size_t remaining = length_ - index;
        const size_t count = remaining < maxLen ? remaining : maxLen;
        memcpy(out, data_ + index, count);
        return count;
    }

private:
    char* data_ = nullptr;
    size_t capacity_ = 0U;
    size_t length_ = 0U;
    bool overflow_ = false;
};
#endif

template <size_t N>
static inline void sendProgmemLiteral_(AsyncWebServerRequest* request, const char* contentType, const char (&content)[N])
{
    if (!request || !contentType || N == 0U) return;
    request->send(200, contentType, reinterpret_cast<const uint8_t*>(content), N - 1U);
}

namespace {
constexpr uint32_t kHttpLatencyInfoMs = 40U;
constexpr uint32_t kHttpLatencyWarnMs = 1000U;
constexpr uint32_t kHttpLatencyFlowCfgInfoMs = 200U;
constexpr uint32_t kHttpLatencyFlowCfgWarnMs = 1000U;
constexpr uint32_t kHeapGuardAssetFreeBytesLight = 8192U;
constexpr uint32_t kHeapGuardAssetFreeBytesMinor = 12288U;
constexpr uint32_t kHeapGuardAssetFreeBytesMajor = 15360U;
constexpr uint32_t kHeapGuardAssetLargestBytesLight = 4096U;
constexpr uint32_t kHeapGuardAssetLargestBytesMinor = 6144U;
constexpr uint32_t kHeapGuardAssetLargestBytesMajor = 8192U;
#if defined(FLOW_PROFILE_WAVESHARE)
constexpr uint32_t kHeapGuardAssetInternalFreeBytesLight = 6144U;
constexpr uint32_t kHeapGuardAssetInternalFreeBytesMinor = 8192U;
constexpr uint32_t kHeapGuardAssetInternalFreeBytesMajor = 10240U;
constexpr uint32_t kHeapGuardAssetInternalLargestBytesLight = 3072U;
constexpr uint32_t kHeapGuardAssetInternalLargestBytesMinor = 4096U;
constexpr uint32_t kHeapGuardAssetInternalLargestBytesMajor = 6144U;
#endif
constexpr uint8_t kAssetBuildConcurrentLimit = 2U;
void (*gHttpActivityHook)(void*) = nullptr;
void* gHttpActivityHookCtx = nullptr;
portMUX_TYPE gAssetBuildMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t gAssetBuildInFlight = 0U;
volatile uint32_t gAssetBuildRejectCount = 0U;

struct WebHeapCharBuffer {
    explicit WebHeapCharBuffer(size_t requestedCapacity)
        : capacity(requestedCapacity)
    {
        if (capacity == 0U) return;
#if defined(FLOW_PROFILE_WAVESHARE)
        data = static_cast<char*>(
            heap_caps_calloc(1, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
#endif
        if (!data) {
            data = static_cast<char*>(heap_caps_calloc(1, capacity, MALLOC_CAP_8BIT));
        }
    }

    ~WebHeapCharBuffer()
    {
        if (data) heap_caps_free(data);
    }

    WebHeapCharBuffer(const WebHeapCharBuffer&) = delete;
    WebHeapCharBuffer& operator=(const WebHeapCharBuffer&) = delete;

    explicit operator bool() const { return data != nullptr; }

    char* data = nullptr;
    size_t capacity = 0;
};

struct BootLogJsonPageCtx {
    WebInterfaceModule* self = nullptr;
    AsyncResponseStream* response = nullptr;
    bool first = true;
    uint16_t count = 0;
};

static const char* activityDomainName_(uint8_t domain)
{
    switch ((ActivityDomain)domain) {
        case ActivityDomain::System: return "system";
        case ActivityDomain::PoolLogic: return "poollogic";
        case ActivityDomain::PoolDevice: return "pooldevice";
        case ActivityDomain::Alarm: return "alarm";
        default: return "unknown";
    }
}

static const char* activitySourceName_(uint8_t source)
{
    switch ((ActivitySource)source) {
        case ActivitySource::System: return "system";
        case ActivitySource::Auto: return "auto";
        case ActivitySource::Manual: return "manual";
        case ActivitySource::Scheduler: return "scheduler";
        case ActivitySource::Safety: return "safety";
        case ActivitySource::Pid: return "pid";
        case ActivitySource::Boot: return "boot";
        default: return "unknown";
    }
}

static const char* activitySeverityName_(uint8_t severity)
{
    switch ((ActivitySeverity)severity) {
        case ActivitySeverity::Info: return "info";
        case ActivitySeverity::Success: return "success";
        case ActivitySeverity::Warning: return "warning";
        case ActivitySeverity::Alarm: return "alarm";
        default: return "info";
    }
}

static const char* activityRoleName_(uint8_t role)
{
    switch ((ActivityRole)role) {
        case ActivityRole::None: return "none";
        case ActivityRole::Filtration: return "filtration";
        case ActivityRole::Swg: return "swg";
        case ActivityRole::Robot: return "robot";
        case ActivityRole::Filling: return "filling";
        case ActivityRole::Ph: return "ph";
        case ActivityRole::Disinfection: return "disinfection";
        case ActivityRole::Heater: return "heater";
        default: return "unknown";
    }
}

static const char* activityStateName_(uint8_t state)
{
    switch ((ActivityState)state) {
        case ActivityState::None: return "none";
        case ActivityState::RequestedOn: return "requested_on";
        case ActivityState::RequestedOff: return "requested_off";
        case ActivityState::On: return "on";
        case ActivityState::Off: return "off";
        default: return "unknown";
    }
}

struct ActivityJsonBufferWriter {
    ActivityJsonBufferWriter(char* output, size_t outputLen)
        : out(output), capacity(outputLen)
    {
        if (out && capacity > 0U) out[0] = '\0';
    }

    bool append(const char* text)
    {
        if (!ok || !text) return false;
        const size_t textLen = strlen(text);
        if (textLen >= remaining()) {
            ok = false;
            return false;
        }
        memcpy(out + length, text, textLen);
        length += textLen;
        out[length] = '\0';
        return true;
    }

    bool appendChar(char value)
    {
        if (!ok || remaining() <= 1U) {
            ok = false;
            return false;
        }
        out[length++] = value;
        out[length] = '\0';
        return true;
    }

    bool appendFormat(const char* format, ...)
    {
        if (!ok || !format || remaining() == 0U) {
            ok = false;
            return false;
        }
        va_list args;
        va_start(args, format);
        const int wrote = vsnprintf(out + length, remaining(), format, args);
        va_end(args);
        if (wrote < 0 || (size_t)wrote >= remaining()) {
            ok = false;
            return false;
        }
        length += (size_t)wrote;
        return true;
    }

    bool appendJsonString(const char* text)
    {
        if (!appendChar('"')) return false;
        if (text) {
            for (const char* p = text; *p != '\0' && ok; ++p) {
                switch (*p) {
                case '"': append("\\\""); break;
                case '\\': append("\\\\"); break;
                case '\b': append("\\b"); break;
                case '\f': append("\\f"); break;
                case '\n': append("\\n"); break;
                case '\r': append("\\r"); break;
                case '\t': append("\\t"); break;
                default:
                    appendChar(((uint8_t)*p < 0x20U) ? '?' : *p);
                    break;
                }
            }
        }
        return appendChar('"');
    }

    size_t remaining() const
    {
        return (out && length < capacity) ? (capacity - length) : 0U;
    }

    char* out = nullptr;
    size_t capacity = 0U;
    size_t length = 0U;
    bool ok = true;
};

struct ActivityLogChunkState {
    static constexpr size_t kPendingMax = 1024U;

    ~ActivityLogChunkState()
    {
        if (events) heap_caps_free(events);
    }

    bool begin(const ActivityLogService* activityLog,
               const ActivityLogStats& statsSnapshot,
               uint16_t requestedOffset,
               uint16_t requestedLimit,
               bool descending)
    {
        stats = statsSnapshot;
        offset = requestedOffset;
        limit = requestedLimit;
        newestFirst = descending;

        if (!activityLog || !activityLog->readPage || stats.count == 0U ||
            offset >= stats.count || limit == 0U) {
            return true;
        }

        const uint16_t remaining = (uint16_t)(stats.count - offset);
        capacity = (limit < remaining) ? limit : remaining;
        const size_t bytes = (size_t)capacity * sizeof(ActivityEvent);

#if defined(FLOW_PROFILE_WAVESHARE)
        events = static_cast<ActivityEvent*>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
        if (!events) {
            events = static_cast<ActivityEvent*>(
                heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
            );
        }
#else
        events = static_cast<ActivityEvent*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
#endif
        if (!events) return false;

        const uint16_t readOffset =
            newestFirst ? (uint16_t)(stats.count - offset - capacity) : offset;
        count = activityLog->readPage(activityLog->ctx,
                                      readOffset,
                                      capacity,
                                      &ActivityLogChunkState::snapshotWriter,
                                      this);
        return true;
    }

    size_t fill(uint8_t* buffer, size_t maxLen)
    {
        if (!buffer || maxLen == 0U) return 0U;

        size_t written = 0U;
        while (written < maxLen) {
            if (pendingPos < pendingLen) {
                const size_t room = maxLen - written;
                const size_t left = pendingLen - pendingPos;
                const size_t copyLen = (left < room) ? left : room;
                memcpy(buffer + written, pending + pendingPos, copyLen);
                written += copyLen;
                pendingPos += copyLen;
                continue;
            }
            if (!prepareNext()) break;
        }
        return written;
    }

private:
    enum class Stage : uint8_t {
        Header,
        Events,
        Suffix,
        Done,
    };

    static bool snapshotWriter(void* ctx,
                               const ActivityEvent& event,
                               uint16_t,
                               uint16_t)
    {
        ActivityLogChunkState* self = static_cast<ActivityLogChunkState*>(ctx);
        if (!self || !self->events || self->count >= self->capacity) return false;
        self->events[self->count++] = event;
        return true;
    }

    bool prepareNext()
    {
        pendingLen = 0U;
        pendingPos = 0U;

        while (stage != Stage::Done) {
            if (stage == Stage::Header) {
                const bool complete =
                    count == 0U || ((uint32_t)offset + (uint32_t)count >= stats.count);
                const int32_t next = complete ? -1 : (int32_t)offset + (int32_t)count;
                ActivityJsonBufferWriter out(pending, sizeof(pending));
                out.appendFormat(
                    "{\"available\":%s,\"capacity\":%u,\"entries\":%u,\"dropped\":%lu,"
                    "\"persisted\":%lu,\"persist_dropped\":%lu,\"psram\":%s,\"spiffs\":%s,"
                    "\"offset\":%u,\"limit\":%u,\"count\":%u,\"next\":",
                    stats.capacity > 0U ? "true" : "false",
                    (unsigned)stats.capacity,
                    (unsigned)stats.count,
                    (unsigned long)stats.droppedCount,
                    (unsigned long)stats.persistedCount,
                    (unsigned long)stats.persistDropCount,
                    stats.psram ? "true" : "false",
                    stats.spiffs ? "true" : "false",
                    (unsigned)offset,
                    (unsigned)limit,
                    (unsigned)count);
                if (next < 0) out.append("null");
                else out.appendFormat("%ld", (long)next);
                out.appendFormat(",\"complete\":%s,\"order\":\"%s\",\"events\":[",
                                 complete ? "true" : "false",
                                 newestFirst ? "desc" : "asc");
                if (!out.ok) return false;
                pendingLen = out.length;
                stage = Stage::Events;
                return pendingLen > 0U;
            }

            if (stage == Stage::Events) {
                if (eventIndex < count) {
                    const uint16_t storedIndex =
                        newestFirst ? (uint16_t)(count - 1U - eventIndex) : eventIndex;
                    if (!formatEvent(events[storedIndex], eventIndex > 0U)) {
                        snprintf(pending, sizeof(pending), "%snull", eventIndex > 0U ? "," : "");
                        pendingLen = strlen(pending);
                    }
                    ++eventIndex;
                    return pendingLen > 0U;
                }
                stage = Stage::Suffix;
                continue;
            }

            if (stage == Stage::Suffix) {
                memcpy(pending, "]}", 3U);
                pendingLen = 2U;
                stage = Stage::Done;
                return true;
            }
        }
        return false;
    }

    bool formatEvent(const ActivityEvent& event, bool prependComma)
    {
        ActivityJsonBufferWriter out(pending, sizeof(pending));
        if (prependComma) out.appendChar(',');
        out.appendFormat(
            "{\"seq\":%lu,\"ts_ms\":%lu,\"epoch_s\":%lu,\"code\":%u,\"alarm_id\":%u,"
            "\"domain\":%u,\"source\":%u,\"severity\":%u,\"role\":%u,"
            "\"state\":%u,\"reason\":%u,\"slot\":%u",
            (unsigned long)event.seq,
            (unsigned long)event.ts_ms,
            (unsigned long)event.epoch_s,
            (unsigned)event.code,
            (unsigned)event.alarmId,
            (unsigned)event.domain,
            (unsigned)event.source,
            (unsigned)event.severity,
            (unsigned)event.role,
            (unsigned)event.state,
            (unsigned)event.reason,
            (unsigned)event.targetSlot);
        out.append(",\"domain_name\":");
        out.appendJsonString(activityDomainName_(event.domain));
        out.append(",\"source_name\":");
        out.appendJsonString(activitySourceName_(event.source));
        out.append(",\"severity_name\":");
        out.appendJsonString(activitySeverityName_(event.severity));
        out.append(",\"role_name\":");
        out.appendJsonString(activityRoleName_(event.role));
        out.append(",\"state_name\":");
        out.appendJsonString(activityStateName_(event.state));
        out.append(",\"title\":");
        out.appendJsonString(event.title);
        out.append(",\"detail\":");
        out.appendJsonString(event.detail);
        out.append(",\"icon\":");
        out.appendJsonString(event.icon);
        out.appendChar('}');
        if (!out.ok) return false;
        pendingLen = out.length;
        return true;
    }

    ActivityLogStats stats{};
    ActivityEvent* events = nullptr;
    uint16_t capacity = 0U;
    uint16_t count = 0U;
    uint16_t offset = 0U;
    uint16_t limit = 0U;
    uint16_t eventIndex = 0U;
    bool newestFirst = false;
    Stage stage = Stage::Header;
    char pending[kPendingMax] = {0};
    size_t pendingLen = 0U;
    size_t pendingPos = 0U;
};

static void appendActivityModuleName_(char* out, size_t outLen, const char* moduleName)
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

static uint16_t summarizeConfigPatch_(const char* patchJson, char* modulesOut, size_t modulesOutLen)
{
    if (modulesOut && modulesOutLen > 0U) modulesOut[0] = '\0';
    if (!patchJson || patchJson[0] == '\0') return 0;

    DynamicJsonDocument doc(Limits::JsonConfigApplyBuf);
    const DeserializationError err = deserializeJson(doc, patchJson);
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
            appendActivityModuleName_(modulesOut, modulesOutLen, moduleName);
        }
    }
    return fieldCount;
}

const char* firmwareManifestCheckStateName_(FirmwareManifestCheckState state)
{
    switch (state) {
        case FirmwareManifestCheckState::Idle: return "idle";
        case FirmwareManifestCheckState::Queued: return "queued";
        case FirmwareManifestCheckState::Downloading: return "downloading";
        case FirmwareManifestCheckState::Ready: return "ready";
        case FirmwareManifestCheckState::Error: return "error";
        default: return "unknown";
    }
}

struct FirmwareManifestResponseChunkState {
    static constexpr size_t kPrefixReserve = 512U;

    ~FirmwareManifestResponseChunkState()
    {
        if (data) heap_caps_free(data);
    }

    bool begin(const FirmwareUpdateService* service,
               const FirmwareManifestCheckSnapshot& snapshot)
    {
        if (!service || !service->copyManifestResult ||
            snapshot.state != FirmwareManifestCheckState::Ready ||
            snapshot.payloadLen == 0U) {
            return false;
        }

        char safeUrl[sizeof(snapshot.manifestUrl)] = {0};
        char current[48] = {0};
        snprintf(safeUrl, sizeof(safeUrl), "%s", snapshot.manifestUrl);
        snprintf(current, sizeof(current), "%s", FirmwareVersion::Full);
        sanitizeJsonString_(safeUrl);
        sanitizeJsonString_(current);

        capacity = kPrefixReserve + snapshot.payloadLen + 2U;
#if defined(FLOW_PROFILE_WAVESHARE)
        data = static_cast<char*>(
            heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
#else
        data = static_cast<char*>(heap_caps_malloc(capacity, MALLOC_CAP_8BIT));
#endif
        if (!data) return false;

        const int prefixWritten =
            snprintf(data,
                     kPrefixReserve,
                     "{\"ok\":true,\"request_id\":%lu,\"state\":\"ready\","
                     "\"manifest_url\":\"%s\",\"current\":{\"flowios3\":\"%s\","
                     "\"esp32s3\":\"%s\",\"waveshare\":\"%s\"},\"manifest\":",
                     (unsigned long)snapshot.requestId,
                     safeUrl,
                     current,
                     current,
                     current);
        if (prefixWritten <= 0 || (size_t)prefixWritten >= kPrefixReserve) return false;

        const size_t prefixLen = (size_t)prefixWritten;
        size_t copiedLen = 0U;
        if (!service->copyManifestResult(service->ctx,
                                         snapshot.requestId,
                                         data + prefixLen,
                                         capacity - prefixLen,
                                         &copiedLen) ||
            copiedLen != snapshot.payloadLen) {
            return false;
        }

        length = prefixLen + copiedLen;
        data[length++] = '}';
        data[length] = '\0';
        return true;
    }

    size_t fill(uint8_t* buffer, size_t maxLen)
    {
        if (!buffer || maxLen == 0U || !data || position >= length) return 0U;
        const size_t remaining = length - position;
        const size_t count = (remaining < maxLen) ? remaining : maxLen;
        memcpy(buffer, data + position, count);
        position += count;
        return count;
    }

    char* data = nullptr;
    size_t capacity = 0U;
    size_t length = 0U;
    size_t position = 0U;
};

const char* httpMethodName_(uint32_t method);
void addNoCacheHeaders_(AsyncWebServerResponse* response);

size_t tokenLenToSlash_(const char* s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n] != '\0' && s[n] != '/') ++n;
    return n;
}

bool childTokenForPrefix_(const char* module,
                          const char* prefix,
                          size_t prefixLen,
                          const char*& childStart,
                          size_t& childLen,
                          bool& isExact)
{
    childStart = nullptr;
    childLen = 0;
    isExact = false;
    if (!module || module[0] == '\0') return false;

    if (prefixLen == 0) {
        childStart = module;
        childLen = tokenLenToSlash_(module);
        return childLen > 0;
    }

    if (strncmp(module, prefix, prefixLen) != 0) return false;
    const char sep = module[prefixLen];
    if (sep == '\0') {
        isExact = true;
        return false;
    }
    if (sep != '/') return false;

    childStart = module + prefixLen + 1;
    childLen = tokenLenToSlash_(childStart);
    return childLen > 0;
}

bool tokensEqual_(const char* a, size_t aLen, const char* b, size_t bLen)
{
    if (!a || !b) return false;
    if (aLen != bLen) return false;
    if (aLen == 0) return true;
    return strncmp(a, b, aLen) == 0;
}

struct HeapForensicSnapshot {
    uint32_t freeBytes = 0;
    uint32_t minFreeBytes = 0;
    uint32_t largestFreeBlock = 0;
};

struct SpiffsAssetForensicMeta {
    char assetName[24] = {0};
    uint32_t sizeBytes = 0;
    bool gzip = false;
};

HeapForensicSnapshot captureHeapForensicSnapshot_()
{
    HeapForensicSnapshot snap{};
    snap.freeBytes = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    snap.minFreeBytes = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    snap.largestFreeBlock = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    return snap;
}

const char* pathBaseName_(const char* path)
{
    if (!path || path[0] == '\0') return "-";
    const char* slash = strrchr(path, '/');
    return (slash && slash[1] != '\0') ? (slash + 1) : path;
}

bool hasPathSuffix_(const char* path, const char* suffix)
{
    if (!path || !suffix) return false;
    const size_t pathLen = strlen(path);
    const size_t suffixLen = strlen(suffix);
    return pathLen >= suffixLen && strcmp(path + (pathLen - suffixLen), suffix) == 0;
}

bool isMinorWebAssetPath_(const char* path)
{
    return hasPathSuffix_(path, ".svg") || hasPathSuffix_(path, ".png") || hasPathSuffix_(path, ".ico");
}

bool isLightWebAssetPath_(const char* path)
{
    return path &&
           (strstr(path, "/webinterface/light.") != nullptr ||
            strstr(path, "/webinterface/prov.") != nullptr);
}

uint32_t fnv1a32_(const char* text)
{
    uint32_t h = 0x811C9DC5u;
    if (!text) return h;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        h ^= (uint32_t)(*p);
        h *= 0x01000193u;
    }
    return h;
}

bool tryAcquireAssetBuildSlot_()
{
    bool acquired = false;
    portENTER_CRITICAL(&gAssetBuildMux);
    if (gAssetBuildInFlight < kAssetBuildConcurrentLimit) {
        gAssetBuildInFlight = gAssetBuildInFlight + 1U;
        acquired = true;
    } else {
        gAssetBuildRejectCount = gAssetBuildRejectCount + 1U;
    }
    portEXIT_CRITICAL(&gAssetBuildMux);
    return acquired;
}

void releaseAssetBuildSlot_()
{
    portENTER_CRITICAL(&gAssetBuildMux);
    if (gAssetBuildInFlight > 0U) {
        gAssetBuildInFlight = gAssetBuildInFlight - 1U;
    }
    portEXIT_CRITICAL(&gAssetBuildMux);
}

void sendTinyBusyJson_(AsyncWebServerRequest* request, const char* reason)
{
    if (!request) return;
    AsyncWebServerResponse* response =
        request->beginResponse(503, "application/json", "{\"ok\":false,\"err\":{\"code\":\"Busy\"}}");
    if (!response) {
        request->send(503, "text/plain", "Busy");
        return;
    }
    response->addHeader("Retry-After", "2");
    response->addHeader("X-Flow-Busy-Reason", reason ? reason : "busy");
    response->addHeader("Connection", "close");
    addNoCacheHeaders_(response);
    request->send(response);
}

bool shouldRejectAssetByFreeHeap_(const char* assetPath,
                                  uint32_t* freeBytesOut = nullptr,
                                  uint32_t* largestBytesOut = nullptr)
{
#if defined(FLOW_PROFILE_WAVESHARE)
    const uint32_t freeInternal = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t largestInternal =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (freeBytesOut) *freeBytesOut = freeInternal;
    if (largestBytesOut) *largestBytesOut = largestInternal;
#else
    const uint32_t freeBytes = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t largestBytes = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (freeBytesOut) *freeBytesOut = freeBytes;
    if (largestBytesOut) *largestBytesOut = largestBytes;
#endif
#if defined(FLOW_PROFILE_WAVESHARE)
    const uint32_t minInternalFreeBytes = isLightWebAssetPath_(assetPath)
        ? kHeapGuardAssetInternalFreeBytesLight
        : isMinorWebAssetPath_(assetPath)
        ? kHeapGuardAssetInternalFreeBytesMinor
        : kHeapGuardAssetInternalFreeBytesMajor;
    const uint32_t minInternalLargestBytes = isLightWebAssetPath_(assetPath)
        ? kHeapGuardAssetInternalLargestBytesLight
        : isMinorWebAssetPath_(assetPath)
        ? kHeapGuardAssetInternalLargestBytesMinor
        : kHeapGuardAssetInternalLargestBytesMajor;
    return freeInternal < minInternalFreeBytes || largestInternal < minInternalLargestBytes;
#else
    const uint32_t minFreeBytes = isLightWebAssetPath_(assetPath)
        ? kHeapGuardAssetFreeBytesLight
        : isMinorWebAssetPath_(assetPath)
        ? kHeapGuardAssetFreeBytesMinor
        : kHeapGuardAssetFreeBytesMajor;
    const uint32_t minLargestBytes = isLightWebAssetPath_(assetPath)
        ? kHeapGuardAssetLargestBytesLight
        : isMinorWebAssetPath_(assetPath)
        ? kHeapGuardAssetLargestBytesMinor
        : kHeapGuardAssetLargestBytesMajor;
    return freeBytes < minFreeBytes || largestBytes < minLargestBytes;
#endif
}

void buildProvisioningApSsid_(char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;
    const uint64_t chipId = ESP.getEfuseMac();
    const uint8_t b0 = (uint8_t)(chipId >> 16);
    const uint8_t b1 = (uint8_t)(chipId >> 8);
    const uint8_t b2 = (uint8_t)(chipId >> 0);
    snprintf(out, outLen, "flow.io-%s-%02X%02X%02X", FLOW_BUILD_PROFILE_NAME, b0, b1, b2);
}

bool isBlankText_(const char* text, size_t maxLen)
{
    if (!text) return true;
    const size_t len = strnlen(text, maxLen);
    if (len == 0U || len >= maxLen) return true;
    for (size_t i = 0; i < len; ++i) {
        if (!isspace((unsigned char)text[i])) return false;
    }
    return true;
}

void loadConfiguredDeviceName_(ConfigStore* cfgStore, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;
    snprintf(out, outLen, "flowio");
    if (!cfgStore) return;

    char systemJson[128] = {0};
    if (!cfgStore->toJsonModule("system", systemJson, sizeof(systemJson), nullptr, false)) return;

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, systemJson) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) return;

    const char* configured = doc.as<JsonObjectConst>()["devicename"] | "";
    if (isBlankText_(configured, outLen)) return;
    snprintf(out, outLen, "%s", configured);
}

bool isModuleFlagAndStringConfigured_(ConfigStore* cfgStore,
                                      const char* moduleName,
                                      const char* enabledKey,
                                      bool enabledDefault,
                                      const char* valueKey)
{
    if (!cfgStore || !moduleName || !enabledKey || !valueKey) return false;
    char moduleJson[512] = {0};
    if (!cfgStore->toJsonModule(moduleName, moduleJson, sizeof(moduleJson), nullptr, false)) return false;

    StaticJsonDocument<512> doc;
    const DeserializationError err = deserializeJson(doc, moduleJson);
    if (err || !doc.is<JsonObjectConst>()) return false;

    JsonObjectConst root = doc.as<JsonObjectConst>();
    const bool enabled = root[enabledKey] | enabledDefault;
    const char* value = root[valueKey] | "";
    return enabled && value && value[0] != '\0';
}

bool isProvisioningConfigured_(ConfigStore* cfgStore, bool requireMqtt)
{
    const bool wifiConfigured =
        isModuleFlagAndStringConfigured_(cfgStore, "wifi", "enabled", true, "ssid");
    if (!wifiConfigured) return false;
    if (!requireMqtt) return true;
    return isModuleFlagAndStringConfigured_(cfgStore, "mqtt", "enabled", false, "host");
}

void appendJsonFieldName_(Print& out, const char* key)
{
    out.print(",\"");
    out.print(key ? key : "");
    out.print("\":");
}

void appendJsonFieldValue_(Print& out, const char* key, JsonVariantConst value)
{
    appendJsonFieldName_(out, key);
    serializeJson(value, out);
}

bool dashboardSlotDegreeCUnit_(const char* unit);

bool sendFlowStatusCompactResponse_(AsyncWebServerRequest* request, const FlowCfgRemoteService* flowCfgSvc)
{
    if (!request || !flowCfgSvc || !flowCfgSvc->runtimeStatusDomainJson) return false;

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    addNoCacheHeaders_(response);
    response->print("{\"ok\":true");

    char domainBuf[640] = {0};
    StaticJsonDocument<768> domainDoc;
    bool anyDomainOk = false;
    char debugSummary[512] = {0};
    size_t debugPos = 0;

    auto domainName = [](FlowStatusDomain domain) -> const char* {
        switch (domain) {
        case FlowStatusDomain::System: return "system";
        case FlowStatusDomain::Wifi: return "wifi";
        case FlowStatusDomain::Mqtt: return "mqtt";
        case FlowStatusDomain::I2c: return "i2c";
        case FlowStatusDomain::Pool: return "pool";
        case FlowStatusDomain::Alarm: return "alarm";
        default: return "unknown";
        }
    };

    auto appendDebug = [&](const char* domain, const char* step, const char* detail) {
        if (!domain || !step || debugPos >= (sizeof(debugSummary) - 1U)) return;
        const char* safeDetail = (detail && detail[0] != '\0') ? detail : "";
        const bool hasDetail = safeDetail[0] != '\0';
        const int wrote = snprintf(debugSummary + debugPos,
                                   sizeof(debugSummary) - debugPos,
                                   "%s%s:%s%s%s",
                                   debugPos > 0U ? ";" : "",
                                   domain,
                                   step,
                                   hasDetail ? "=" : "",
                                   hasDetail ? safeDetail : "");
        if (wrote <= 0) return;
        const size_t delta = (size_t)wrote;
        if (delta >= (sizeof(debugSummary) - debugPos)) {
            debugPos = sizeof(debugSummary) - 1U;
        } else {
            debugPos += delta;
        }
    };

    auto loadDomain = [&](FlowStatusDomain domain) -> JsonObjectConst {
        const char* dname = domainName(domain);
        memset(domainBuf, 0, sizeof(domainBuf));
        domainDoc.clear();
        if (!flowCfgSvc->runtimeStatusDomainJson(flowCfgSvc->ctx, domain, domainBuf, sizeof(domainBuf))) {
            appendDebug(dname, "call_fail", domainBuf[0] ? "payload" : "");
            LOGW("flow.status domain=%s step=call_fail payload=%s",
                 dname,
                 domainBuf[0] ? domainBuf : "<empty>");
            return JsonObjectConst();
        }
        const DeserializationError err = deserializeJson(domainDoc, domainBuf);
        if (err || !domainDoc.is<JsonObjectConst>()) {
            appendDebug(dname, "json_fail", err ? err.c_str() : "not_object");
            LOGW("flow.status domain=%s step=json_fail err=%s payload=%s",
                 dname,
                 err ? err.c_str() : "not_object",
                 domainBuf[0] ? domainBuf : "<empty>");
            domainDoc.clear();
            return JsonObjectConst();
        }
        JsonObjectConst root = domainDoc.as<JsonObjectConst>();
        if (!(root["ok"] | false)) {
            appendDebug(dname, "ok_false", "payload");
            LOGW("flow.status domain=%s step=ok_false payload=%s",
                 dname,
                 domainBuf[0] ? domainBuf : "<empty>");
            domainDoc.clear();
            return JsonObjectConst();
        }
        appendDebug(dname, "ok", "");
        anyDomainOk = true;
        return root;
    };

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::System);
        if (!root.isNull()) {
            appendJsonFieldName_(*response, "fw");
            printJsonEscaped_(*response, root["fw"] | "");
            appendJsonFieldValue_(*response, "upms", root["upms"]);
            response->print(",\"heap\":{");
            JsonObjectConst heapIn = root["heap"];
            response->print("\"free\":");
            serializeJson(heapIn["free"], *response);
            appendJsonFieldValue_(*response, "min_free", heapIn["min_free"]);
            response->print('}');
        }
    }

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::Wifi);
        if (!root.isNull()) {
            JsonObjectConst wifiIn = root["wifi"];
            response->print(",\"wifi\":{");
            response->print("\"rdy\":");
            serializeJson(wifiIn["rdy"], *response);
            appendJsonFieldName_(*response, "ip");
            printJsonEscaped_(*response, wifiIn["ip"] | "");
            appendJsonFieldName_(*response, "mac");
            printJsonEscaped_(*response, wifiIn["mac"] | "");
            appendJsonFieldValue_(*response, "hrss", wifiIn["hrss"]);
            appendJsonFieldValue_(*response, "rssi", wifiIn["rssi"]);
            response->print('}');
        }
    }

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::Mqtt);
        if (!root.isNull()) {
            JsonObjectConst mqttIn = root["mqtt"];
            response->print(",\"mqtt\":{");
            response->print("\"rdy\":");
            serializeJson(mqttIn["rdy"], *response);
            appendJsonFieldName_(*response, "srv");
            printJsonEscaped_(*response, mqttIn["srv"] | "");
            appendJsonFieldValue_(*response, "rxdrp", mqttIn["rxdrp"]);
            appendJsonFieldValue_(*response, "prsf", mqttIn["prsf"]);
            appendJsonFieldValue_(*response, "hndf", mqttIn["hndf"]);
            appendJsonFieldValue_(*response, "ovr", mqttIn["ovr"]);
            response->print('}');
        }
    }

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::Pool);
        if (!root.isNull()) {
            JsonObjectConst poolIn = root["pool"];
            response->print(",\"pool\":{");
            response->print("\"has\":");
            serializeJson(poolIn["has"], *response);
            appendJsonFieldValue_(*response, "auto", poolIn["auto"]);
            appendJsonFieldValue_(*response, "wint", poolIn["wint"]);
            appendJsonFieldValue_(*response, "wat", poolIn["wat"]);
            appendJsonFieldValue_(*response, "air", poolIn["air"]);
            appendJsonFieldValue_(*response, "ph", poolIn["ph"]);
            appendJsonFieldValue_(*response, "orp", poolIn["orp"]);
            appendJsonFieldValue_(*response, "fil", poolIn["fil"]);
            appendJsonFieldValue_(*response, "php", poolIn["php"]);
            appendJsonFieldValue_(*response, "clp", poolIn["clp"]);
            appendJsonFieldValue_(*response, "rbt", poolIn["rbt"]);
            response->print('}');
        }
    }

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::I2c);
        if (!root.isNull()) {
            JsonObjectConst i2cIn = root["i2c"];
            response->print(",\"i2c\":{");
            response->print("\"lnk\":");
            serializeJson(i2cIn["lnk"], *response);
            appendJsonFieldValue_(*response, "seen", i2cIn["seen"]);
            appendJsonFieldValue_(*response, "req", i2cIn["req"]);
            appendJsonFieldValue_(*response, "breq", i2cIn["breq"]);
            appendJsonFieldValue_(*response, "ago", i2cIn["ago"]);
            response->print('}');
        }
    }

    if (!anyDomainOk) {
        delete response;
        char errJson[768] = {0};
        snprintf(errJson,
                 sizeof(errJson),
                 "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.status\",\"detail\":\"%s\"}}",
                 debugSummary[0] ? debugSummary : "no_domain_ok");
        LOGW("flow.status aggregate failed detail=%s", debugSummary[0] ? debugSummary : "no_domain_ok");
        // Keep HTTP 200 with structured payload so UI can degrade gracefully
        // without browser-level network error noise.
        request->send(200, "application/json", errJson);
        return false;
    }

    response->print('}');
    LOGI("flow.status aggregate ok detail=%s",
         debugSummary[0] ? debugSummary : "ok");
    request->send(response);
    return true;
}

void fillSpiffsAssetForensicMeta_(SpiffsAssetForensicMeta* out,
                                  const char* servedPath,
                                  uint32_t sizeBytes,
                                  bool gzip)
{
    if (!out) return;
    snprintf(out->assetName, sizeof(out->assetName), "%s", pathBaseName_(servedPath));
    out->sizeBytes = sizeBytes;
    out->gzip = gzip;
}

#if FLOW_WEB_HEAP_FORENSICS
void logHttpHeapForensic_(AsyncWebServerRequest* req,
                          const char* route,
                          uint32_t startUs,
                          const HeapForensicSnapshot& startHeap)
{
    const HeapForensicSnapshot endHeap = captureHeapForensicSnapshot_();
    const uint32_t elapsedUs = micros() - startUs;
    const long deltaFree = (long)endHeap.freeBytes - (long)startHeap.freeBytes;
    const uint32_t lowWaterDrop =
        (startHeap.minFreeBytes > endHeap.minFreeBytes) ? (startHeap.minFreeBytes - endHeap.minFreeBytes) : 0U;
    const char* method = req ? httpMethodName_(req->method()) : "?";
    LOGW("HTTPfx %s %s us=%lu f0=%lu f1=%lu df=%ld m1=%lu lo=%lu",
         method,
         route ? route : "?",
         (unsigned long)elapsedUs,
         (unsigned long)startHeap.freeBytes,
         (unsigned long)endHeap.freeBytes,
         deltaFree,
         (unsigned long)endHeap.minFreeBytes,
         (unsigned long)lowWaterDrop);
}

void logSpiffsAssetHeapForensic_(const char* stage,
                                 const SpiffsAssetForensicMeta& meta,
                                 uint32_t startUs,
                                 const HeapForensicSnapshot& startHeap)
{
    const HeapForensicSnapshot endHeap = captureHeapForensicSnapshot_();
    const uint32_t elapsedUs = micros() - startUs;
    const long deltaFree = (long)endHeap.freeBytes - (long)startHeap.freeBytes;
    const uint32_t lowWaterDrop =
        (startHeap.minFreeBytes > endHeap.minFreeBytes) ? (startHeap.minFreeBytes - endHeap.minFreeBytes) : 0U;
    LOGW("ASfx %s %s u=%lu f0=%lu f1=%lu d=%ld m=%lu l=%lu s=%lu z=%u",
         stage ? stage : "?",
         meta.assetName[0] ? meta.assetName : "-",
         (unsigned long)elapsedUs,
         (unsigned long)startHeap.freeBytes,
         (unsigned long)endHeap.freeBytes,
         deltaFree,
         (unsigned long)endHeap.minFreeBytes,
         (unsigned long)lowWaterDrop,
         (unsigned long)meta.sizeBytes,
         meta.gzip ? 1U : 0U);
}
#endif

uint32_t webAssetFingerprintFile_(uint32_t hash, const char* path)
{
    if (!path || path[0] == '\0' || !SPIFFS.exists(path)) {
        return hash ^ 0x9E3779B9UL;
    }

    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        return hash ^ 0x85EBCA6BUL;
    }

    hash ^= (uint32_t)file.size();
    hash *= 16777619UL;

    uint8_t buffer[128];
    while (file.available()) {
        const size_t got = file.read(buffer, sizeof(buffer));
        for (size_t i = 0; i < got; ++i) {
            hash ^= (uint32_t)buffer[i];
            hash *= 16777619UL;
        }
    }
    return hash;
}

const char* webAssetVersion_()
{
    static char version[64] = {0};
    if (version[0] != '\0') return version;

    uint32_t hash = 2166136261UL;
    hash = webAssetFingerprintFile_(hash, "/webinterface/app-core.js.gz");
    hash = webAssetFingerprintFile_(hash, "/webinterface/app-core.css.gz");
    hash = webAssetFingerprintFile_(hash, "/webinterface/sh.html.gz");
    hash = webAssetFingerprintFile_(hash, "/webinterface/app.js.gz");
    hash = webAssetFingerprintFile_(hash, "/wc/i.j.gz");
    snprintf(version, sizeof(version), "%s-%08lx", FirmwareVersion::BuildRef, (unsigned long)hash);
    return version;
}

void addNoCacheHeaders_(AsyncWebServerResponse* response)
{
    if (!response) return;
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
}

void addVersionedAssetCacheHeaders_(AsyncWebServerResponse* response)
{
    if (!response) return;
    response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
}

bool isCurrentWebAssetVersionRequest_(AsyncWebServerRequest* request)
{
    if (!request || !request->hasParam("v")) return false;
    const AsyncWebParameter* versionParam = request->getParam("v");
    if (!versionParam) return false;
    const char* currentVersion = webAssetVersion_();
    if (!currentVersion || currentVersion[0] == '\0') return false;
    const String& requestedVersion = versionParam->value();
    return requestedVersion.length() == strlen(currentVersion) &&
           strcmp(requestedVersion.c_str(), currentVersion) == 0;
}

void addCacheAwareAssetHeaders_(AsyncWebServerRequest* request, AsyncWebServerResponse* response)
{
    if (!request || !response) return;
    if (isCurrentWebAssetVersionRequest_(request)) {
        addVersionedAssetCacheHeaders_(response);
    } else {
        addNoCacheHeaders_(response);
    }
}

int flowCfgApplyHttpStatus_(const char* ackJson)
{
    if (!ackJson || ackJson[0] == '\0') return 500;
    if (strstr(ackJson, "\"code\":\"BadCfgJson\"")) return 400;
    if (strstr(ackJson, "\"code\":\"ArgsTooLarge\"") || strstr(ackJson, "\"code\":\"CfgTruncated\"")) return 413;
    if (strstr(ackJson, "\"code\":\"NotReady\"")) return 503;
    if (strstr(ackJson, "\"code\":\"CfgApplyFailed\"")) return 409;
    if (strstr(ackJson, "\"code\":\"IoError\"")) return 502;
    if (strstr(ackJson, "\"code\":\"Failed\"")) return 502;
    return 500;
}

bool parseFlowStatusDomainParam_(const char* raw, FlowStatusDomain& domainOut)
{
    if (!raw || raw[0] == '\0') return false;
    if (strcasecmp(raw, "system") == 0) {
        domainOut = FlowStatusDomain::System;
        return true;
    }
    if (strcasecmp(raw, "wifi") == 0) {
        domainOut = FlowStatusDomain::Wifi;
        return true;
    }
    if (strcasecmp(raw, "mqtt") == 0) {
        domainOut = FlowStatusDomain::Mqtt;
        return true;
    }
    if (strcasecmp(raw, "i2c") == 0) {
        domainOut = FlowStatusDomain::I2c;
        return true;
    }
    if (strcasecmp(raw, "pool") == 0) {
        domainOut = FlowStatusDomain::Pool;
        return true;
    }
    return false;
}

const char* httpMethodName_(uint32_t method)
{
    switch (method) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "OTHER";
    }
}

const char* runtimeUiWireTypeName_(RuntimeUiWireType type)
{
    switch (type) {
    case RuntimeUiWireType::NotFound: return "not_found";
    case RuntimeUiWireType::Unavailable: return "unavailable";
    case RuntimeUiWireType::Bool: return "bool";
    case RuntimeUiWireType::Int32: return "int32";
    case RuntimeUiWireType::UInt32: return "uint32";
    case RuntimeUiWireType::Float32: return "float";
    case RuntimeUiWireType::Enum: return "enum";
    case RuntimeUiWireType::String: return "string";
    default: return "unknown";
    }
}

size_t runtimeUiWireEstimate_(const RuntimeUiManifestItem* item)
{
    if (!item || !item->type) return 20U;
    if (strcmp(item->type, "bool") == 0) return 4U;
    if (strcmp(item->type, "enum") == 0) return 4U;
    if (strcmp(item->type, "int32") == 0) return 7U;
    if (strcmp(item->type, "uint32") == 0) return 7U;
    if (strcmp(item->type, "float") == 0) return 7U;
    if (strcmp(item->type, "string") == 0) {
        if (strcmp(item->key, "mqtt.server") == 0) return 72U;
        return 24U;
    }
    return 20U;
}

uint32_t readLe32_(const uint8_t* in)
{
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

bool appendRuntimeUiJsonValues_(JsonArray values, const uint8_t* payload, size_t payloadLen)
{
    if (!payload || payloadLen == 0U) return true;

    size_t offset = 0U;
    const uint8_t count = payload[offset++];
    for (uint8_t i = 0; i < count; ++i) {
        if ((offset + 3U) > payloadLen) return false;
        const RuntimeUiId runtimeId = (RuntimeUiId)((RuntimeUiId)payload[offset] |
                                                    ((RuntimeUiId)payload[offset + 1U] << 8));
        offset += 2U;
        const RuntimeUiWireType wireType = (RuntimeUiWireType)payload[offset++];
        const RuntimeUiManifestItem* manifestItem = findRuntimeUiManifestItem(runtimeId);

        JsonObject value = values.createNestedObject();
        value["id"] = runtimeId;
        if (manifestItem) {
            value["key"] = manifestItem->key;
            value["type"] = manifestItem->type;
            if (manifestItem->unit && manifestItem->unit[0] != '\0') {
                value["unit"] = manifestItem->unit;
            }
        } else {
            value["type"] = runtimeUiWireTypeName_(wireType);
        }

        switch (wireType) {
        case RuntimeUiWireType::NotFound:
            value["status"] = "not_found";
            break;

        case RuntimeUiWireType::Unavailable:
            value["status"] = "unavailable";
            break;

        case RuntimeUiWireType::Bool:
            if ((offset + 1U) > payloadLen) return false;
            value["value"] = (payload[offset++] != 0U);
            break;

        case RuntimeUiWireType::Int32: {
            if ((offset + 4U) > payloadLen) return false;
            int32_t raw = 0;
            const uint32_t bits = readLe32_(payload + offset);
            memcpy(&raw, &bits, sizeof(raw));
            value["value"] = raw;
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::UInt32:
            if ((offset + 4U) > payloadLen) return false;
            value["value"] = readLe32_(payload + offset);
            offset += 4U;
            break;

        case RuntimeUiWireType::Float32: {
            if ((offset + 4U) > payloadLen) return false;
            const uint32_t bits = readLe32_(payload + offset);
            float raw = 0.0f;
            memcpy(&raw, &bits, sizeof(raw));
            value["value"] = raw;
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::Enum:
            if ((offset + 1U) > payloadLen) return false;
            value["value"] = payload[offset++];
            break;

        case RuntimeUiWireType::String: {
            if ((offset + 1U) > payloadLen) return false;
            const uint8_t len = payload[offset++];
            if ((offset + len) > payloadLen) return false;
            char text[I2cCfgProtocol::MaxPayload + 1U] = {0};
            memcpy(text, payload + offset, len);
            text[len] = '\0';
            value["value"] = text;
            offset += len;
            break;
        }

        default:
            return false;
        }
    }

    return offset == payloadLen;
}

bool appendRuntimeUiJsonValuesToStream_(Print& out, const uint8_t* payload, size_t payloadLen, bool& firstValue)
{
    if (!payload || payloadLen == 0U) return true;

    size_t offset = 0U;
    const uint8_t count = payload[offset++];
    for (uint8_t i = 0; i < count; ++i) {
        if ((offset + 3U) > payloadLen) return false;
        const RuntimeUiId runtimeId = (RuntimeUiId)((RuntimeUiId)payload[offset] |
                                                    ((RuntimeUiId)payload[offset + 1U] << 8));
        offset += 2U;
        const RuntimeUiWireType wireType = (RuntimeUiWireType)payload[offset++];
        const RuntimeUiManifestItem* manifestItem = findRuntimeUiManifestItem(runtimeId);

        if (!firstValue) out.print(',');
        firstValue = false;

        out.print('{');
        out.print("\"id\":");
        out.print((unsigned)runtimeId);
        out.print(",\"key\":");
        printJsonEscaped_(out, manifestItem ? manifestItem->key : "");
        out.print(",\"type\":");
        printJsonEscaped_(out, manifestItem ? manifestItem->type : runtimeUiWireTypeName_(wireType));
        if (manifestItem && manifestItem->unit && manifestItem->unit[0] != '\0') {
            out.print(",\"unit\":");
            printJsonEscaped_(out, manifestItem->unit);
        }

        switch (wireType) {
        case RuntimeUiWireType::NotFound:
            out.print(",\"status\":\"not_found\"}");
            break;

        case RuntimeUiWireType::Unavailable:
            out.print(",\"status\":\"unavailable\"}");
            break;

        case RuntimeUiWireType::Bool:
            if ((offset + 1U) > payloadLen) return false;
            out.print(",\"value\":");
            out.print((payload[offset++] != 0U) ? "true" : "false");
            out.print('}');
            break;

        case RuntimeUiWireType::Int32: {
            if ((offset + 4U) > payloadLen) return false;
            int32_t raw = 0;
            const uint32_t bits = readLe32_(payload + offset);
            memcpy(&raw, &bits, sizeof(raw));
            out.print(",\"value\":");
            out.print((int32_t)raw);
            out.print('}');
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::UInt32:
            if ((offset + 4U) > payloadLen) return false;
            out.print(",\"value\":");
            out.print((unsigned long)readLe32_(payload + offset));
            out.print('}');
            offset += 4U;
            break;

        case RuntimeUiWireType::Float32: {
            if ((offset + 4U) > payloadLen) return false;
            const uint32_t bits = readLe32_(payload + offset);
            float raw = 0.0f;
            memcpy(&raw, &bits, sizeof(raw));
            out.print(",\"value\":");
            out.print(raw, 3);
            out.print('}');
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::Enum:
            if ((offset + 1U) > payloadLen) return false;
            out.print(",\"value\":");
            out.print((unsigned)payload[offset++]);
            out.print('}');
            break;

        case RuntimeUiWireType::String: {
            if ((offset + 1U) > payloadLen) return false;
            const uint8_t len = payload[offset++];
            if ((offset + len) > payloadLen) return false;
            char text[I2cCfgProtocol::MaxPayload + 1U] = {0};
            memcpy(text, payload + offset, len);
            text[len] = '\0';
            out.print(",\"value\":");
            printJsonEscaped_(out, text);
            out.print('}');
            offset += len;
            break;
        }

        default:
            return false;
        }
    }

    return offset == payloadLen;
}

constexpr size_t kMaxRuntimeHttpIds = 48U;

#if !defined(FLOW_PROFILE_MICRONOVA)
void printRuntimeValuePrefix_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, const char* type, const char* unit)
{
    if (!firstValue) out.print(',');
    firstValue = false;
    out.print("{\"id\":");
    out.print((unsigned)id);
    out.print(",\"key\":");
    printJsonEscaped_(out, key);
    out.print(",\"type\":");
    printJsonEscaped_(out, type);
    if (unit && unit[0] != '\0') {
        out.print(",\"unit\":");
        printJsonEscaped_(out, unit);
    }
}

void printRuntimeBool_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, bool value)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "bool", nullptr);
    out.print(",\"value\":");
    out.print(value ? "true" : "false");
    out.print('}');
}

void printRuntimeI32_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, int32_t value, const char* unit = nullptr)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "int32", unit);
    out.print(",\"value\":");
    out.print((int32_t)value);
    out.print('}');
}

void printRuntimeU32_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, uint32_t value, const char* unit = nullptr)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "uint32", unit);
    out.print(",\"value\":");
    out.print((unsigned long)value);
    out.print('}');
}

void printRuntimeF32_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, float value, const char* unit = nullptr)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "float", unit);
    out.print(",\"value\":");
    out.print(value, 3);
    out.print('}');
}

void printRuntimeString_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, const char* value)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "string", nullptr);
    out.print(",\"value\":");
    printJsonEscaped_(out, value ? value : "");
    out.print('}');
}

void printRuntimeUnavailable_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, const char* type)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, type, nullptr);
    out.print(",\"status\":\"unavailable\"}");
}
#endif

#if defined(FLOW_PROFILE_MICRONOVA)
constexpr RuntimeUiId kMicronovaRuntimeIdOnline = makeRuntimeUiId(ModuleId::MicronovaBoiler, 1);
constexpr RuntimeUiId kMicronovaRuntimeIdStateCode = makeRuntimeUiId(ModuleId::MicronovaBoiler, 2);
constexpr RuntimeUiId kMicronovaRuntimeIdStateText = makeRuntimeUiId(ModuleId::MicronovaBoiler, 3);
constexpr RuntimeUiId kMicronovaRuntimeIdPowerState = makeRuntimeUiId(ModuleId::MicronovaBoiler, 4);
constexpr RuntimeUiId kMicronovaRuntimeIdPowerLevel = makeRuntimeUiId(ModuleId::MicronovaBoiler, 5);
constexpr RuntimeUiId kMicronovaRuntimeIdFanSpeed = makeRuntimeUiId(ModuleId::MicronovaBoiler, 6);
constexpr RuntimeUiId kMicronovaRuntimeIdTargetTemp = makeRuntimeUiId(ModuleId::MicronovaBoiler, 7);
constexpr RuntimeUiId kMicronovaRuntimeIdRoomTemp = makeRuntimeUiId(ModuleId::MicronovaBoiler, 8);
constexpr RuntimeUiId kMicronovaRuntimeIdFumesTemp = makeRuntimeUiId(ModuleId::MicronovaBoiler, 9);
constexpr RuntimeUiId kMicronovaRuntimeIdWaterTemp = makeRuntimeUiId(ModuleId::MicronovaBoiler, 10);
constexpr RuntimeUiId kMicronovaRuntimeIdWaterPressure = makeRuntimeUiId(ModuleId::MicronovaBoiler, 11);
constexpr RuntimeUiId kMicronovaRuntimeIdAlarmCode = makeRuntimeUiId(ModuleId::MicronovaBoiler, 12);
constexpr RuntimeUiId kMicronovaRuntimeIdLastUpdateMs = makeRuntimeUiId(ModuleId::MicronovaBoiler, 13);
constexpr RuntimeUiId kMicronovaRuntimeIdLastCommand = makeRuntimeUiId(ModuleId::MicronovaBoiler, 14);

const char kMicronovaRuntimeManifestJson[] PROGMEM = R"json({
  "ok": true,
  "values": [
    {"id":2901,"runtimeId":2901,"moduleId":29,"module":"micronova.boiler","valueId":1,"key":"micronova.online","label":"Connexion chaudière","type":"bool","domain":"micronova","group":"Chaudière","unit":null,"decimals":null,"order":10,"display":"boolean"},
    {"id":2902,"runtimeId":2902,"moduleId":29,"module":"micronova.boiler","valueId":2,"key":"micronova.stove_state_code","label":"Code état","type":"int32","domain":"micronova","group":"Chaudière","unit":null,"decimals":0,"order":20},
    {"id":2903,"runtimeId":2903,"moduleId":29,"module":"micronova.boiler","valueId":3,"key":"micronova.stove_state_text","label":"État","type":"string","domain":"micronova","group":"Chaudière","unit":null,"decimals":null,"order":30,"display":"badge"},
    {"id":2904,"runtimeId":2904,"moduleId":29,"module":"micronova.boiler","valueId":4,"key":"micronova.power_state","label":"Marche","type":"string","domain":"micronova","group":"Chaudière","unit":null,"decimals":null,"order":40,"display":"badge"},
    {"id":2905,"runtimeId":2905,"moduleId":29,"module":"micronova.boiler","valueId":5,"key":"micronova.power_level","label":"Puissance","type":"int32","domain":"micronova","group":"Commandes","unit":null,"decimals":0,"order":50},
    {"id":2906,"runtimeId":2906,"moduleId":29,"module":"micronova.boiler","valueId":6,"key":"micronova.fan_speed","label":"Ventilation","type":"int32","domain":"micronova","group":"Commandes","unit":null,"decimals":0,"order":60},
    {"id":2907,"runtimeId":2907,"moduleId":29,"module":"micronova.boiler","valueId":7,"key":"micronova.target_temperature","label":"Consigne","type":"int32","domain":"micronova","group":"Températures","unit":"°C","decimals":0,"order":70},
    {"id":2908,"runtimeId":2908,"moduleId":29,"module":"micronova.boiler","valueId":8,"key":"micronova.room_temperature","label":"Température ambiante","type":"float","domain":"micronova","group":"Températures","unit":"°C","decimals":1,"order":80},
    {"id":2909,"runtimeId":2909,"moduleId":29,"module":"micronova.boiler","valueId":9,"key":"micronova.fumes_temperature","label":"Température fumées","type":"float","domain":"micronova","group":"Températures","unit":"°C","decimals":1,"order":90},
    {"id":2910,"runtimeId":2910,"moduleId":29,"module":"micronova.boiler","valueId":10,"key":"micronova.water_temperature","label":"Température eau","type":"float","domain":"micronova","group":"Hydraulique","unit":"°C","decimals":1,"order":100},
    {"id":2911,"runtimeId":2911,"moduleId":29,"module":"micronova.boiler","valueId":11,"key":"micronova.water_pressure","label":"Pression eau","type":"float","domain":"micronova","group":"Hydraulique","unit":"bar","decimals":2,"order":110},
    {"id":2912,"runtimeId":2912,"moduleId":29,"module":"micronova.boiler","valueId":12,"key":"micronova.alarm_code","label":"Code alarme","type":"int32","domain":"micronova","group":"Alarmes","unit":null,"decimals":0,"order":120},
    {"id":2913,"runtimeId":2913,"moduleId":29,"module":"micronova.boiler","valueId":13,"key":"micronova.last_update_ms","label":"Dernière lecture","type":"uint32","domain":"micronova","group":"Chaudière","unit":"ms","decimals":0,"order":130},
    {"id":2914,"runtimeId":2914,"moduleId":29,"module":"micronova.boiler","valueId":14,"key":"micronova.last_command","label":"Dernière commande","type":"string","domain":"micronova","group":"Commandes","unit":null,"decimals":null,"order":140,"display":"badge"},
    {"id":2001,"runtimeId":2001,"moduleId":20,"module":"mqtt","valueId":1,"key":"mqtt.ready","label":"MQTT connecté","type":"bool","domain":"mqtt","group":"MQTT","unit":null,"decimals":null,"order":10,"display":"boolean"},
    {"id":2003,"runtimeId":2003,"moduleId":20,"module":"mqtt","valueId":3,"key":"mqtt.rx_drop","label":"Messages ignorés","type":"uint32","domain":"mqtt","group":"MQTT","unit":null,"decimals":0,"order":30},
    {"id":2004,"runtimeId":2004,"moduleId":20,"module":"mqtt","valueId":4,"key":"mqtt.parse_fail","label":"Payloads invalides","type":"uint32","domain":"mqtt","group":"MQTT","unit":null,"decimals":0,"order":40},
    {"id":2005,"runtimeId":2005,"moduleId":20,"module":"mqtt","valueId":5,"key":"mqtt.handler_fail","label":"Commandes refusées","type":"uint32","domain":"mqtt","group":"MQTT","unit":null,"decimals":0,"order":50},
    {"id":2006,"runtimeId":2006,"moduleId":20,"module":"mqtt","valueId":6,"key":"mqtt.oversize_drop","label":"Messages trop grands","type":"uint32","domain":"mqtt","group":"MQTT","unit":null,"decimals":0,"order":60},
    {"id":1701,"runtimeId":1701,"moduleId":17,"module":"system","valueId":1,"key":"system.firmware","label":"Firmware","type":"string","domain":"system","group":"Système","unit":null,"decimals":null,"order":10},
    {"id":1702,"runtimeId":1702,"moduleId":17,"module":"system","valueId":2,"key":"system.uptime_ms","label":"Uptime","type":"uint32","domain":"system","group":"Système","unit":"ms","decimals":0,"order":20},
    {"id":1703,"runtimeId":1703,"moduleId":17,"module":"system","valueId":3,"key":"system.heap_free","label":"Heap libre","type":"uint32","domain":"system","group":"Mémoire","unit":"B","decimals":0,"order":30},
    {"id":1704,"runtimeId":1704,"moduleId":17,"module":"system","valueId":4,"key":"system.heap_min_free","label":"Heap minimum","type":"uint32","domain":"system","group":"Mémoire","unit":"B","decimals":0,"order":40},
    {"id":1001,"runtimeId":1001,"moduleId":10,"module":"wifi","valueId":1,"key":"wifi.ready","label":"Réseau connecté","type":"bool","domain":"wifi","group":"Réseau","unit":null,"decimals":null,"order":10,"display":"boolean"},
    {"id":1002,"runtimeId":1002,"moduleId":10,"module":"wifi","valueId":2,"key":"wifi.ip","label":"Adresse IP","type":"string","domain":"wifi","group":"Réseau","unit":null,"decimals":null,"order":20},
    {"id":1003,"runtimeId":1003,"moduleId":10,"module":"wifi","valueId":3,"key":"wifi.rssi","label":"RSSI","type":"int32","domain":"wifi","group":"Réseau","unit":"dBm","decimals":0,"order":30},
    {"id":1004,"runtimeId":1004,"moduleId":10,"module":"wifi","valueId":4,"key":"network.type","label":"Type","type":"string","domain":"wifi","group":"Réseau","unit":null,"decimals":null,"order":40}
  ]
})json";

void printRuntimeValuePrefix_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, const char* type, const char* unit)
{
    if (!firstValue) out.print(',');
    firstValue = false;
    out.print("{\"id\":");
    out.print((unsigned)id);
    out.print(",\"key\":");
    printJsonEscaped_(out, key);
    out.print(",\"type\":");
    printJsonEscaped_(out, type);
    if (unit && unit[0] != '\0') {
        out.print(",\"unit\":");
        printJsonEscaped_(out, unit);
    }
}

void printRuntimeBool_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, bool value)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "bool", nullptr);
    out.print(",\"value\":");
    out.print(value ? "true" : "false");
    out.print('}');
}

void printRuntimeI32_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, int32_t value, const char* unit = nullptr)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "int32", unit);
    out.print(",\"value\":");
    out.print((int32_t)value);
    out.print('}');
}

void printRuntimeU32_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, uint32_t value, const char* unit = nullptr)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "uint32", unit);
    out.print(",\"value\":");
    out.print((unsigned long)value);
    out.print('}');
}

void printRuntimeF32_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, float value, const char* unit = nullptr)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "float", unit);
    out.print(",\"value\":");
    out.print(value, 3);
    out.print('}');
}

void printRuntimeString_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, const char* value)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "string", nullptr);
    out.print(",\"value\":");
    printJsonEscaped_(out, value ? value : "");
    out.print('}');
}

void printRuntimeUnavailable_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, const char* type)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, type, nullptr);
    out.print(",\"status\":\"unavailable\"}");
}

bool appendMicronovaLocalRuntimeValue_(Print& out, DataStore* dataStore, RuntimeUiId id, bool& firstValue)
{
    if (!dataStore) return false;
    const RuntimeData& rt = dataStore->data();
    const MicronovaRuntimeData& mn = rt.micronova;
    switch (id) {
        case kMicronovaRuntimeIdOnline: printRuntimeBool_(out, firstValue, id, "micronova.online", mn.online); return true;
        case kMicronovaRuntimeIdStateCode: printRuntimeI32_(out, firstValue, id, "micronova.stove_state_code", mn.stoveStateCode); return true;
        case kMicronovaRuntimeIdStateText: printRuntimeString_(out, firstValue, id, "micronova.stove_state_text", mn.stoveStateText); return true;
        case kMicronovaRuntimeIdPowerState: printRuntimeString_(out, firstValue, id, "micronova.power_state", mn.powerState); return true;
        case kMicronovaRuntimeIdPowerLevel: printRuntimeI32_(out, firstValue, id, "micronova.power_level", mn.powerLevel); return true;
        case kMicronovaRuntimeIdFanSpeed: printRuntimeI32_(out, firstValue, id, "micronova.fan_speed", mn.fanSpeed); return true;
        case kMicronovaRuntimeIdTargetTemp: printRuntimeI32_(out, firstValue, id, "micronova.target_temperature", mn.targetTemperature, "\xC2\xB0""C"); return true;
        case kMicronovaRuntimeIdRoomTemp: printRuntimeF32_(out, firstValue, id, "micronova.room_temperature", mn.roomTemperature, "\xC2\xB0""C"); return true;
        case kMicronovaRuntimeIdFumesTemp: printRuntimeF32_(out, firstValue, id, "micronova.fumes_temperature", mn.fumesTemperature, "\xC2\xB0""C"); return true;
        case kMicronovaRuntimeIdWaterTemp: printRuntimeF32_(out, firstValue, id, "micronova.water_temperature", mn.waterTemperature, "\xC2\xB0""C"); return true;
        case kMicronovaRuntimeIdWaterPressure: printRuntimeF32_(out, firstValue, id, "micronova.water_pressure", mn.waterPressure, "bar"); return true;
        case kMicronovaRuntimeIdAlarmCode: printRuntimeI32_(out, firstValue, id, "micronova.alarm_code", mn.alarmCode); return true;
        case kMicronovaRuntimeIdLastUpdateMs: printRuntimeU32_(out, firstValue, id, "micronova.last_update_ms", mn.lastUpdateMs, "ms"); return true;
        case kMicronovaRuntimeIdLastCommand: printRuntimeString_(out, firstValue, id, "micronova.last_command", mn.lastCommand); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 1): printRuntimeBool_(out, firstValue, id, "mqtt.ready", mqttReady(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 3): printRuntimeU32_(out, firstValue, id, "mqtt.rx_drop", mqttRxDrop(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 4): printRuntimeU32_(out, firstValue, id, "mqtt.parse_fail", mqttParseFail(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 5): printRuntimeU32_(out, firstValue, id, "mqtt.handler_fail", mqttHandlerFail(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 6): printRuntimeU32_(out, firstValue, id, "mqtt.oversize_drop", mqttOversizeDrop(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::System, 1): printRuntimeString_(out, firstValue, id, "system.firmware", FirmwareVersion::Full); return true;
        case makeRuntimeUiId(ModuleId::System, 2): {
            SystemStatsSnapshot snap{};
            SystemStats::collect(snap);
            printRuntimeU32_(out, firstValue, id, "system.uptime_ms", snap.uptimeMs, "ms");
            return true;
        }
        case makeRuntimeUiId(ModuleId::System, 3): {
            SystemStatsSnapshot snap{};
            SystemStats::collect(snap);
            printRuntimeU32_(out, firstValue, id, "system.heap_free", snap.heap.freeBytes, "B");
            return true;
        }
        case makeRuntimeUiId(ModuleId::System, 4): {
            SystemStatsSnapshot snap{};
            SystemStats::collect(snap);
            printRuntimeU32_(out, firstValue, id, "system.heap_min_free", snap.heap.minFreeBytes, "B");
            return true;
        }
        case makeRuntimeUiId(ModuleId::Wifi, 1): printRuntimeBool_(out, firstValue, id, "wifi.ready", networkReady(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Wifi, 2): {
            const IpV4 ip = networkIp(*dataStore);
            char ipText[16] = {0};
            snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", (unsigned)ip.b[0], (unsigned)ip.b[1], (unsigned)ip.b[2], (unsigned)ip.b[3]);
            printRuntimeString_(out, firstValue, id, "wifi.ip", ipText);
            return true;
        }
        case makeRuntimeUiId(ModuleId::Wifi, 3): printRuntimeI32_(out, firstValue, id, "wifi.rssi", WiFi.isConnected() ? (int32_t)WiFi.RSSI() : 0, "dBm"); return true;
        default:
            printRuntimeUnavailable_(out, firstValue, id, "", "unknown");
            return true;
    }
}

void sendMicronovaLocalRuntimeValuesResponse_(AsyncWebServerRequest* request,
                                              DataStore* dataStore,
                                              const RuntimeUiId* ids,
                                              size_t idCount)
{
    if (!request || !dataStore) {
        if (request) request->send(503, "application/json", "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.values.local\"}}");
        return;
    }
    if (!ids || idCount == 0U) {
        request->send(400, "application/json", "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
        return;
    }
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    addNoCacheHeaders_(response);
    response->print("{\"ok\":true,\"values\":[");
    bool firstValue = true;
    for (size_t i = 0U; i < idCount; ++i) {
        (void)appendMicronovaLocalRuntimeValue_(*response, dataStore, ids[i], firstValue);
    }
    response->print("]}");
    request->send(response);
}
#endif

#if defined(FLOW_PROFILE_WAVESHARE)
bool waveshareLoadPoolModeFlags_(ConfigStore* cfgStore,
                                bool& hasMode,
                                bool& autoMode,
                                bool& winterMode,
                                bool& phAutoMode,
                                bool& orpAutoMode)
{
    hasMode = false;
    autoMode = false;
    winterMode = false;
    phAutoMode = false;
    orpAutoMode = false;
    if (!cfgStore) return false;

    char moduleJson[320] = {0};
    bool truncated = false;
    if (!cfgStore->toJsonModule("poollogic/modes", moduleJson, sizeof(moduleJson), &truncated, true)) {
        return false;
    }

    StaticJsonDocument<384> doc;
    if (deserializeJson(doc, moduleJson)) return false;
    JsonObjectConst root = doc.as<JsonObjectConst>();
    if (root.isNull()) return false;

    hasMode = true;
    autoMode = root["auto_mode"] | false;
    winterMode = root["winter_mode"] | false;

    memset(moduleJson, 0, sizeof(moduleJson));
    truncated = false;
    if (cfgStore->toJsonModule("poollogic/ph", moduleJson, sizeof(moduleJson), &truncated, true)) {
        StaticJsonDocument<128> phDoc;
        if (!deserializeJson(phDoc, moduleJson)) {
            JsonObjectConst phRoot = phDoc.as<JsonObjectConst>();
            if (!phRoot.isNull()) phAutoMode = phRoot["ph_auto_mode"] | false;
        }
    }

    memset(moduleJson, 0, sizeof(moduleJson));
    truncated = false;
    if (cfgStore->toJsonModule("poollogic/chlorine", moduleJson, sizeof(moduleJson), &truncated, true)) {
        StaticJsonDocument<128> disDoc;
        if (!deserializeJson(disDoc, moduleJson)) {
            JsonObjectConst disRoot = disDoc.as<JsonObjectConst>();
            if (!disRoot.isNull()) orpAutoMode = disRoot["dis_auto_mode"] | false;
        }
    }
    return true;
}

void waveshareLoadMqttServer_(ConfigStore* cfgStore, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;
    out[0] = '\0';
    if (!cfgStore) return;

    char moduleJson[320] = {0};
    bool truncated = false;
    if (!cfgStore->toJsonModule("mqtt", moduleJson, sizeof(moduleJson), &truncated, true)) return;

    StaticJsonDocument<384> doc;
    if (deserializeJson(doc, moduleJson)) return;
    JsonObjectConst root = doc.as<JsonObjectConst>();
    if (root.isNull()) return;

    const char* host = root["host"] | "";
    const int32_t port = root["port"] | 0;
    if (!host || host[0] == '\0') return;
    if (port > 0) {
        snprintf(out, outLen, "%s:%ld", host, (long)port);
    } else {
        snprintf(out, outLen, "%s", host);
    }
}

void waveshareLoadAlarmMasks_(const AlarmService* alarmSvc,
                             uint32_t& activeMask,
                             uint32_t& resettableMask,
                             uint32_t& conditionMask)
{
    activeMask = 0U;
    resettableMask = 0U;
    conditionMask = 0U;
    if (!alarmSvc || !alarmSvc->listIds || !alarmSvc->buildAlarmState) return;

    AlarmId ids[Limits::Alarm::MaxAlarms] = {};
    const uint8_t count = alarmSvc->listIds(alarmSvc->ctx, ids, (uint8_t)Limits::Alarm::MaxAlarms);
    for (uint8_t i = 0; i < count; ++i) {
        char stateJson[144] = {0};
        if (!alarmSvc->buildAlarmState(alarmSvc->ctx, ids[i], stateJson, sizeof(stateJson))) continue;

        StaticJsonDocument<192> doc;
        if (deserializeJson(doc, stateJson)) continue;
        const uint8_t slot = doc["slot"] | 255U;
        if (slot >= 32U) continue;
        const uint32_t bit = (1UL << slot);

        const uint8_t active = doc["a"] | 0U;
        const uint8_t resettable = doc["r"] | 0U;
        const uint8_t cond = doc["c"] | 0U;
        if (active != 0U) activeMask |= bit;
        if (resettable != 0U) resettableMask |= bit;
        if (cond == 1U) conditionMask |= bit;
    }
}

struct WaveshareRuntimeContext {
    bool poolModeLoaded = false;
    bool poolModeAvailable = false;
    bool poolAutoMode = false;
    bool poolWinterMode = false;
    bool poolPhAutoMode = false;
    bool poolOrpAutoMode = false;
    bool mqttServerLoaded = false;
    char mqttServer[96] = {0};
    bool alarmMasksLoaded = false;
    uint32_t alarmActiveMask = 0U;
    uint32_t alarmResettableMask = 0U;
    uint32_t alarmConditionMask = 0U;
    bool systemStatsLoaded = false;
    SystemStatsSnapshot systemStats{};
};

void waveshareEnsurePoolMode_(WaveshareRuntimeContext& ctx, ConfigStore* cfgStore)
{
    if (ctx.poolModeLoaded) return;
    ctx.poolModeLoaded = true;
    ctx.poolModeAvailable = waveshareLoadPoolModeFlags_(cfgStore,
                                                       ctx.poolModeAvailable,
                                                       ctx.poolAutoMode,
                                                       ctx.poolWinterMode,
                                                       ctx.poolPhAutoMode,
                                                       ctx.poolOrpAutoMode);
}

void waveshareEnsureMqttServer_(WaveshareRuntimeContext& ctx, ConfigStore* cfgStore)
{
    if (ctx.mqttServerLoaded) return;
    ctx.mqttServerLoaded = true;
    waveshareLoadMqttServer_(cfgStore, ctx.mqttServer, sizeof(ctx.mqttServer));
}

void waveshareEnsureAlarmMasks_(WaveshareRuntimeContext& ctx, const AlarmService* alarmSvc)
{
    if (ctx.alarmMasksLoaded) return;
    ctx.alarmMasksLoaded = true;
    waveshareLoadAlarmMasks_(alarmSvc, ctx.alarmActiveMask, ctx.alarmResettableMask, ctx.alarmConditionMask);
}

void waveshareEnsureSystemStats_(WaveshareRuntimeContext& ctx)
{
    if (ctx.systemStatsLoaded) return;
    ctx.systemStatsLoaded = true;
    SystemStats::collect(ctx.systemStats);
}

void wavesharePrintUnavailableByManifestType_(Print& out, bool& firstValue, RuntimeUiId id)
{
    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(id);
    printRuntimeUnavailable_(out, firstValue, id, item ? item->key : "", item ? item->type : "unknown");
}

bool appendWaveshareLocalRuntimeValue_(Print& out,
                                      DataStore* dataStore,
                                      ConfigStore* cfgStore,
                                      const AlarmService* alarmSvc,
                                      RuntimeUiId id,
                                      bool& firstValue,
                                      WaveshareRuntimeContext& ctx)
{
    if (!dataStore) {
        wavesharePrintUnavailableByManifestType_(out, firstValue, id);
        return true;
    }

    switch (id) {
        case 901:
            waveshareEnsureAlarmMasks_(ctx, alarmSvc);
            printRuntimeU32_(out, firstValue, id, "alarms.active_mask", ctx.alarmActiveMask);
            return true;
        case 902:
            waveshareEnsureAlarmMasks_(ctx, alarmSvc);
            printRuntimeU32_(out, firstValue, id, "alarms.resettable_mask", ctx.alarmResettableMask);
            return true;
        case 903:
            waveshareEnsureAlarmMasks_(ctx, alarmSvc);
            printRuntimeU32_(out, firstValue, id, "alarms.condition_mask", ctx.alarmConditionMask);
            return true;
        case 2401:
            waveshareEnsurePoolMode_(ctx, cfgStore);
            if (!ctx.poolModeAvailable) {
                wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            } else {
                printRuntimeBool_(out, firstValue, id, "pool.auto_mode", ctx.poolAutoMode);
            }
            return true;
        case 2402:
            waveshareEnsurePoolMode_(ctx, cfgStore);
            if (!ctx.poolModeAvailable) {
                wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            } else {
                printRuntimeBool_(out, firstValue, id, "pool.winter_mode", ctx.poolWinterMode);
            }
            return true;
        case 2403:
            waveshareEnsurePoolMode_(ctx, cfgStore);
            if (!ctx.poolModeAvailable) {
                wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            } else {
                printRuntimeBool_(out, firstValue, id, "pool.ph_auto_mode", ctx.poolPhAutoMode);
            }
            return true;
        case 2404:
            waveshareEnsurePoolMode_(ctx, cfgStore);
            if (!ctx.poolModeAvailable) {
                wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            } else {
                printRuntimeBool_(out, firstValue, id, "pool.dis_auto_mode", ctx.poolOrpAutoMode);
            }
            return true;
        case 2301:
        case 2302:
        case 2303:
        case 2304: {
            uint8_t slot = PoolIds::DeviceFiltrationPump;
            const char* key = "pool.filtration_on";
            if (id == 2302) {
                slot = PoolIds::DevicePhPump;
                key = "pool.ph_pump_on";
            } else if (id == 2303) {
                slot = PoolIds::DeviceChlorinePump;
                key = "pool.chlorine_pump_on";
            } else if (id == 2304) {
                slot = PoolIds::DeviceRobot;
                key = "pool.robot_on";
            }

            PoolDeviceRuntimeStateEntry state{};
            if (!poolDeviceRuntimeState(*dataStore, slot, state)) {
                wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            } else {
                printRuntimeBool_(out, firstValue, id, key, state.actualOn);
            }
            return true;
        }
        case 2101:
            printRuntimeBool_(out, firstValue, id, "mqtt.ready", mqttReady(*dataStore));
            return true;
        case 2102:
            waveshareEnsureMqttServer_(ctx, cfgStore);
            if (ctx.mqttServer[0] == '\0') {
                wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            } else {
                printRuntimeString_(out, firstValue, id, "mqtt.server", ctx.mqttServer);
            }
            return true;
        case 2103:
            printRuntimeU32_(out, firstValue, id, "mqtt.rx_drop", mqttRxDrop(*dataStore));
            return true;
        case 2104:
            printRuntimeU32_(out, firstValue, id, "mqtt.parse_fail", mqttParseFail(*dataStore));
            return true;
        case 2105:
            printRuntimeU32_(out, firstValue, id, "mqtt.handler_fail", mqttHandlerFail(*dataStore));
            return true;
        case 2106:
            printRuntimeU32_(out, firstValue, id, "mqtt.oversize_drop", mqttOversizeDrop(*dataStore));
            return true;
        case 2201:
        case 2202:
        case 2203:
        case 2204:
        case 2206: {
            uint8_t runtimeIndex = 4;
            const char* key = "pool.water_temp";
            const char* unit = "\xC2\xB0""C";
            if (id == 2202) {
                runtimeIndex = 5;
                key = "pool.air_temp";
            } else if (id == 2203) {
                runtimeIndex = 1;
                key = "pool.ph";
                unit = nullptr;
            } else if (id == 2204) {
                runtimeIndex = 0;
                key = "pool.orp";
                unit = "mV";
            } else if (id == 2206) {
                runtimeIndex = 2;
                key = "pool.psi";
                unit = "PSI";
            }

            float value = 0.0f;
            if (!ioEndpointFloat(*dataStore, runtimeIndex, value)) {
                wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            } else {
                printRuntimeF32_(out, firstValue, id, key, value, unit);
            }
            return true;
        }
        case 2205: {
            const uint8_t runtimeIndex = 20;
            float value = 0.0f;
            if (ioEndpointFloat(*dataStore, runtimeIndex, value)) {
                printRuntimeF32_(out, firstValue, id, "pool.water_counter", value, "L");
                return true;
            }
            int32_t counterInt = 0;
            if (ioEndpointInt(*dataStore, runtimeIndex, counterInt)) {
                printRuntimeF32_(out, firstValue, id, "pool.water_counter", (float)counterInt, "L");
                return true;
            }
            wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            return true;
        }
        case 2207:
        case 2208:
        case 2209:
        case 2210:
        case 2211:
        case 2212:
        case 2213:
        case 2214:
            wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            return true;
        case 1801:
            printRuntimeString_(out, firstValue, id, "system.firmware", FirmwareVersion::Full);
            return true;
        case 1802:
            waveshareEnsureSystemStats_(ctx);
            printRuntimeU32_(out, firstValue, id, "system.uptime_ms", (uint32_t)ctx.systemStats.uptimeMs, "ms");
            return true;
        case 1803:
            waveshareEnsureSystemStats_(ctx);
            printRuntimeU32_(out, firstValue, id, "system.heap_free", ctx.systemStats.heap.freeBytes, "B");
            return true;
        case 1804:
            waveshareEnsureSystemStats_(ctx);
            printRuntimeU32_(out, firstValue, id, "system.heap_min_free", ctx.systemStats.heap.minFreeBytes, "B");
            return true;
        case 1301:
            printRuntimeBool_(out, firstValue, id, "time.ready", timeReady(*dataStore) || timeSource(*dataStore) != TimeSource::None);
            return true;
        case 1302:
            printRuntimeString_(out, firstValue, id, "time.source", timeSourceText(*dataStore));
            return true;
        case 1303:
            printRuntimeString_(out, firstValue, id, "time.quality", timeQualityText(*dataStore));
            return true;
        case 1001:
            printRuntimeBool_(out, firstValue, id, "wifi.ready", networkReady(*dataStore));
            return true;
        case 1002: {
            const IpV4 ip = networkIp(*dataStore);
            char ipText[16] = {0};
            snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", (unsigned)ip.b[0], (unsigned)ip.b[1], (unsigned)ip.b[2], (unsigned)ip.b[3]);
            printRuntimeString_(out, firstValue, id, "wifi.ip", ipText);
            return true;
        }
        case 1003:
            if (!WiFi.isConnected()) {
                wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            } else {
                printRuntimeI32_(out, firstValue, id, "wifi.rssi", (int32_t)WiFi.RSSI(), "dBm");
            }
            return true;
        case 1004:
            printRuntimeString_(out, firstValue, id, "network.type", (networkReady(*dataStore) && !WiFi.isConnected()) ? "ethernet" : "wifi");
            return true;
        default:
            wavesharePrintUnavailableByManifestType_(out, firstValue, id);
            return true;
    }
}

void sendWaveshareLocalRuntimeValuesResponse_(AsyncWebServerRequest* request,
                                             DataStore* dataStore,
                                             ConfigStore* cfgStore,
                                             const AlarmService* alarmSvc,
                                             const RuntimeUiId* ids,
                                             size_t idCount)
{
    if (!request || !dataStore) {
        if (request) request->send(503, "application/json", "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.values.local\"}}");
        return;
    }
    if (!ids || idCount == 0U) {
        request->send(400, "application/json", "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
        return;
    }

    WaveshareRuntimeContext ctx{};
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    addNoCacheHeaders_(response);
    response->print("{\"ok\":true,\"values\":[");
    bool firstValue = true;
    for (size_t i = 0U; i < idCount; ++i) {
        (void)appendWaveshareLocalRuntimeValue_(*response, dataStore, cfgStore, alarmSvc, ids[i], firstValue, ctx);
    }
    response->print("]}");
    request->send(response);
}

bool waveshareBuildStatusDomainJson_(FlowStatusDomain domain,
                                    DataStore* dataStore,
                                    ConfigStore* cfgStore,
                                    const AlarmService* alarmSvc,
                                    char* out,
                                    size_t outLen)
{
    if (!out || outLen == 0U) return false;
    out[0] = '\0';

    WaveshareRuntimeContext ctx{};
    StaticJsonDocument<768> doc;
    doc["ok"] = true;

    if (domain == FlowStatusDomain::System) {
        char deviceName[48] = {0};
        loadConfiguredDeviceName_(cfgStore, deviceName, sizeof(deviceName));
        waveshareEnsureSystemStats_(ctx);
        doc["devicename"] = deviceName;
        doc["fw"] = FirmwareVersion::Full;
        doc["upms"] = (uint64_t)ctx.systemStats.uptimeMs64;
        JsonObject time = doc.createNestedObject("time");
        time["rdy"] = dataStore ? (timeReady(*dataStore) || timeSource(*dataStore) != TimeSource::None) : false;
        time["src"] = dataStore ? timeSourceText(*dataStore) : "none";
        time["src_id"] = dataStore ? (uint8_t)timeSource(*dataStore) : 0U;
        time["qlt"] = dataStore ? timeQualityText(*dataStore) : "invalid";
        time["qlt_id"] = dataStore ? (uint8_t)timeQuality(*dataStore) : 0U;
        time["last_ntp"] = dataStore ? (uint32_t)timeLastNtpSyncUtc(*dataStore) : 0U;
        time["last_rtc"] = dataStore ? (uint32_t)timeLastRtcSyncUtc(*dataStore) : 0U;
        JsonObject heap = doc.createNestedObject("heap");
        heap["free"] = ctx.systemStats.heap.freeBytes;
        heap["min_free"] = ctx.systemStats.heap.minFreeBytes;
        heap["larg"] = ctx.systemStats.heap.largestFreeBlock;
        heap["frag"] = ctx.systemStats.heap.fragPercent;
        heap["internal_free"] = ctx.systemStats.heap.internalFreeBytes;
        heap["internal_min"] = ctx.systemStats.heap.internalMinFreeBytes;
        heap["internal_larg"] = ctx.systemStats.heap.internalLargestFreeBlock;
        heap["internal_frag"] = ctx.systemStats.heap.internalFragPercent;
        return serializeJson(doc, out, outLen) > 0U;
    }

    if (domain == FlowStatusDomain::Wifi) {
        JsonObject wifi = doc.createNestedObject("wifi");
        const bool wifiUp = dataStore ? networkReady(*dataStore) : false;
        const bool wifiConnected = WiFi.isConnected();
        const bool ethernetActive = wifiUp && !wifiConnected;
        wifi["rdy"] = wifiUp;
        wifi["typ"] = ethernetActive ? "ethernet" : "wifi";
        IpV4 ip = dataStore ? networkIp(*dataStore) : IpV4{{0, 0, 0, 0}};
        char ipText[20] = {0};
        snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", (unsigned)ip.b[0], (unsigned)ip.b[1], (unsigned)ip.b[2], (unsigned)ip.b[3]);
        wifi["ip"] = ipText;
        uint8_t mac[6] = {0};
        if (ethernetActive) {
            ETH.macAddress(mac);
        } else {
            WiFi.macAddress(mac);
        }
        char macText[18] = {0};
        snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
                 (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2],
                 (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
        wifi["mac"] = macText;
        if (wifiUp && wifiConnected) {
            wifi["rssi"] = (int32_t)WiFi.RSSI();
            wifi["hrss"] = true;
        } else {
            wifi["rssi"] = 0;
            wifi["hrss"] = false;
        }
        return serializeJson(doc, out, outLen) > 0U;
    }

    if (domain == FlowStatusDomain::Mqtt) {
        JsonObject mqtt = doc.createNestedObject("mqtt");
        mqtt["rdy"] = dataStore ? mqttReady(*dataStore) : false;
        waveshareEnsureMqttServer_(ctx, cfgStore);
        mqtt["srv"] = ctx.mqttServer;
        mqtt["rxdrp"] = dataStore ? mqttRxDrop(*dataStore) : 0U;
        mqtt["prsf"] = dataStore ? mqttParseFail(*dataStore) : 0U;
        mqtt["hndf"] = dataStore ? mqttHandlerFail(*dataStore) : 0U;
        mqtt["ovr"] = dataStore ? mqttOversizeDrop(*dataStore) : 0U;
        return serializeJson(doc, out, outLen) > 0U;
    }

    if (domain == FlowStatusDomain::Pool) {
        JsonObject pool = doc.createNestedObject("pool");
        bool hasMode = false;
        bool autoMode = false;
        bool winterMode = false;
        bool phAutoMode = false;
        bool orpAutoMode = false;
        (void)waveshareLoadPoolModeFlags_(cfgStore, hasMode, autoMode, winterMode, phAutoMode, orpAutoMode);
        pool["has"] = hasMode;
        pool["auto"] = autoMode;
        pool["wint"] = winterMode;
        pool["pha"] = phAutoMode;
        pool["ora"] = orpAutoMode;

        auto setFloat = [&](const char* key, uint8_t runtimeIndex, uint8_t decimals) {
            float value = 0.0f;
            if (!dataStore || !ioEndpointFloat(*dataStore, runtimeIndex, value)) {
                pool[key] = nullptr;
                return;
            }
            const float scale = (decimals == 2U) ? 100.0f : (decimals == 1U ? 10.0f : 1.0f);
            const float rounded = (decimals == 0U) ? roundf(value) : (roundf(value * scale) / scale);
            pool[key] = rounded;
        };
        setFloat("wat", 4, 1U);
        setFloat("air", 5, 1U);
        setFloat("ph", 1, 2U);
        setFloat("orp", 0, 0U);

        auto setDevice = [&](const char* key, uint8_t slot) {
            PoolDeviceRuntimeStateEntry state{};
            if (!dataStore || !poolDeviceRuntimeState(*dataStore, slot, state)) {
                pool[key] = nullptr;
                return;
            }
            pool[key] = state.actualOn;
        };
        setDevice("fil", PoolIds::DeviceFiltrationPump);
        setDevice("php", PoolIds::DevicePhPump);
        setDevice("clp", PoolIds::DeviceChlorinePump);
        setDevice("rbt", PoolIds::DeviceRobot);
        return serializeJson(doc, out, outLen) > 0U;
    }

    if (domain == FlowStatusDomain::I2c) {
        JsonObject i2c = doc.createNestedObject("i2c");
        i2c["ena"] = false;
        i2c["sta"] = false;
        i2c["adr"] = 0;
        i2c["req"] = 0;
        i2c["breq"] = 0;
        i2c["bcrc"] = 0;
        i2c["bfmt"] = 0;
        i2c["seen"] = false;
        i2c["ago"] = 0;
        i2c["lnk"] = true;
        i2c["sup_ip"] = "0.0.0.0";
        return serializeJson(doc, out, outLen) > 0U;
    }

    if (domain == FlowStatusDomain::Alarm) {
        waveshareEnsureAlarmMasks_(ctx, alarmSvc);
        JsonObject alm = doc.createNestedObject("alm");
        const uint32_t activeMask = ctx.alarmActiveMask;
        uint8_t count = 0U;
        for (uint8_t bit = 0U; bit < 32U; ++bit) {
            if ((activeMask & (1UL << bit)) != 0U) ++count;
        }
        alm["cnt"] = count;
        JsonArray codes = alm.createNestedArray("codes");
        for (uint8_t bit = 0U; bit < 32U; ++bit) {
            if ((activeMask & (1UL << bit)) == 0U) continue;
            char code[20] = {0};
            snprintf(code, sizeof(code), "alarm_%u", (unsigned)(bit + 1U));
            codes.add(code);
        }
        return serializeJson(doc, out, outLen) > 0U;
    }

    return false;
}

bool sendWaveshareStatusCompactResponse_(AsyncWebServerRequest* request,
                                        DataStore* dataStore,
                                        ConfigStore* cfgStore,
                                        const AlarmService* alarmSvc)
{
    if (!request) return false;
    char systemJson[640] = {0};
    char wifiJson[320] = {0};
    char mqttJson[384] = {0};
    char poolJson[512] = {0};
    char i2cJson[256] = {0};
    if (!waveshareBuildStatusDomainJson_(FlowStatusDomain::System, dataStore, cfgStore, alarmSvc, systemJson, sizeof(systemJson))) return false;
    if (!waveshareBuildStatusDomainJson_(FlowStatusDomain::Wifi, dataStore, cfgStore, alarmSvc, wifiJson, sizeof(wifiJson))) return false;
    if (!waveshareBuildStatusDomainJson_(FlowStatusDomain::Mqtt, dataStore, cfgStore, alarmSvc, mqttJson, sizeof(mqttJson))) return false;
    if (!waveshareBuildStatusDomainJson_(FlowStatusDomain::Pool, dataStore, cfgStore, alarmSvc, poolJson, sizeof(poolJson))) return false;
    if (!waveshareBuildStatusDomainJson_(FlowStatusDomain::I2c, dataStore, cfgStore, alarmSvc, i2cJson, sizeof(i2cJson))) return false;

    StaticJsonDocument<768> systemDoc;
    StaticJsonDocument<512> wifiDoc;
    StaticJsonDocument<512> mqttDoc;
    StaticJsonDocument<640> poolDoc;
    StaticJsonDocument<320> i2cDoc;
    if (deserializeJson(systemDoc, systemJson)) return false;
    if (deserializeJson(wifiDoc, wifiJson)) return false;
    if (deserializeJson(mqttDoc, mqttJson)) return false;
    if (deserializeJson(poolDoc, poolJson)) return false;
    if (deserializeJson(i2cDoc, i2cJson)) return false;

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    addNoCacheHeaders_(response);
    response->print("{\"ok\":true");
    JsonObjectConst systemRoot = systemDoc.as<JsonObjectConst>();
    if (!systemRoot["devicename"].isNull()) appendJsonFieldValue_(*response, "devicename", systemRoot["devicename"]);
    if (!systemRoot["fw"].isNull()) appendJsonFieldValue_(*response, "fw", systemRoot["fw"]);
    if (!systemRoot["upms"].isNull()) appendJsonFieldValue_(*response, "upms", systemRoot["upms"]);
    if (!systemRoot["heap"].isNull()) appendJsonFieldValue_(*response, "heap", systemRoot["heap"]);

    JsonObjectConst wifiRoot = wifiDoc.as<JsonObjectConst>();
    if (!wifiRoot["wifi"].isNull()) appendJsonFieldValue_(*response, "wifi", wifiRoot["wifi"]);
    JsonObjectConst mqttRoot = mqttDoc.as<JsonObjectConst>();
    if (!mqttRoot["mqtt"].isNull()) appendJsonFieldValue_(*response, "mqtt", mqttRoot["mqtt"]);
    JsonObjectConst poolRoot = poolDoc.as<JsonObjectConst>();
    if (!poolRoot["pool"].isNull()) appendJsonFieldValue_(*response, "pool", poolRoot["pool"]);
    JsonObjectConst i2cRoot = i2cDoc.as<JsonObjectConst>();
    if (!i2cRoot["i2c"].isNull()) appendJsonFieldValue_(*response, "i2c", i2cRoot["i2c"]);
    if (dataStore) {
        response->print(",\"time\":{");
        response->print("\"rdy\":");
        response->print((timeReady(*dataStore) || timeSource(*dataStore) != TimeSource::None) ? "true" : "false");
        response->print(",\"src\":");
        printJsonEscaped_(*response, timeSourceText(*dataStore));
        response->print(",\"src_id\":");
        response->print((unsigned)timeSource(*dataStore));
        response->print(",\"qlt\":");
        printJsonEscaped_(*response, timeQualityText(*dataStore));
        response->print(",\"qlt_id\":");
        response->print((unsigned)timeQuality(*dataStore));
        response->print(",\"last_ntp\":");
        response->print((unsigned long)timeLastNtpSyncUtc(*dataStore));
        response->print(",\"last_rtc\":");
        response->print((unsigned long)timeLastRtcSyncUtc(*dataStore));
        response->print('}');
    }

    response->print("}");
    request->send(response);
    return true;
}

struct WaveshareDashboardSlotConfig {
    bool enabled = true;
    RuntimeUiId runtimeUiId = 0U;
    char label[24] = {0};
    uint8_t colorId = 0U;
};

struct WaveshareAlarmDashboardSlotConfig {
    bool enabled = true;
    uint16_t alarmId = 0U;
    char label[24] = {0};
    uint8_t colorId = 0U;
};

struct WaveshareAlarmDashboardSlotState {
    bool available = false;
    bool latched = false;
    bool conditionKnown = false;
    bool conditionTrue = false;
};

struct WaveshareDashboardRuntimeValue {
    bool available = false;
    RuntimeUiWireType wireType = RuntimeUiWireType::Unavailable;
    bool boolValue = false;
    int32_t i32Value = 0;
    uint32_t u32Value = 0U;
    float f32Value = 0.0f;
    char stringValue[64] = {0};
};

constexpr uint8_t kWaveshareDashboardSlotCount = 8U;
constexpr RuntimeUiId kWaveshareDashboardDefaultRuntimeUiIds[kWaveshareDashboardSlotCount] = {
    makeRuntimeUiId(ModuleId::Io, 1),
    makeRuntimeUiId(ModuleId::Io, 2),
    makeRuntimeUiId(ModuleId::Io, 3),
    makeRuntimeUiId(ModuleId::Io, 4),
    makeRuntimeUiId(ModuleId::Io, 5),
    makeRuntimeUiId(ModuleId::Io, 8),
    makeRuntimeUiId(ModuleId::Io, 7),
    makeRuntimeUiId(ModuleId::Io, 6),
};
constexpr const char* kWaveshareDashboardDefaultLabels[kWaveshareDashboardSlotCount] = {
    "Eau",
    "Air",
    "pH",
    "ORP",
    "Compteur",
    "BME680",
    "BMP280",
    "PSI",
};
constexpr uint8_t kWaveshareDashboardDefaultColorIds[kWaveshareDashboardSlotCount] = {
    0U,
    1U,
    2U,
    3U,
    4U,
    5U,
    6U,
    7U,
};
constexpr uint16_t kWaveshareAlarmDashboardDefaultIds[kWaveshareDashboardSlotCount] = {
    (uint16_t)AlarmId::PoolPsiLow,
    (uint16_t)AlarmId::PoolPsiHigh,
    (uint16_t)AlarmId::PoolPhTankLow,
    (uint16_t)AlarmId::PoolChlorineTankLow,
    (uint16_t)AlarmId::PoolPhPumpMaxUptime,
    (uint16_t)AlarmId::PoolChlorinePumpMaxUptime,
    (uint16_t)AlarmId::PoolWaterLevelLow,
    (uint16_t)AlarmId::PoolWaterLevelLow,
};
constexpr bool kWaveshareAlarmDashboardDefaultEnabled[kWaveshareDashboardSlotCount] = {
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    false,
};
constexpr const char* kWaveshareAlarmDashboardDefaultLabels[kWaveshareDashboardSlotCount] = {
    "PSI bas",
    "PSI haut",
    "pH vide",
    "Chlore vide",
    "pH uptime",
    "ORP uptime",
    "Eau basse",
    "",
};
constexpr uint8_t kWaveshareAlarmDashboardDefaultColorIds[kWaveshareDashboardSlotCount] = {
    17U,
    17U,
    8U,
    10U,
    9U,
    7U,
    5U,
    19U,
};
constexpr const char* kWaveshareDashboardColorHex[] = {
    "#E6EFFF", "#E5F8FC", "#E8FAEF", "#F0EAFE", "#E4F6FA", "#E3F7FE", "#EAF8FD",
    "#FEF0E8", "#FCE7EF", "#FFF0E1", "#FFF7D9", "#EDF8E7", "#F0FAE6", "#F5EEFF",
    "#EBEEFF", "#EEF7FF", "#F4FADE", "#FFE9E4", "#F9EEE8", "#F1F4F8", "#FFFFFF",
};

const char* waveshareDashboardColorHex_(uint8_t colorId, uint8_t slot)
{
    const uint8_t colorCount = (uint8_t)(sizeof(kWaveshareDashboardColorHex) / sizeof(kWaveshareDashboardColorHex[0]));
    if (colorId < colorCount) return kWaveshareDashboardColorHex[colorId];
    if (slot < kWaveshareDashboardSlotCount) {
        const uint8_t fallbackId = kWaveshareDashboardDefaultColorIds[slot];
        if (fallbackId < colorCount) return kWaveshareDashboardColorHex[fallbackId];
    }
    return "#FFFFFF";
}

const char* waveshareAlarmDashboardLabel_(uint16_t alarmId)
{
    switch ((AlarmId)alarmId) {
        case AlarmId::PoolPsiLow: return "PSI bas";
        case AlarmId::PoolPsiHigh: return "PSI haut";
        case AlarmId::PoolPhTankLow: return "pH vide";
        case AlarmId::PoolChlorineTankLow: return "Chlore vide";
        case AlarmId::PoolPhPumpMaxUptime: return "pH uptime";
        case AlarmId::PoolChlorinePumpMaxUptime: return "ORP uptime";
        case AlarmId::PoolWaterLevelLow: return "Eau basse";
        case AlarmId::None:
        default: return "Alarme";
    }
}

void waveshareDashboardFallbackLabel_(RuntimeUiId id, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;
    out[0] = '\0';
    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(id);
    const char* key = (item && item->key) ? item->key : "";
    const char* src = strrchr(key, '.');
    src = src ? (src + 1) : key;
    if (!src || src[0] == '\0') {
        snprintf(out, outLen, "Mesure");
        return;
    }

    bool upperNext = true;
    size_t j = 0U;
    for (size_t i = 0U; src[i] != '\0' && (j + 1U) < outLen; ++i) {
        char ch = src[i];
        if (ch == '_' || ch == '-') {
            out[j++] = ' ';
            upperNext = true;
            continue;
        }
        if (upperNext && ch >= 'a' && ch <= 'z') ch = (char)(ch - ('a' - 'A'));
        out[j++] = ch;
        upperNext = false;
    }
    out[j] = '\0';
}

void waveshareLoadDashboardSlotConfig_(ConfigStore* cfgStore, uint8_t slot, WaveshareDashboardSlotConfig& out)
{
    out.enabled = true;
    out.runtimeUiId = (slot < kWaveshareDashboardSlotCount) ? kWaveshareDashboardDefaultRuntimeUiIds[slot] : 0U;
    out.colorId = (slot < kWaveshareDashboardSlotCount) ? kWaveshareDashboardDefaultColorIds[slot] : 0U;
    snprintf(out.label,
             sizeof(out.label),
             "%s",
             (slot < kWaveshareDashboardSlotCount) ? kWaveshareDashboardDefaultLabels[slot] : "Mesure");
    if (!cfgStore || slot >= kWaveshareDashboardSlotCount) return;

    char moduleName[32] = {0};
    snprintf(moduleName, sizeof(moduleName), "tft/s3/sensors/slot%02u", (unsigned)slot);
    char moduleJson[384] = {0};
    bool truncated = false;
    if (!cfgStore->toJsonModule(moduleName, moduleJson, sizeof(moduleJson), &truncated, true) || truncated) return;

    StaticJsonDocument<448> doc;
    if (deserializeJson(doc, moduleJson)) return;
    if (doc.containsKey("enabled")) out.enabled = doc["enabled"].as<bool>();
    if (doc.containsKey("runtime_ui_id")) out.runtimeUiId = (RuntimeUiId)(doc["runtime_ui_id"].as<uint32_t>() & 0xFFFFU);
    if (doc.containsKey("color_id")) out.colorId = (uint8_t)(doc["color_id"].as<uint32_t>() & 0xFFU);
    if (doc.containsKey("label")) {
        const char* label = doc["label"].as<const char*>();
        snprintf(out.label, sizeof(out.label), "%s", label ? label : "");
    }
}

void waveshareLoadAlarmDashboardSlotConfig_(ConfigStore* cfgStore, uint8_t slot, WaveshareAlarmDashboardSlotConfig& out)
{
    out.enabled = (slot < kWaveshareDashboardSlotCount) ? kWaveshareAlarmDashboardDefaultEnabled[slot] : false;
    out.alarmId = (slot < kWaveshareDashboardSlotCount) ? kWaveshareAlarmDashboardDefaultIds[slot] : 0U;
    out.colorId = (slot < kWaveshareDashboardSlotCount) ? kWaveshareAlarmDashboardDefaultColorIds[slot] : 0U;
    snprintf(out.label,
             sizeof(out.label),
             "%s",
             (slot < kWaveshareDashboardSlotCount) ? kWaveshareAlarmDashboardDefaultLabels[slot] : "");
    if (!cfgStore || slot >= kWaveshareDashboardSlotCount) return;

    char moduleName[32] = {0};
    snprintf(moduleName, sizeof(moduleName), "tft/s3/alarms/slot%02u", (unsigned)slot);
    char moduleJson[384] = {0};
    bool truncated = false;
    if (!cfgStore->toJsonModule(moduleName, moduleJson, sizeof(moduleJson), &truncated, true) || truncated) return;

    StaticJsonDocument<448> doc;
    if (deserializeJson(doc, moduleJson)) return;
    if (doc.containsKey("enabled")) out.enabled = doc["enabled"].as<bool>();
    if (doc.containsKey("alarm_id")) out.alarmId = (uint16_t)(doc["alarm_id"].as<uint32_t>() & 0xFFFFU);
    if (doc.containsKey("color_id")) out.colorId = (uint8_t)(doc["color_id"].as<uint32_t>() & 0xFFU);
    if (doc.containsKey("label")) {
        const char* label = doc["label"].as<const char*>();
        snprintf(out.label, sizeof(out.label), "%s", label ? label : "");
    }
}

bool waveshareSetDashboardRuntimeFromIoValue_(const IoValue& value, WaveshareDashboardRuntimeValue& out)
{
    if (!value.valid) return false;
    out.available = true;
    if (value.type == IO_VAL_BOOL) {
        out.wireType = RuntimeUiWireType::Bool;
        out.boolValue = value.v.b != 0U;
        return true;
    }
    if (value.type == IO_VAL_INT32) {
        out.wireType = RuntimeUiWireType::Int32;
        out.i32Value = value.v.i32;
        return true;
    }
    out.wireType = RuntimeUiWireType::Float32;
    out.f32Value = value.v.f;
    return true;
}

bool waveshareReadDashboardIoValue_(const IOServiceV2* ioSvc, IoId ioId, WaveshareDashboardRuntimeValue& out)
{
    if (!ioSvc || !ioSvc->readValue) return false;
    IoValue value{};
    if (ioSvc->readValue(ioSvc->ctx, ioId, &value) != IO_OK) return false;
    return waveshareSetDashboardRuntimeFromIoValue_(value, out);
}

bool waveshareReadDashboardIoBackendValue_(const IOServiceV2* ioSvc,
                                          uint8_t backend,
                                          uint8_t channel,
                                          WaveshareDashboardRuntimeValue& out)
{
    if (!ioSvc || !ioSvc->count || !ioSvc->idAt || !ioSvc->meta) return false;
    const uint8_t count = ioSvc->count(ioSvc->ctx);
    for (uint8_t i = 0U; i < count; ++i) {
        IoId ioId = IO_ID_INVALID;
        if (ioSvc->idAt(ioSvc->ctx, i, &ioId) != IO_OK) continue;
        IoEndpointMeta meta{};
        if (ioSvc->meta(ioSvc->ctx, ioId, &meta) != IO_OK) continue;
        if (meta.backend == backend && meta.channel == channel) {
            return waveshareReadDashboardIoValue_(ioSvc, ioId, out);
        }
    }
    return false;
}

bool waveshareReadDashboardPoolSensorDataStore_(DataStore* dataStore,
                                               uint8_t valueId,
                                               WaveshareDashboardRuntimeValue& out)
{
    if (!dataStore) return false;

    uint8_t runtimeIndex = 4;
    if (valueId == 2U) {
        runtimeIndex = 5;
    } else if (valueId == 3U) {
        runtimeIndex = 1;
    } else if (valueId == 4U) {
        runtimeIndex = 0;
    } else if (valueId == 5U) {
        runtimeIndex = 20;
    } else if (valueId == 6U) {
        runtimeIndex = 2;
    } else if (valueId != 1U) {
        return false;
    }

    float value = 0.0f;
    if (ioEndpointFloat(*dataStore, runtimeIndex, value)) {
        out.available = true;
        out.wireType = RuntimeUiWireType::Float32;
        out.f32Value = value;
        return true;
    }
    if (valueId == 5U) {
        int32_t counterInt = 0;
        if (ioEndpointInt(*dataStore, runtimeIndex, counterInt)) {
            out.available = true;
            out.wireType = RuntimeUiWireType::Float32;
            out.f32Value = (float)counterInt;
            return true;
        }
    }
    return false;
}

bool waveshareReadDashboardRuntimeValue_(DataStore* dataStore,
                                        ConfigStore* cfgStore,
                                        const AlarmService* alarmSvc,
                                        const IOServiceV2* ioSvc,
                                        RuntimeUiId id,
                                        WaveshareDashboardRuntimeValue& out,
                                        WaveshareRuntimeContext& ctx)
{
    if (!findRuntimeUiManifestItem(id)) return false;

    const ModuleId module = (ModuleId)runtimeUiModuleId(id);
    const uint8_t valueId = runtimeUiValueId(id);
    switch (module) {
        case ModuleId::Alarm:
            waveshareEnsureAlarmMasks_(ctx, alarmSvc);
            out.available = true;
            out.wireType = RuntimeUiWireType::UInt32;
            if (valueId == 1U) out.u32Value = ctx.alarmActiveMask;
            else if (valueId == 2U) out.u32Value = ctx.alarmResettableMask;
            else if (valueId == 3U) out.u32Value = ctx.alarmConditionMask;
            else return false;
            return true;

        case ModuleId::PoolLogic:
            waveshareEnsurePoolMode_(ctx, cfgStore);
            if (!ctx.poolModeAvailable) return false;
            out.available = true;
            out.wireType = RuntimeUiWireType::Bool;
            if (valueId == 1U) out.boolValue = ctx.poolAutoMode;
            else if (valueId == 2U) out.boolValue = ctx.poolWinterMode;
            else if (valueId == 3U) out.boolValue = ctx.poolPhAutoMode;
            else if (valueId == 4U) out.boolValue = ctx.poolOrpAutoMode;
            else return false;
            return true;

        case ModuleId::PoolDevice: {
            if (!dataStore) return false;
            uint8_t deviceSlot = 0xFFU;
            if (valueId == 1U) deviceSlot = PoolIds::DeviceFiltrationPump;
            else if (valueId == 2U) deviceSlot = PoolIds::DevicePhPump;
            else if (valueId == 3U) deviceSlot = PoolIds::DeviceChlorinePump;
            else if (valueId == 4U) deviceSlot = PoolIds::DeviceRobot;
            else return false;

            PoolDeviceRuntimeStateEntry state{};
            if (!poolDeviceRuntimeState(*dataStore, deviceSlot, state)) return false;
            out.available = true;
            out.wireType = RuntimeUiWireType::Bool;
            out.boolValue = state.actualOn;
            return true;
        }

        case ModuleId::Mqtt:
            if (!dataStore) return false;
            out.available = true;
            if (valueId == 1U) {
                out.wireType = RuntimeUiWireType::Bool;
                out.boolValue = mqttReady(*dataStore);
                return true;
            }
            if (valueId == 2U) {
                waveshareEnsureMqttServer_(ctx, cfgStore);
                if (ctx.mqttServer[0] == '\0') return false;
                out.wireType = RuntimeUiWireType::String;
                snprintf(out.stringValue, sizeof(out.stringValue), "%s", ctx.mqttServer);
                return true;
            }
            out.wireType = RuntimeUiWireType::UInt32;
            if (valueId == 3U) out.u32Value = mqttRxDrop(*dataStore);
            else if (valueId == 4U) out.u32Value = mqttParseFail(*dataStore);
            else if (valueId == 5U) out.u32Value = mqttHandlerFail(*dataStore);
            else if (valueId == 6U) out.u32Value = mqttOversizeDrop(*dataStore);
            else return false;
            return true;

        case ModuleId::Io:
            if (valueId >= 1U && valueId <= 6U) {
                IoId ioId = ioIdFromSlot(analogInputSlot(4));
                if (valueId == 2U) ioId = ioIdFromSlot(analogInputSlot(5));
                else if (valueId == 3U) ioId = ioIdFromSlot(analogInputSlot(1));
                else if (valueId == 4U) ioId = ioIdFromSlot(analogInputSlot(0));
                else if (valueId == 5U) ioId = ioIdFromSlot(digitalInputSlot(12));
                else if (valueId == 6U) ioId = ioIdFromSlot(analogInputSlot(2));
                return waveshareReadDashboardIoValue_(ioSvc, ioId, out) ||
                       waveshareReadDashboardPoolSensorDataStore_(dataStore, valueId, out);
            }
            if (valueId == 7U) return waveshareReadDashboardIoBackendValue_(ioSvc, IO_BACKEND_BMP280, 0U, out);
            if (valueId == 8U) return waveshareReadDashboardIoBackendValue_(ioSvc, IO_BACKEND_BME680, 0U, out);
            if (valueId == 9U) return waveshareReadDashboardIoBackendValue_(ioSvc, IO_BACKEND_BMP280, 1U, out);
            if (valueId == 10U) return waveshareReadDashboardIoBackendValue_(ioSvc, IO_BACKEND_SHT40, 0U, out);
            if (valueId == 11U) return waveshareReadDashboardIoBackendValue_(ioSvc, IO_BACKEND_SHT40, 1U, out);
            if (valueId == 12U) return waveshareReadDashboardIoBackendValue_(ioSvc, IO_BACKEND_BME680, 1U, out);
            if (valueId == 13U) return waveshareReadDashboardIoBackendValue_(ioSvc, IO_BACKEND_BME680, 2U, out);
            if (valueId == 14U) return waveshareReadDashboardIoBackendValue_(ioSvc, IO_BACKEND_BME680, 3U, out);
            return false;

        case ModuleId::System:
            out.available = true;
            if (valueId == 1U) {
                out.wireType = RuntimeUiWireType::String;
                snprintf(out.stringValue, sizeof(out.stringValue), "%s", FirmwareVersion::Full);
                return true;
            }
            waveshareEnsureSystemStats_(ctx);
            out.wireType = RuntimeUiWireType::UInt32;
            if (valueId == 2U) out.u32Value = (uint32_t)ctx.systemStats.uptimeMs;
            else if (valueId == 3U) out.u32Value = ctx.systemStats.heap.freeBytes;
            else if (valueId == 4U) out.u32Value = ctx.systemStats.heap.minFreeBytes;
            else return false;
            return true;

        case ModuleId::Wifi:
            if (!dataStore) return false;
            out.available = true;
            if (valueId == 1U) {
                out.wireType = RuntimeUiWireType::Bool;
                out.boolValue = networkReady(*dataStore);
                return true;
            }
            if (valueId == 2U) {
                const IpV4 ip = networkIp(*dataStore);
                out.wireType = RuntimeUiWireType::String;
                snprintf(out.stringValue,
                         sizeof(out.stringValue),
                         "%u.%u.%u.%u",
                         (unsigned)ip.b[0],
                         (unsigned)ip.b[1],
                         (unsigned)ip.b[2],
                         (unsigned)ip.b[3]);
                return true;
            }
            if (valueId == 3U) {
                if (!WiFi.isConnected()) return false;
                out.wireType = RuntimeUiWireType::Int32;
                out.i32Value = (int32_t)WiFi.RSSI();
                return true;
            }
            return false;

        default:
            return false;
    }
}

void waveshareTrimDashboardSlotFloat_(char* text)
{
    if (!text) return;
    char* dot = strchr(text, '.');
    if (!dot) return;
    char* end = text + strlen(text);
    while (end > dot && end[-1] == '0') --end;
    if (end > dot && end[-1] == '.') --end;
    *end = '\0';
    if (strcmp(text, "-0") == 0) snprintf(text, 4, "0");
}

uint8_t waveshareDashboardSlotDecimals_(RuntimeUiId id, RuntimeUiWireType type)
{
    if (type != RuntimeUiWireType::Float32) return 0U;
    if (id == makeRuntimeUiId(ModuleId::Io, 3)) return 2U;
    if (id == makeRuntimeUiId(ModuleId::Io, 6)) return 2U;
    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(id);
    const char* unit = (item && item->unit) ? item->unit : "";
    if (unit && strcmp(unit, "mV") == 0) return 0U;
    return 1U;
}

void waveshareFormatDashboardRuntimeValue_(RuntimeUiId id,
                                          const WaveshareDashboardRuntimeValue& value,
                                          char* valueOut,
                                          size_t valueOutLen,
                                          char* unitOut,
                                          size_t unitOutLen)
{
    if (valueOut && valueOutLen > 0U) valueOut[0] = '\0';
    if (unitOut && unitOutLen > 0U) unitOut[0] = '\0';
    if (!valueOut || valueOutLen == 0U || !unitOut || unitOutLen == 0U || !value.available) {
        if (valueOut && valueOutLen > 0U) snprintf(valueOut, valueOutLen, "Indisponible");
        return;
    }

    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(id);
    const char* unit = (item && item->unit) ? item->unit : "";
    if (unit && unit[0] != '\0') {
        snprintf(unitOut, unitOutLen, "%s", dashboardSlotDegreeCUnit_(unit) ? "\xC2\xB0""C" : unit);
    }

    switch (value.wireType) {
        case RuntimeUiWireType::Bool:
            unitOut[0] = '\0';
            snprintf(valueOut, valueOutLen, "%s", value.boolValue ? "Actif" : "Arret");
            return;
        case RuntimeUiWireType::Int32:
            snprintf(valueOut, valueOutLen, "%ld", (long)value.i32Value);
            return;
        case RuntimeUiWireType::UInt32:
        case RuntimeUiWireType::Enum:
            snprintf(valueOut, valueOutLen, "%lu", (unsigned long)value.u32Value);
            return;
        case RuntimeUiWireType::Float32: {
            const uint8_t decimals = waveshareDashboardSlotDecimals_(id, value.wireType);
            if (decimals > 0U) {
                snprintf(valueOut, valueOutLen, "%.*f", (int)decimals, (double)value.f32Value);
                waveshareTrimDashboardSlotFloat_(valueOut);
            } else {
                snprintf(valueOut, valueOutLen, "%ld", lroundf(value.f32Value));
            }
            return;
        }
        case RuntimeUiWireType::String:
            unitOut[0] = '\0';
            snprintf(valueOut, valueOutLen, "%s", value.stringValue);
            return;
        case RuntimeUiWireType::NotFound:
        case RuntimeUiWireType::Unavailable:
        default:
            unitOut[0] = '\0';
            snprintf(valueOut, valueOutLen, "Indisponible");
            return;
    }
}

const char* waveshareIoBackendLabel_(uint8_t backend)
{
    switch (backend) {
        case IO_BACKEND_GPIO: return "GPIO";
        case IO_BACKEND_PCF8574: return "PCF8574";
        case IO_BACKEND_ADS1115_INT: return "ADS1115 int";
        case IO_BACKEND_ADS1115_EXT_DIFF: return "ADS1115 ext";
        case IO_BACKEND_DS18B20: return "DS18B20";
        case IO_BACKEND_SHT40: return "SHT40";
        case IO_BACKEND_BMP280: return "BMP280";
        case IO_BACKEND_BME680: return "BME680";
        case IO_BACKEND_INA226: return "INA226";
        case IO_BACKEND_TCA9554: return "TCA9554";
        case IO_BACKEND_MCP23017: return "MCP23017";
        default: return "unknown";
    }
}

const char* waveshareIoSlotKindLabel_(uint8_t kind)
{
    switch (kind) {
        case IO_SLOT_ANALOG_INPUT: return "analog_in";
        case IO_SLOT_DIGITAL_INPUT: return "digital_in";
        case IO_SLOT_DIGITAL_OUTPUT: return "digital_out";
        default: return "unknown";
    }
}

const char* waveshareIoPortKindLabel_(uint8_t kind)
{
    switch (kind) {
        case IO_PORT_KIND_GPIO_INPUT: return "gpio_input";
        case IO_PORT_KIND_GPIO_OUTPUT: return "gpio_output";
        case IO_PORT_KIND_PCF8574_OUTPUT: return "pcf8574_output";
        case IO_PORT_KIND_ADS_INTERNAL_SINGLE: return "ads1115_internal";
        case IO_PORT_KIND_ADS_EXTERNAL_DIFF: return "ads1115_external_diff";
        case IO_PORT_KIND_DS18_WATER: return "ds18b20_water";
        case IO_PORT_KIND_DS18_AIR: return "ds18b20_air";
        case IO_PORT_KIND_INA226: return "ina226";
        case IO_PORT_KIND_SHT40: return "sht40";
        case IO_PORT_KIND_BMP280: return "bmp280";
        case IO_PORT_KIND_BME680: return "bme680";
        case IO_PORT_KIND_TCA9554_OUTPUT: return "tca9554_output";
        case IO_PORT_KIND_MCP23017_OUTPUT: return "mcp23017_output";
        case IO_PORT_KIND_MCP23017_INPUT: return "mcp23017_input";
        default: return "none";
    }
}

const char* waveshareIoPortDirectionLabel_(uint8_t kind)
{
    switch (kind) {
        case IO_PORT_KIND_GPIO_INPUT:
        case IO_PORT_KIND_ADS_INTERNAL_SINGLE:
        case IO_PORT_KIND_ADS_EXTERNAL_DIFF:
        case IO_PORT_KIND_DS18_WATER:
        case IO_PORT_KIND_DS18_AIR:
        case IO_PORT_KIND_INA226:
        case IO_PORT_KIND_SHT40:
        case IO_PORT_KIND_BMP280:
        case IO_PORT_KIND_BME680:
        case IO_PORT_KIND_MCP23017_INPUT:
            return "input";
        case IO_PORT_KIND_GPIO_OUTPUT:
        case IO_PORT_KIND_PCF8574_OUTPUT:
        case IO_PORT_KIND_TCA9554_OUTPUT:
        case IO_PORT_KIND_MCP23017_OUTPUT:
            return "output";
        default:
            return "unknown";
    }
}

const char* wavesharePoolDeviceBlockReasonLabel_(uint8_t reason)
{
    switch (reason) {
        case POOL_DEVICE_BLOCK_NONE: return "";
        case POOL_DEVICE_BLOCK_DISABLED: return "disabled";
        case POOL_DEVICE_BLOCK_INTERLOCK: return "interlock";
        case POOL_DEVICE_BLOCK_IO_ERROR: return "io_error";
        case POOL_DEVICE_BLOCK_MAX_UPTIME: return "max_uptime";
        case POOL_DEVICE_BLOCK_UNBOUND: return "unbound";
        case POOL_DEVICE_BLOCK_IO_DISABLED: return "io_disabled";
        default: return "blocked";
    }
}

bool waveshareIoPortBackendChannel_(const IOBindingPortSpec& spec, uint8_t& backendOut, uint8_t& channelOut)
{
    switch (spec.kind) {
        case IO_PORT_KIND_GPIO_INPUT:
        case IO_PORT_KIND_GPIO_OUTPUT:
            backendOut = IO_BACKEND_GPIO;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_PCF8574_OUTPUT:
            backendOut = IO_BACKEND_PCF8574;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_ADS_INTERNAL_SINGLE:
            backendOut = IO_BACKEND_ADS1115_INT;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_ADS_EXTERNAL_DIFF:
            backendOut = IO_BACKEND_ADS1115_EXT_DIFF;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_DS18_WATER:
        case IO_PORT_KIND_DS18_AIR:
            backendOut = IO_BACKEND_DS18B20;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_INA226:
            backendOut = IO_BACKEND_INA226;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_SHT40:
            backendOut = IO_BACKEND_SHT40;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_BMP280:
            backendOut = IO_BACKEND_BMP280;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_BME680:
            backendOut = IO_BACKEND_BME680;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_TCA9554_OUTPUT:
            backendOut = IO_BACKEND_TCA9554;
            channelOut = spec.channel;
            return true;
        case IO_PORT_KIND_MCP23017_OUTPUT:
        case IO_PORT_KIND_MCP23017_INPUT:
            backendOut = IO_BACKEND_MCP23017;
            channelOut = spec.channel;
            return true;
        default:
            return false;
    }
}

bool waveshareIoPortMatchesMeta_(const IOBindingPortSpec& spec, const IoEndpointMeta& meta)
{
    return meta.bindingPort != IO_PORT_INVALID && spec.portId == meta.bindingPort;
}

const IOBindingPortSpec* waveshareFindPortForMeta_(const IoEndpointMeta& meta)
{
    using namespace Profiles::Waveshare::IoLayout;
    for (const IOBindingPortSpec& spec : kBindingPorts) {
        if (waveshareIoPortMatchesMeta_(spec, meta)) return &spec;
    }
    return nullptr;
}

const DomainIoSlotBinding* waveshareFindDomainBinding_(DomainSlotId domainSlot)
{
    for (const DomainIoSlotBinding& binding : PoolDomain::kDomainIoSlots) {
        if (binding.domainSlot == domainSlot) return &binding;
    }
    return nullptr;
}

const DomainSlotPreset* waveshareFindDomainSlotPreset_(DomainSlotId domainSlot)
{
    for (const DomainSlotPreset& preset : PoolDomain::kDomainSlots) {
        if (preset.id == domainSlot) return &preset;
    }
    return nullptr;
}

void waveshareFormatIoValue_(const IoEndpointMeta& meta, const IoValue& value, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;
    out[0] = '\0';
    if (!value.valid) {
        snprintf(out, outLen, "-");
        return;
    }
    if (value.type == IO_VAL_BOOL) {
        snprintf(out, outLen, "%s", value.v.b ? "on" : "off");
        return;
    }
    if (value.type == IO_VAL_INT32) {
        snprintf(out, outLen, "%ld", (long)value.v.i32);
        return;
    }
    int precision = (int)meta.precision;
    if (precision < 0) precision = 0;
    if (precision > 4) precision = 4;
    snprintf(out, outLen, "%.*f", precision, (double)value.v.f);
    waveshareTrimDashboardSlotFloat_(out);
}

struct WaveshareIoSummaryState {
    DomainSlotId domainSlot = DOMAIN_SLOT_INVALID;
    const char* state = "sleeping";
    const char* error = "";
    bool active = false;
    bool manuallyDisabled = false;
    bool errorState = false;
    IoEndpointMeta meta{};
    IoValue value{};
    bool hasMeta = false;
    bool hasValue = false;
    bool hasBindingPort = false;
    bool hasPoolDevice = false;
    PoolDeviceSvcMeta poolMeta{};
    uint8_t poolActualOn = 0U;
    uint32_t poolActualTsMs = 0U;
};

WaveshareIoSummaryState waveshareIoSummaryStateForSlot_(const DomainStatusService* domainStatusSvc,
                                                        DomainSlotId domainSlot)
{
    WaveshareIoSummaryState out{};
    out.domainSlot = domainSlot;
    DomainSlotStatus shared{};
    if (!domainStatusSvc || !domainStatusSvc->slotStatus ||
        !domainStatusSvc->slotStatus(domainStatusSvc->ctx, domainSlot, &shared)) {
        out.error = "not_configured";
        return out;
    }

    out.state = domainSlotRuntimeStateName(shared.state);
    out.active = shared.active != 0U;
    out.manuallyDisabled = shared.state == DomainSlotRuntimeState::ManuallyDisabled;
    out.errorState = shared.error != 0U;
    out.meta = shared.meta;
    out.value = shared.value;
    out.hasMeta = shared.hasMeta != 0U;
    out.hasValue = shared.hasValue != 0U;
    out.hasBindingPort = shared.hasBindingPort != 0U;
    out.hasPoolDevice = shared.hasPoolDevice != 0U;
    out.poolMeta = shared.poolMeta;
    out.poolActualOn = shared.poolActualOn;
    out.poolActualTsMs = shared.poolActualTsMs;
    out.error = (shared.reason == DomainSlotStatusReason::PoolDeviceBlocked)
        ? wavesharePoolDeviceBlockReasonLabel_(shared.poolMeta.blockReason)
        : domainSlotStatusReasonName(shared.reason);
    return out;
}

const WaveshareIoSummaryState* waveshareFindIoSummaryState_(const WaveshareIoSummaryState* states,
                                                           size_t stateCount,
                                                           DomainSlotId domainSlot)
{
    if (!states) return nullptr;
    for (size_t i = 0U; i < stateCount; ++i) {
        if (states[i].domainSlot == domainSlot) return &states[i];
    }
    return nullptr;
}

struct WaveshareBindingPortState {
    IoId ioId = IO_ID_INVALID;
    uint32_t tsMs = 0U;
    char valueText[32] = {0};
    const char* state = "sleeping";
    const char* reason = "";
    bool bound = false;
    bool valueOk = false;
};

void waveshareCollectBindingPortStates_(const IOServiceV2* ioSvc,
                                        WaveshareBindingPortState* states,
                                        size_t stateCount)
{
    using namespace Profiles::Waveshare::IoLayout;
    if (!states || stateCount == 0U || !ioSvc || !ioSvc->count || !ioSvc->idAt || !ioSvc->meta) return;

    if (ioSvc->bindingPortStatus) {
        for (size_t portIndex = 0U; portIndex < stateCount; ++portIndex) {
            IoRuntimeStatus runtime{};
            if (ioSvc->bindingPortStatus(ioSvc->ctx, kBindingPorts[portIndex].portId, &runtime) != IO_OK) continue;
            states[portIndex].state = ioRuntimeStateName(runtime.state);
            states[portIndex].reason = ioRuntimeReasonName(runtime.reason);
        }
    }

    const uint8_t endpointCount = ioSvc->count(ioSvc->ctx);
    for (uint8_t i = 0U; i < endpointCount; ++i) {
        IoId ioId = IO_ID_INVALID;
        if (ioSvc->idAt(ioSvc->ctx, i, &ioId) != IO_OK) continue;

        IoEndpointMeta meta{};
        if (ioSvc->meta(ioSvc->ctx, ioId, &meta) != IO_OK) continue;
        const IOBindingPortSpec* port = waveshareFindPortForMeta_(meta);
        if (!port) continue;

        const size_t portIndex = (size_t)(port - kBindingPorts);
        if (portIndex >= stateCount || states[portIndex].bound) continue;

        WaveshareBindingPortState& state = states[portIndex];
        state.bound = true;
        state.ioId = ioId;

        IoRuntimeStatus runtime{};
        if (ioSvc->runtimeStatus && ioSvc->runtimeStatus(ioSvc->ctx, ioId, &runtime) == IO_OK) {
            state.state = ioRuntimeStateName(runtime.state);
            state.reason = ioRuntimeReasonName(runtime.reason);
            if (runtime.state != IO_RUNTIME_ACTIVE) continue;
        }

        IoValue value{};
        if (!ioSvc->readValue || ioSvc->readValue(ioSvc->ctx, ioId, &value) != IO_OK || !value.valid) {
            state.state = "error";
            state.reason = "read_failed";
            continue;
        }
        state.valueOk = true;
        state.state = "active";
        state.reason = "";
        state.tsMs = value.tsMs;
        waveshareFormatIoValue_(meta, value, state.valueText, sizeof(state.valueText));
    }
}

void wavesharePrintPoolDeviceJson_(Print& response, const WaveshareIoSummaryState& state)
{
    if (!state.hasPoolDevice) {
        response.print("null");
        return;
    }
    response.print("{\"slot\":");
    response.print((unsigned)state.poolMeta.slot);
    response.print(",\"label\":");
    printJsonEscaped_(response, state.poolMeta.label);
    response.print(",\"enabled\":");
    response.print(state.poolMeta.enabled ? "true" : "false");
    response.print(",\"actual_on\":");
    response.print(state.poolActualOn ? "true" : "false");
    response.print(",\"block_reason\":");
    printJsonEscaped_(response, wavesharePoolDeviceBlockReasonLabel_(state.poolMeta.blockReason));
    response.print("}");
}

struct WaveshareIoResponseSnapshot {
    static constexpr size_t kBindingPortCount =
        sizeof(Profiles::Waveshare::IoLayout::kBindingPorts) /
        sizeof(Profiles::Waveshare::IoLayout::kBindingPorts[0]);
    static constexpr size_t kDomainSlotCount =
        sizeof(PoolDomain::kDomainSlots) / sizeof(PoolDomain::kDomainSlots[0]);

    WaveshareBindingPortState bindingStates[kBindingPortCount]{};
    WaveshareIoSummaryState domainStates[kDomainSlotCount]{};
    uint16_t bindingActive = 0U;
    uint16_t bindingManuallyDisabled = 0U;
    uint16_t bindingError = 0U;
    uint16_t ioActive = 0U;
    uint16_t ioManuallyDisabled = 0U;
    uint16_t ioError = 0U;
    DomainStatusSummary domainSummary{};
    uint8_t driverActive[11] = {0};
    uint8_t driverManuallyDisabled[11] = {0};
    uint8_t driverError[11] = {0};
};

void waveshareCollectIoResponseSnapshot_(const IOServiceV2* ioSvc,
                                         const DomainStatusService* domainStatusSvc,
                                         WaveshareIoResponseSnapshot& snapshot)
{
    waveshareCollectBindingPortStates_(ioSvc,
                                       snapshot.bindingStates,
                                       WaveshareIoResponseSnapshot::kBindingPortCount);
    for (const WaveshareBindingPortState& state : snapshot.bindingStates) {
        if (strcmp(state.state, "active") == 0) ++snapshot.bindingActive;
        else if (strcmp(state.state, "manually_disabled") == 0) ++snapshot.bindingManuallyDisabled;
        else if (strcmp(state.state, "error") == 0) ++snapshot.bindingError;
    }

    snapshot.domainSummary.total = (uint16_t)WaveshareIoResponseSnapshot::kDomainSlotCount;
    for (size_t i = 0U; i < WaveshareIoResponseSnapshot::kDomainSlotCount; ++i) {
        const DomainSlotPreset& preset = PoolDomain::kDomainSlots[i];
        WaveshareIoSummaryState& state = snapshot.domainStates[i];
        state = waveshareIoSummaryStateForSlot_(domainStatusSvc, preset.id);
        if (state.active) ++snapshot.domainSummary.active;
        else if (state.manuallyDisabled) ++snapshot.domainSummary.manuallyDisabled;
        else if (state.errorState) ++snapshot.domainSummary.error;
        else ++snapshot.domainSummary.sleeping;
    }

    for (const DomainIoSlotBinding& binding : PoolDomain::kDomainIoSlots) {
        const WaveshareIoSummaryState* state =
            waveshareFindIoSummaryState_(snapshot.domainStates,
                                         WaveshareIoResponseSnapshot::kDomainSlotCount,
                                         binding.domainSlot);
        if (!state) continue;
        if (state->active) {
            ++snapshot.ioActive;
            if (state->hasMeta && state->meta.backend < 11U) {
                ++snapshot.driverActive[state->meta.backend];
            }
        }
        if (state->manuallyDisabled) {
            ++snapshot.ioManuallyDisabled;
            if (state->hasMeta && state->meta.backend < 11U) {
                ++snapshot.driverManuallyDisabled[state->meta.backend];
            }
        }
        if (state->errorState) {
            ++snapshot.ioError;
            if (state->hasMeta && state->meta.backend < 11U) {
                ++snapshot.driverError[state->meta.backend];
            }
        }
    }
}

void wavesharePrintIoSlotTopologyJson_(Print& response,
                                       const WaveshareIoSummaryState& state,
                                       const DomainIoSlotBinding& binding,
                                       const DomainSlotPreset* domainPreset)
{
    response.print("{\"io_slot\":");
    printJsonEscaped_(response, waveshareIoSlotKindLabel_(ioSlotKind(binding.ioSlot)));
    response.print(",\"io_slot_index\":");
    response.print((unsigned)ioSlotIndex(binding.ioSlot));
    response.print(",\"io_id\":");
    response.print((unsigned)ioIdFromSlot(binding.ioSlot));
    response.print(",\"io_id_label\":");
    printJsonEscaped_(response, "");
    response.print(",\"domain_slot_id\":");
    response.print((unsigned)binding.domainSlot);
    response.print(",\"kind\":");
    printJsonEscaped_(response, waveshareIoSlotKindLabel_(ioSlotKind(binding.ioSlot)));
    const IOBindingPortSpec* port = state.hasMeta ? waveshareFindPortForMeta_(state.meta) : nullptr;
    uint8_t portBackend = IO_BACKEND_GPIO;
    uint8_t portChannel = 0U;
    const bool hasPort = port && waveshareIoPortBackendChannel_(*port, portBackend, portChannel);
    response.print(",\"driver\":");
    printJsonEscaped_(response, hasPort ? waveshareIoBackendLabel_(portBackend) : "");
    response.print(",\"channel\":");
    if (hasPort) response.print((unsigned)portChannel);
    else response.print("null");
    response.print(",\"binding_port\":");
    response.print(port ? (unsigned)port->portId : 0U);
    response.print(",\"label\":");
    printJsonEscaped_(response, domainPreset && domainPreset->displayName ? domainPreset->displayName : "");
    response.print(",\"config_name\":");
    printJsonEscaped_(response, state.hasMeta ? state.meta.name : "");
    response.print("}");
}

void wavesharePrintIoSlotRuntimeJson_(Print& response,
                                      const WaveshareIoSummaryState& state,
                                      const DomainIoSlotBinding& binding)
{
    char valueText[32] = {0};
    if (state.hasValue) waveshareFormatIoValue_(state.meta, state.value, valueText, sizeof(valueText));
    response.print("{\"domain_slot_id\":");
    response.print((unsigned)binding.domainSlot);
    response.print(",\"io_slot\":");
    printJsonEscaped_(response, waveshareIoSlotKindLabel_(ioSlotKind(binding.ioSlot)));
    response.print(",\"io_slot_index\":");
    response.print((unsigned)ioSlotIndex(binding.ioSlot));
    response.print(",\"io_id\":");
    response.print((unsigned)ioIdFromSlot(binding.ioSlot));
    response.print(",\"state\":");
    printJsonEscaped_(response, state.state);
    response.print(",\"last_value\":");
    printJsonEscaped_(response, valueText[0] != '\0' ? valueText : "-");
    response.print(",\"ts_ms\":");
    response.print(state.hasValue ? (unsigned long)state.value.tsMs : 0UL);
    response.print(",\"error\":");
    printJsonEscaped_(response, state.error);
    response.print(",\"pool_device\":");
    wavesharePrintPoolDeviceJson_(response, state);
    response.print("}");
}

void buildWaveshareIoTopologyResponse_(Print& response,
                                       const WaveshareIoResponseSnapshot& snapshot,
                                       uint32_t revision)
{
    using namespace Profiles::Waveshare::IoLayout;
    response.print("{\"ok\":true,\"schema\":3,\"revision\":");
    response.print((unsigned long)revision);
    response.print(",\"binding_ports\":[");
    bool first = true;
    for (size_t portIndex = 0U; portIndex < WaveshareIoResponseSnapshot::kBindingPortCount; ++portIndex) {
        const IOBindingPortSpec& spec = kBindingPorts[portIndex];
        uint8_t backend = IO_BACKEND_GPIO;
        uint8_t channel = 0U;
        (void)waveshareIoPortBackendChannel_(spec, backend, channel);
        if (!first) response.print(',');
        response.print("{\"port_id\":");
        response.print((unsigned)spec.portId);
        response.print(",\"board_port\":");
        printJsonEscaped_(response, spec.boardLabel ? spec.boardLabel : "");
        response.print(",\"kind\":");
        printJsonEscaped_(response, waveshareIoPortKindLabel_(spec.kind));
        response.print(",\"direction\":");
        printJsonEscaped_(response, waveshareIoPortDirectionLabel_(spec.kind));
        response.print(",\"driver\":");
        printJsonEscaped_(response, waveshareIoBackendLabel_(backend));
        response.print(",\"channel\":");
        response.print((unsigned)channel);
        response.print("}");
        first = false;
    }

    response.print("],\"io_slots\":[");
    first = true;
    for (const DomainIoSlotBinding& binding : PoolDomain::kDomainIoSlots) {
        const WaveshareIoSummaryState* state =
            waveshareFindIoSummaryState_(snapshot.domainStates,
                                         WaveshareIoResponseSnapshot::kDomainSlotCount,
                                         binding.domainSlot);
        if (!state) continue;
        if (!first) response.print(',');
        wavesharePrintIoSlotTopologyJson_(response,
                                          *state,
                                          binding,
                                          waveshareFindDomainSlotPreset_(binding.domainSlot));
        first = false;
    }

    response.print("],\"domain_slots\":[");
    first = true;
    for (size_t i = 0U; i < WaveshareIoResponseSnapshot::kDomainSlotCount; ++i) {
        const DomainSlotPreset& preset = PoolDomain::kDomainSlots[i];
        const DomainIoSlotBinding* binding = waveshareFindDomainBinding_(preset.id);
        const IoSlotId ioSlot = binding ? binding->ioSlot : IO_SLOT_INVALID;
        const WaveshareIoSummaryState& state = snapshot.domainStates[i];
        if (!first) response.print(',');
        response.print("{\"domain_slot_id\":");
        response.print((unsigned)preset.id);
        response.print(",\"endpoint_id\":");
        printJsonEscaped_(response, preset.endpointId ? preset.endpointId : "");
        response.print(",\"io_name\":");
        printJsonEscaped_(response, state.hasMeta ? state.meta.name : "");
        response.print(",\"display_name\":");
        printJsonEscaped_(response, preset.displayName ? preset.displayName : "");
        response.print(",\"slot_kind\":");
        printJsonEscaped_(response, waveshareIoSlotKindLabel_(preset.slotKind));
        response.print(",\"io_slot\":");
        printJsonEscaped_(response, ioSlot == IO_SLOT_INVALID ? "" : waveshareIoSlotKindLabel_(ioSlotKind(ioSlot)));
        response.print(",\"io_slot_index\":");
        response.print(ioSlot == IO_SLOT_INVALID ? 0U : (unsigned)ioSlotIndex(ioSlot));
        response.print("}");
        first = false;
    }
    response.print("]}");
}

void buildWaveshareIoRuntimeResponse_(Print& response,
                                      const WaveshareIoResponseSnapshot& snapshot,
                                      uint32_t topologyRevision)
{
    using namespace Profiles::Waveshare::IoLayout;
    response.print("{\"ok\":true,\"schema\":3,\"topology_revision\":");
    response.print((unsigned long)topologyRevision);
    response.print(",\"summary\":{");
    response.print("\"binding_ports_total\":");
    response.print((unsigned)WaveshareIoResponseSnapshot::kBindingPortCount);
    response.print(",\"binding_ports_active\":");
    response.print((unsigned)snapshot.bindingActive);
    response.print(",\"binding_ports_manually_disabled\":");
    response.print((unsigned)snapshot.bindingManuallyDisabled);
    response.print(",\"binding_ports_error\":");
    response.print((unsigned)snapshot.bindingError);
    response.print(",\"io_slots_total\":");
    response.print((unsigned)(sizeof(PoolDomain::kDomainIoSlots) / sizeof(PoolDomain::kDomainIoSlots[0])));
    response.print(",\"io_slots_active\":");
    response.print((unsigned)snapshot.ioActive);
    response.print(",\"io_slots_manually_disabled\":");
    response.print((unsigned)snapshot.ioManuallyDisabled);
    response.print(",\"io_slots_error\":");
    response.print((unsigned)snapshot.ioError);
    response.print(",\"domain_slots_total\":");
    response.print((unsigned)(sizeof(PoolDomain::kDomainSlots) / sizeof(PoolDomain::kDomainSlots[0])));
    response.print(",\"domain_slots_active\":");
    response.print((unsigned)snapshot.domainSummary.active);
    response.print(",\"domain_slots_manually_disabled\":");
    response.print((unsigned)snapshot.domainSummary.manuallyDisabled);
    response.print(",\"domain_slots_error\":");
    response.print((unsigned)snapshot.domainSummary.error);
    response.print(",\"error_slots\":");
    response.print((unsigned)snapshot.domainSummary.error);
    response.print("},\"drivers\":[");
    bool first = true;
    for (uint8_t backend = 0U; backend < 11U; ++backend) {
        if (snapshot.driverActive[backend] == 0U &&
            snapshot.driverManuallyDisabled[backend] == 0U &&
            snapshot.driverError[backend] == 0U) continue;
        if (!first) response.print(',');
        response.print("{\"driver\":");
        printJsonEscaped_(response, waveshareIoBackendLabel_(backend));
        response.print(",\"active_slots\":");
        response.print((unsigned)snapshot.driverActive[backend]);
        response.print(",\"manually_disabled_slots\":");
        response.print((unsigned)snapshot.driverManuallyDisabled[backend]);
        response.print(",\"error_slots\":");
        response.print((unsigned)snapshot.driverError[backend]);
        response.print("}");
        first = false;
    }

    response.print("],\"binding_ports\":[");
    first = true;
    for (size_t portIndex = 0U; portIndex < WaveshareIoResponseSnapshot::kBindingPortCount; ++portIndex) {
        const IOBindingPortSpec& spec = kBindingPorts[portIndex];
        const WaveshareBindingPortState& state = snapshot.bindingStates[portIndex];
        if (!first) response.print(',');
        response.print("{\"port_id\":");
        response.print((unsigned)spec.portId);
        response.print(",\"io_id\":");
        response.print(state.bound ? (unsigned)state.ioId : (unsigned)IO_ID_INVALID);
        response.print(",\"io_id_label\":");
        printJsonEscaped_(response, state.bound ? "" : "Non affecte");
        response.print(",\"state\":");
        printJsonEscaped_(response, state.state);
        response.print(",\"reason\":");
        printJsonEscaped_(response, state.reason);
        response.print(",\"last_value\":");
        printJsonEscaped_(response, state.valueOk ? state.valueText : "-");
        response.print(",\"ts_ms\":");
        response.print(state.valueOk ? (unsigned long)state.tsMs : 0UL);
        response.print("}");
        first = false;
    }

    response.print("],\"io_slots\":[");
    first = true;
    for (const DomainIoSlotBinding& binding : PoolDomain::kDomainIoSlots) {
        const WaveshareIoSummaryState* state =
            waveshareFindIoSummaryState_(snapshot.domainStates,
                                         WaveshareIoResponseSnapshot::kDomainSlotCount,
                                         binding.domainSlot);
        if (!state) continue;
        if (!first) response.print(',');
        wavesharePrintIoSlotRuntimeJson_(response, *state, binding);
        first = false;
    }

    response.print("],\"domain_slots\":[");
    first = true;
    for (size_t i = 0U; i < WaveshareIoResponseSnapshot::kDomainSlotCount; ++i) {
        const DomainSlotPreset& preset = PoolDomain::kDomainSlots[i];
        if (!first) response.print(',');
        const WaveshareIoSummaryState& state = snapshot.domainStates[i];
        char valueText[32] = {0};
        if (state.hasValue) waveshareFormatIoValue_(state.meta, state.value, valueText, sizeof(valueText));
        response.print("{\"domain_slot_id\":");
        response.print((unsigned)preset.id);
        response.print(",\"state\":");
        printJsonEscaped_(response, state.state);
        response.print(",\"last_value\":");
        printJsonEscaped_(response, valueText[0] != '\0' ? valueText : "-");
        response.print(",\"pool_device\":");
        wavesharePrintPoolDeviceJson_(response, state);
        response.print("}");
        first = false;
    }

    response.print("],\"error_slots\":[");
    first = true;
    for (size_t i = 0U; i < WaveshareIoResponseSnapshot::kDomainSlotCount; ++i) {
        const DomainSlotPreset& preset = PoolDomain::kDomainSlots[i];
        const DomainIoSlotBinding* binding = waveshareFindDomainBinding_(preset.id);
        const IoSlotId ioSlot = binding ? binding->ioSlot : IO_SLOT_INVALID;
        const WaveshareIoSummaryState& state = snapshot.domainStates[i];
        if (!state.errorState) continue;
        if (!first) response.print(',');
        response.print("{\"domain_slot_id\":");
        response.print((unsigned)preset.id);
        response.print(",\"label\":");
        printJsonEscaped_(response, preset.displayName ? preset.displayName : "");
        response.print(",\"io_slot\":");
        printJsonEscaped_(response, ioSlot == IO_SLOT_INVALID ? "" : waveshareIoSlotKindLabel_(ioSlotKind(ioSlot)));
        response.print(",\"error\":");
        printJsonEscaped_(response, state.error);
        response.print("}");
        first = false;
    }
    response.print("]}");
}

bool waveshareReadAlarmDashboardSlotState_(const AlarmService* alarmSvc,
                                          uint16_t alarmId,
                                          WaveshareAlarmDashboardSlotState& out)
{
    out = WaveshareAlarmDashboardSlotState{};
    if (!alarmSvc || !alarmSvc->buildAlarmState || alarmId == 0U) return false;

    char stateJson[144] = {0};
    if (!alarmSvc->buildAlarmState(alarmSvc->ctx, (AlarmId)alarmId, stateJson, sizeof(stateJson))) return false;

    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, stateJson)) return false;
    out.available = true;
    out.latched = (doc["a"] | 0U) != 0U;
    const uint8_t condition = doc["c"] | 2U;
    out.conditionKnown = condition != (uint8_t)AlarmCondState::Unknown;
    out.conditionTrue = condition == (uint8_t)AlarmCondState::True;
    return true;
}

void sendWaveshareDashboardSlotsResponse_(AsyncResponseStream& response,
                                         bool& firstSlot,
                                         DataStore* dataStore,
                                         ConfigStore* cfgStore,
                                         const AlarmService* alarmSvc,
                                         const IOServiceV2* ioSvc)
{
    WaveshareRuntimeContext ctx{};
    for (uint8_t i = 0U; i < kWaveshareDashboardSlotCount; ++i) {
        WaveshareDashboardSlotConfig slot{};
        waveshareLoadDashboardSlotConfig_(cfgStore, i, slot);

        char label[32] = {0};
        snprintf(label, sizeof(label), "%s", slot.label);
        if (label[0] == '\0') waveshareDashboardFallbackLabel_(slot.runtimeUiId, label, sizeof(label));

        WaveshareDashboardRuntimeValue runtimeValue{};
        const bool available = slot.enabled &&
                               waveshareReadDashboardRuntimeValue_(dataStore,
                                                                  cfgStore,
                                                                  alarmSvc,
                                                                  ioSvc,
                                                                  slot.runtimeUiId,
                                                                  runtimeValue,
                                                                  ctx);
        runtimeValue.available = available;

        char valueText[40] = {0};
        char unitText[12] = {0};
        waveshareFormatDashboardRuntimeValue_(slot.runtimeUiId,
                                             runtimeValue,
                                             valueText,
                                             sizeof(valueText),
                                             unitText,
                                             sizeof(unitText));

        if (!firstSlot) response.print(',');
        response.print("{\"slot\":");
        response.print((unsigned)i);
        response.print(",\"enabled\":");
        response.print(slot.enabled ? "true" : "false");
        response.print(",\"runtime_ui_id\":");
        response.print((unsigned long)slot.runtimeUiId);
        response.print(",\"label\":");
        printJsonEscaped_(response, label[0] != '\0' ? label : "Mesure");
        response.print(",\"value\":");
        printJsonEscaped_(response, valueText);
        response.print(",\"unit\":");
        printJsonEscaped_(response, unitText);
        response.print(",\"bg_color\":");
        printJsonEscaped_(response, waveshareDashboardColorHex_(slot.colorId, i));
        response.print(",\"available\":");
        response.print(available ? "true" : "false");
        response.print("}");
        firstSlot = false;
    }
}

void sendWaveshareAlarmDashboardSlotsResponse_(AsyncResponseStream& response,
                                              bool& firstSlot,
                                              ConfigStore* cfgStore,
                                              const AlarmService* alarmSvc)
{
    for (uint8_t i = 0U; i < kWaveshareDashboardSlotCount; ++i) {
        WaveshareAlarmDashboardSlotConfig slot{};
        waveshareLoadAlarmDashboardSlotConfig_(cfgStore, i, slot);

        char label[32] = {0};
        if (slot.enabled) {
            snprintf(label, sizeof(label), "%s", slot.label);
            if (label[0] == '\0') {
                snprintf(label, sizeof(label), "%s", waveshareAlarmDashboardLabel_(slot.alarmId));
            }
        }

        WaveshareAlarmDashboardSlotState state{};
        const bool available = slot.enabled &&
                               waveshareReadAlarmDashboardSlotState_(alarmSvc, slot.alarmId, state);
        state.available = available;

        if (!firstSlot) response.print(',');
        response.print("{\"slot\":");
        response.print((unsigned)i);
        response.print(",\"enabled\":");
        response.print(slot.enabled ? "true" : "false");
        response.print(",\"alarm_id\":");
        response.print((unsigned)slot.alarmId);
        response.print(",\"label\":");
        printJsonEscaped_(response, label);
        response.print(",\"bg_color\":");
        printJsonEscaped_(response, waveshareDashboardColorHex_(slot.colorId, i));
        response.print(",\"available\":");
        response.print(available ? "true" : "false");
        response.print(",\"latched\":");
        response.print(state.latched ? "true" : "false");
        response.print(",\"condition_known\":");
        response.print(state.conditionKnown ? "true" : "false");
        response.print(",\"condition_true\":");
        response.print(state.conditionTrue ? "true" : "false");
        response.print("}");
        firstSlot = false;
    }
}

#endif

bool parseRuntimeUiIdsCsv_(const char* raw, RuntimeUiId* idsOut, size_t capacity, size_t& countOut)
{
    countOut = 0U;
    if (!raw || !idsOut || capacity == 0U) return false;

    uint32_t current = 0U;
    bool hasDigit = false;

    auto flushCurrent = [&]() -> bool {
        if (!hasDigit) return true;
        if (countOut >= capacity || current == 0U || current > 65535U) return false;
        idsOut[countOut++] = (RuntimeUiId)current;
        current = 0U;
        hasDigit = false;
        return true;
    };

    for (const char* p = raw; *p != '\0'; ++p) {
        const char ch = *p;
        if (ch >= '0' && ch <= '9') {
            hasDigit = true;
            current = (current * 10U) + (uint32_t)(ch - '0');
            if (current > 65535U) return false;
            continue;
        }
        if (ch == ',' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (ch == ',') {
                if (!flushCurrent()) return false;
            }
            continue;
        }
        return false;
    }

    return flushCurrent() && countOut > 0U;
}

void sendRuntimeUiValuesResponse_(AsyncWebServerRequest* request,
                                  const FlowCfgRemoteService* flowCfgSvc,
                                  const RuntimeUiId* ids,
                                  size_t idCount)
{
    if (!request || !flowCfgSvc || !flowCfgSvc->runtimeUiValues) {
        if (request) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.values\"}}");
        }
        return;
    }
    if (flowCfgSvc->isReady && !flowCfgSvc->isReady(flowCfgSvc->ctx)) {
        request->send(503, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.values.link\"}}");
        return;
    }
    if (!ids || idCount == 0U) {
        request->send(400, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
        return;
    }

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    addNoCacheHeaders_(response);
    response->print("{\"ok\":true,\"values\":[");
    bool firstValue = true;

    size_t start = 0U;
    while (start < idCount) {
        size_t batchCount = 0U;
        size_t batchBudget = 1U;  // record count byte
        while ((start + batchCount) < idCount) {
            const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(ids[start + batchCount]);
            const bool isString = item && item->type && strcmp(item->type, "string") == 0;
            const size_t estimate = runtimeUiWireEstimate_(item);

            if (batchCount > 0U && (isString || (batchBudget + estimate) > I2cCfgProtocol::MaxPayload)) {
                break;
            }
            batchBudget += estimate;
            ++batchCount;
            if (isString) break;
        }
        if (batchCount == 0U) batchCount = 1U;

        uint8_t payload[I2cCfgProtocol::MaxPayload] = {0};
        size_t written = 0U;
        if (!flowCfgSvc->runtimeUiValues(flowCfgSvc->ctx,
                                         ids + start,
                                         (uint8_t)batchCount,
                                         payload,
                                         sizeof(payload),
                                         &written)) {
            delete response;
            request->send(502, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"runtime.values.fetch\"}}");
            return;
        }
        if (!appendRuntimeUiJsonValuesToStream_(*response, payload, written, firstValue)) {
            delete response;
            request->send(502, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"runtime.values.decode\"}}");
            return;
        }
        start += batchCount;
    }

    response->print("]}");
    request->send(response);
}

bool dashboardSlotDegreeCUnit_(const char* unit)
{
    if (!unit || unit[0] == '\0') return false;
    if ((uint8_t)unit[0] == 0xB0 && unit[1] == 'C' && unit[2] == '\0') return true;
    return (uint8_t)unit[0] == 0xC2 && (uint8_t)unit[1] == 0xB0 && unit[2] == 'C' && unit[3] == '\0';
}

#if !defined(FLOW_PROFILE_MICRONOVA) && !defined(FLOW_PROFILE_WAVESHARE)
void dashboardSlotBgColorHex_(uint16_t color565, char* out, size_t outLen)
{
    if (!out || outLen < 8U) return;
    const uint8_t r5 = (uint8_t)((color565 >> 11) & 0x1FU);
    const uint8_t g6 = (uint8_t)((color565 >> 5) & 0x3FU);
    const uint8_t b5 = (uint8_t)(color565 & 0x1FU);
    const uint8_t r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
    const uint8_t g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
    const uint8_t b8 = (uint8_t)((b5 << 3) | (b5 >> 2));
    snprintf(out, outLen, "#%02X%02X%02X", (unsigned)r8, (unsigned)g8, (unsigned)b8);
}

uint8_t dashboardSlotDecimals_(const FlowRemoteDashboardSlotRuntime& slot)
{
    if ((RuntimeUiWireType)slot.wireType != RuntimeUiWireType::Float32) return 0U;
    if (slot.runtimeUiId == makeRuntimeUiId(ModuleId::Io, 3)) return 2U;
    if (slot.runtimeUiId == makeRuntimeUiId(ModuleId::Io, 6)) return 2U;
    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(slot.runtimeUiId);
    const char* unit = (item && item->unit) ? item->unit : "";
    if (unit && strcmp(unit, "mV") == 0) return 0U;
    return 1U;
}

void trimDashboardSlotFloat_(char* text)
{
    if (!text) return;
    char* dot = strchr(text, '.');
    if (!dot) return;
    char* end = text + strlen(text);
    while (end > dot && end[-1] == '0') --end;
    if (end > dot && end[-1] == '.') --end;
    *end = '\0';
    if (strcmp(text, "-0") == 0) snprintf(text, 4, "0");
}

void formatDashboardSlotValueText_(const FlowRemoteDashboardSlotRuntime& slot, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;
    if (!slot.enabled || !slot.available) {
        snprintf(out, outLen, "Indisponible");
        return;
    }

    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(slot.runtimeUiId);
    const char* unit = (item && item->unit) ? item->unit : "";

    switch ((RuntimeUiWireType)slot.wireType) {
        case RuntimeUiWireType::Bool:
            snprintf(out, outLen, "%s", slot.boolValue ? "Actif" : "Arret");
            return;
        case RuntimeUiWireType::Enum:
            snprintf(out, outLen, "%u", (unsigned)slot.enumValue);
            return;
        case RuntimeUiWireType::Int32:
            if (unit && unit[0] != '\0') {
                snprintf(out, outLen, "%ld %s", (long)slot.i32Value, unit);
            } else {
                snprintf(out, outLen, "%ld", (long)slot.i32Value);
            }
            return;
        case RuntimeUiWireType::UInt32:
            if (unit && unit[0] != '\0') {
                snprintf(out, outLen, "%lu %s", (unsigned long)slot.u32Value, unit);
            } else {
                snprintf(out, outLen, "%lu", (unsigned long)slot.u32Value);
            }
            return;
        case RuntimeUiWireType::Float32: {
            char numberBuf[24] = {0};
            const uint8_t decimals = dashboardSlotDecimals_(slot);
            if (decimals > 0U) {
                snprintf(numberBuf, sizeof(numberBuf), "%.*f", (int)decimals, (double)slot.f32Value);
                trimDashboardSlotFloat_(numberBuf);
            } else {
                snprintf(numberBuf, sizeof(numberBuf), "%ld", lroundf(slot.f32Value));
            }
            if (unit && unit[0] != '\0') {
                if (dashboardSlotDegreeCUnit_(unit)) {
                    snprintf(out, outLen, "%s %s", numberBuf, "\xC2\xB0""C");
                } else {
                    snprintf(out, outLen, "%s %s", numberBuf, unit);
                }
            } else {
                snprintf(out, outLen, "%s", numberBuf);
            }
            return;
        }
        case RuntimeUiWireType::NotFound:
        case RuntimeUiWireType::Unavailable:
        case RuntimeUiWireType::String:
        default:
            snprintf(out, outLen, "Indisponible");
            return;
    }
}
#endif

struct HttpLatencyScope {
    AsyncWebServerRequest* req;
    const char* route;
    uint32_t startUs;
    uint32_t infoMs;
    uint32_t warnMs;
#if FLOW_WEB_HEAP_FORENSICS
    HeapForensicSnapshot startHeap;
#endif

    HttpLatencyScope(AsyncWebServerRequest* request,
                     const char* routePath,
                     uint32_t infoThresholdMs = kHttpLatencyInfoMs,
                     uint32_t warnThresholdMs = kHttpLatencyWarnMs)
        : req(request),
          route(routePath),
          startUs(micros()),
          infoMs(infoThresholdMs),
          warnMs((warnThresholdMs > infoThresholdMs) ? warnThresholdMs : (infoThresholdMs + 1U))
    {
        if (gHttpActivityHook) {
            gHttpActivityHook(gHttpActivityHookCtx);
        }
#if FLOW_WEB_HEAP_FORENSICS
        startHeap = captureHeapForensicSnapshot_();
#endif
    }

    ~HttpLatencyScope()
    {
#if FLOW_WEB_HEAP_FORENSICS
        logHttpHeapForensic_(req, route, startUs, startHeap);
#endif
        const uint32_t elapsedUs = micros() - startUs;
        const uint32_t elapsedMs = elapsedUs / 1000U;
        if (elapsedMs < infoMs) return;

        const char* method = req ? httpMethodName_(req->method()) : "?";
#if defined(FLOW_PROFILE_WAVESHARE)
        const uint32_t heapFree = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const uint32_t heapLargest =
            (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
        const uint32_t heapFree = (uint32_t)ESP.getFreeHeap();
        const uint32_t heapLargest = heapFree;
#endif
        if (elapsedMs >= warnMs) {
            LOGW("HTTP slow %s %s latency=%lums heap=%lu largest=%lu",
                 method,
                 route ? route : "?",
                 (unsigned long)elapsedMs,
                 (unsigned long)heapFree,
                 (unsigned long)heapLargest);
        } else {
            LOGI("HTTP %s %s latency=%lums heap=%lu largest=%lu",
                 method,
                 route ? route : "?",
                 (unsigned long)elapsedMs,
                 (unsigned long)heapFree,
                 (unsigned long)heapLargest);
        }
    }
};

const UartSpec* webBridgeUartSpec_(const BoardSpec& board)
{
    return boardFindUart(board, "bridge");
}
} // namespace

static const char kWebInterfaceFallbackPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>flow.io Rescue</title>
<style>
  :root { color-scheme: dark; --bg:#07111f; --panel:#101c2f; --panel2:#13243b; --line:#29415f; --text:#edf4ff; --muted:#a9bad2; --accent:#41c7b7; --warn:#ffd166; --bad:#ff7b8a; }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--text); font-family: Arial, Helvetica, sans-serif; }
  header { padding: 18px 16px 14px; border-bottom: 1px solid var(--line); background: #0a1728; }
  main { width: min(980px, 100%); margin: 0 auto; padding: 14px; }
  h1 { margin: 0; font-size: 24px; letter-spacing: 0; }
  h2 { margin: 0 0 12px; font-size: 17px; }
  p { margin: 8px 0 0; color: var(--muted); line-height: 1.45; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(270px, 1fr)); gap: 12px; }
  section { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 14px; }
  label { display: block; margin: 10px 0 4px; color: var(--muted); font-size: 13px; }
  input, select { width: 100%; min-height: 38px; border: 1px solid var(--line); border-radius: 5px; background: #071422; color: var(--text); padding: 8px 10px; font-size: 15px; }
  input[type="checkbox"] { width: auto; min-height: 0; margin-right: 8px; }
  button { min-height: 38px; border: 1px solid #55d5c8; border-radius: 5px; background: #0c5e58; color: white; padding: 8px 12px; font-size: 14px; font-weight: 700; cursor: pointer; }
  button.secondary { border-color: var(--line); background: var(--panel2); color: var(--text); }
  button.warn { border-color: #d5a530; background: #705316; }
  button:disabled { opacity: .55; cursor: wait; }
  .row { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 12px; }
  .row > * { flex: 1 1 150px; }
  .status { white-space: pre-wrap; word-break: break-word; min-height: 42px; margin-top: 10px; padding: 10px; border-radius: 5px; background: #071422; border: 1px solid #1d314a; color: var(--muted); font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 12px; line-height: 1.4; }
  .ok { color: var(--accent); }
  .bad { color: var(--bad); }
  .note { color: var(--warn); }
  .wide { grid-column: 1 / -1; }
  a { color: #8bdff4; }
</style>
</head>
<body>
<header>
  <h1>flow.io Rescue</h1>
  <p>Console minimale embarquee dans le firmware Waveshare. Elle reste disponible meme si la partition SPIFFS ne contient plus l'interface web.</p>
</header>
<main>
  <section class="wide">
    <h2>Etat</h2>
    <div class="row">
      <button class="secondary" id="refresh" type="button">Rafraichir</button>
      <button class="secondary" id="scan" type="button">Scanner le réseau</button>
      <a href="/webinterface?full=1" style="align-self:center">Essayer l'interface complete</a>
    </div>
    <div class="status" id="status">Chargement...</div>
  </section>

  <div class="grid">
    <section>
      <h2>Réseau Waveshare</h2>
      <label><input id="wifiEnabled" type="checkbox" checked />Activer le réseau station</label>
      <label for="wifiList">Reseaux detectes</label>
      <select id="wifiList"><option value="">Saisie manuelle</option></select>
      <label for="ssid">SSID</label>
      <input id="ssid" autocomplete="off" />
      <label for="pass">Mot de passe</label>
      <input id="pass" type="password" autocomplete="off" />
      <div class="row">
        <button id="saveWifi" type="button">Enregistrer réseau</button>
      </div>
      <div class="status" id="wifiMsg">-</div>
    </section>

    <section>
      <h2>Serveur d'upgrade</h2>
      <label for="host">Hote HTTP</label>
      <input id="host" placeholder="192.168.1.10:8000" autocomplete="off" />
      <label for="basePath">Chemin de base</label>
      <input id="basePath" placeholder="/binary" autocomplete="off" />
      <div class="row">
        <button id="saveFwCfg" type="button">Enregistrer</button>
        <button class="secondary" id="checkManifest" type="button">Manifest</button>
      </div>
      <div class="status" id="fwCfgMsg">-</div>
    </section>

    <section class="wide">
      <h2>Upgrade de secours</h2>
      <p>Renseigner une URL explicite vers l'image a installer.</p>
      <label for="waveshareUrl">URL explicite firmware Waveshare</label>
      <input id="waveshareUrl" placeholder="http://serveur/binary/waveshare.bin" autocomplete="off" />
      <label for="spiffsUrl">URL explicite image SPIFFS</label>
      <input id="spiffsUrl" placeholder="http://serveur/binary/waveshare-spiffs.bin" autocomplete="off" />
      <div class="row">
        <button class="warn" id="updateSpiffs" type="button">Upgrade SPIFFS</button>
        <button class="warn" id="updateWaveshare" type="button">Upgrade Waveshare</button>
      </div>
      <div class="status" id="updateMsg">-</div>
    </section>
  </div>
</main>
<script>
(() => {
  const $ = (id) => document.getElementById(id);
  const status = $("status");
  const wifiMsg = $("wifiMsg");
  const fwCfgMsg = $("fwCfgMsg");
  const updateMsg = $("updateMsg");
  const buttons = Array.from(document.querySelectorAll("button"));

  const setBusy = (busy) => buttons.forEach((b) => { b.disabled = busy; });
  const formBody = (data) => {
    const body = new URLSearchParams();
    Object.keys(data).forEach((k) => {
      if (data[k] !== undefined && data[k] !== null) body.set(k, data[k]);
    });
    return body;
  };
  const api = async (url, options = {}) => {
    const res = await fetch(url, Object.assign({ cache: "no-store" }, options));
    const text = await res.text();
    let json = null;
    try { json = text ? JSON.parse(text) : null; } catch (_) {}
    if (!res.ok || (json && json.ok === false)) {
      const msg = json && json.err ? (json.err.msg || json.err.code || "failed") : (text || res.statusText);
      throw new Error(msg);
    }
    return json || { ok: true, text };
  };
  const put = (node, obj, cls) => {
    node.className = "status" + (cls ? " " + cls : "");
    node.textContent = typeof obj === "string" ? obj : JSON.stringify(obj, null, 2);
  };

  async function refreshAll() {
    setBusy(true);
    try {
      const [meta, net, wifi, fw, fwst] = await Promise.all([
        api("/api/web/meta").catch((e) => ({ ok:false, err:e.message })),
        api("/api/network/mode").catch((e) => ({ ok:false, err:e.message })),
        api("/api/wifi/config").catch((e) => ({ ok:false, err:e.message })),
        api("/api/fwupdate/config").catch((e) => ({ ok:false, err:e.message })),
        api("/api/fwupdate/status").catch((e) => ({ ok:false, err:e.message }))
      ]);
      if (wifi.ok !== false) {
        $("wifiEnabled").checked = wifi.enabled !== false;
        $("ssid").value = wifi.ssid || "";
        $("pass").value = wifi.pass || "";
      }
      if (fw.ok !== false) {
        $("host").value = fw.update_host || "";
        $("basePath").value = fw.update_path || "";
      }
      put(status, { web: meta, network: net, updater: fwst }, "ok");
    } catch (e) {
      put(status, e.message, "bad");
    } finally {
      setBusy(false);
    }
  }

  async function scanWifi() {
    setBusy(true);
    try {
      await api("/api/wifi/scan", { method: "POST", body: formBody({ force: "1" }) });
      put(wifiMsg, "Scan lance, attente des resultats...", "note");
      let scan = null;
      let lastErr = null;
      for (let attempt = 0; attempt < 8; ++attempt) {
        try {
          scan = await api("/api/wifi/scan");
          const list = $("wifiList");
          list.innerHTML = '<option value="">Saisie manuelle</option>';
          (scan.networks || []).forEach((net) => {
            const opt = document.createElement("option");
            opt.value = net.ssid || "";
            opt.textContent = `${net.ssid || "<hidden>"} (${net.rssi} dBm)`;
            list.appendChild(opt);
          });
          put(wifiMsg, scan, "ok");
          if (!scan.running && !scan.requested) break;
        } catch (e) {
          lastErr = e;
        }
        await new Promise((resolve) => setTimeout(resolve, 450));
      }
      if (!scan && lastErr) throw lastErr;
    } catch (e) {
      put(wifiMsg, e.message, "bad");
    } finally {
      setBusy(false);
    }
  }

  async function saveWifi() {
    setBusy(true);
    try {
      const out = await api("/api/wifi/config", {
        method: "POST",
        body: formBody({
          enabled: $("wifiEnabled").checked ? "1" : "0",
          ssid: $("ssid").value.trim(),
          pass: $("pass").value
        })
      });
      put(wifiMsg, out.reboot_scheduled ? "Réseau enregistre. Redemarrage planifie." : "Réseau enregistre.", "ok");
      await refreshAll();
    } catch (e) {
      put(wifiMsg, e.message, "bad");
    } finally {
      setBusy(false);
    }
  }

  async function saveFwConfig() {
    setBusy(true);
    try {
      const out = await api("/api/fwupdate/config", {
        method: "POST",
        body: formBody({
          update_host: $("host").value.trim(),
          update_path: $("basePath").value.trim()
        })
      });
      put(fwCfgMsg, out, "ok");
      await refreshAll();
    } catch (e) {
      put(fwCfgMsg, e.message, "bad");
    } finally {
      setBusy(false);
    }
  }

  async function checkManifest() {
    setBusy(true);
    try {
      const started = await api("/api/fwupdate/check", { method: "POST" });
      const requestId = Number(started && started.request_id);
      if (!Number.isFinite(requestId) || requestId <= 0) {
        throw new Error("identifiant de vérification invalide");
      }
      const deadline = Date.now() + 85000;
      let out = null;
      while (Date.now() < deadline) {
        out = await api("/api/fwupdate/check?request_id=" + encodeURIComponent(String(requestId)));
        if (out && out.state === "ready") break;
        if (!out || (out.state !== "queued" && out.state !== "downloading")) {
          throw new Error("état de vérification inattendu");
        }
        await new Promise((resolve) => setTimeout(resolve, 450));
      }
      if (!out || out.state !== "ready") {
        throw new Error("délai de vérification du manifest dépassé");
      }
      put(fwCfgMsg, out, "ok");
    } catch (e) {
      put(fwCfgMsg, e.message, "bad");
    } finally {
      setBusy(false);
    }
  }

  async function startUpdate(target) {
    const isSpiffs = target === "spiffs";
    const url = (isSpiffs ? $("spiffsUrl").value : $("waveshareUrl").value).trim();
    const label = isSpiffs ? "SPIFFS" : "Waveshare";
    if (!confirm("Lancer l'upgrade " + label + " ?")) return;
    setBusy(true);
    try {
      const out = await api("/fwupdate/" + target, {
        method: "POST",
        body: formBody(url ? { url } : {})
      });
      put(updateMsg, out, "ok");
      pollStatus();
    } catch (e) {
      put(updateMsg, e.message, "bad");
    } finally {
      setBusy(false);
    }
  }

  async function pollStatus() {
    try {
      const out = await api("/api/fwupdate/status");
      put(updateMsg, out, out.state === "error" ? "bad" : "ok");
      if (out.busy || out.pending || ["queued","downloading","flashing","rebooting"].includes(out.state)) {
        setTimeout(pollStatus, 1500);
      }
    } catch (e) {
      put(updateMsg, e.message, "bad");
    }
  }

  $("wifiList").addEventListener("change", () => {
    if ($("wifiList").value) $("ssid").value = $("wifiList").value;
  });
  $("refresh").addEventListener("click", refreshAll);
  $("scan").addEventListener("click", scanWifi);
  $("saveWifi").addEventListener("click", saveWifi);
  $("saveFwCfg").addEventListener("click", saveFwConfig);
  $("checkManifest").addEventListener("click", checkManifest);
  $("updateSpiffs").addEventListener("click", () => startUpdate("spiffs"));
  $("updateWaveshare").addEventListener("click", () => startUpdate("waveshare"));
  refreshAll();
  pollStatus();
})();
</script>
</body>
</html>
)HTML";

static const char kWebSerialLogPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Micronova - Logs Locaux</title>
<style>
  :root { color-scheme: dark; }
  html, body { margin: 0; padding: 0; background: #091220; color: #dbeafe; font-family: "SFMono-Regular", Menlo, Monaco, Consolas, monospace; }
  .top { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 10px 12px; background: #0f1b2f; border-bottom: 1px solid #1d304f; }
  .status { font-size: 12px; opacity: 0.9; }
  .actions { display: flex; gap: 8px; }
  button, a { border: 1px solid #2b4b78; background: #142742; color: #dbeafe; text-decoration: none; border-radius: 6px; padding: 6px 10px; font-size: 12px; }
  button:hover, a:hover { background: #1d3963; cursor: pointer; }
  #out { white-space: pre-wrap; word-break: break-word; margin: 0; padding: 12px; height: calc(100vh - 56px); overflow: auto; font-size: 12px; line-height: 1.35; }
  .line { display: block; }
  .log-d { color: #93c5fd; }
  .log-i { color: #86efac; }
  .log-w { color: #fde68a; }
  .log-e { color: #fca5a5; font-weight: 600; }
  .log-t { color: #d8b4fe; }
  .ansi-red { color: #f87171; }
  .ansi-green { color: #4ade80; }
  .ansi-yellow { color: #facc15; }
  .ansi-blue { color: #60a5fa; }
  .ansi-magenta { color: #c084fc; }
  .ansi-cyan { color: #22d3ee; }
  .ansi-white { color: #e5e7eb; }
  .ansi-gray { color: #9ca3af; }
</style>
</head>
<body>
  <div class="top">
    <div class="status" id="status">Connexion...</div>
    <div class="actions">
      <button id="bootlog" type="button">Boot</button>
      <button id="pause" type="button">Pause</button>
      <button id="clear" type="button">Clear</button>
      <a href="/webinterface">Retour UI</a>
    </div>
  </div>
  <pre id="out"></pre>
<script>
(() => {
  const out = document.getElementById('out');
  const status = document.getElementById('status');
  const bootlogBtn = document.getElementById('bootlog');
  const pauseBtn = document.getElementById('pause');
  const clearBtn = document.getElementById('clear');
  let paused = false;
  let ws = null;

  const MAX_LINES = 1200;

  const levelClassFor = (line) => {
    if (line.includes("][E][")) return "log-e";
    if (line.includes("][W][")) return "log-w";
    if (line.includes("][I][")) return "log-i";
    if (line.includes("][D][")) return "log-d";
    if (line.includes("][T][")) return "log-t";
    return "";
  };

  const ansiClassForCode = (code) => {
    if (code === 31) return "ansi-red";
    if (code === 32) return "ansi-green";
    if (code === 33) return "ansi-yellow";
    if (code === 34) return "ansi-blue";
    if (code === 35) return "ansi-magenta";
    if (code === 36) return "ansi-cyan";
    if (code === 37) return "ansi-white";
    if (code === 90) return "ansi-gray";
    return "";
  };

  const appendChunk = (parent, text, cls) => {
    if (!text) return;
    const span = document.createElement("span");
    if (cls) span.className = cls;
    span.textContent = text;
    parent.appendChild(span);
  };

  const appendAnsiAware = (parent, line, baseClass) => {
    const re = /\x1b\[([0-9;]*)m/g;
    let cursor = 0;
    let activeAnsi = "";
    let found = false;
    let m;
    while ((m = re.exec(line)) !== null) {
      found = true;
      const start = m.index;
      appendChunk(parent, line.slice(cursor, start), activeAnsi || baseClass);
      const codes = (m[1] || "0").split(";");
      for (const codeText of codes) {
        const code = Number(codeText || "0");
        if (code === 0) {
          activeAnsi = "";
          continue;
        }
        const mapped = ansiClassForCode(code);
        if (mapped) activeAnsi = mapped;
      }
      cursor = re.lastIndex;
    }
    if (!found) {
      appendChunk(parent, line, baseClass);
      return;
    }
    appendChunk(parent, line.slice(cursor), activeAnsi || baseClass);
  };

  const trimLines = () => {
    while (out.childNodes.length > MAX_LINES) {
      out.removeChild(out.firstChild);
    }
  };

  const append = (line, force = false) => {
    if (paused && !force) return;
    const row = document.createElement("span");
    row.className = "line";
    appendAnsiAware(row, line, levelClassFor(line));
    out.appendChild(row);
    trimLines();
    out.scrollTop = out.scrollHeight;
  };

  const open = () => {
    const scheme = location.protocol === "https:" ? "wss" : "ws";
    ws = new WebSocket(`${scheme}://${location.host}/wslog`);
    ws.onopen = () => { status.textContent = "Connecté à /wslog"; };
    ws.onmessage = (ev) => { append(String(ev.data || "")); };
    ws.onclose = () => {
      status.textContent = "Déconnecté, reconnexion...";
      setTimeout(open, 1200);
    };
    ws.onerror = () => {
      status.textContent = "Erreur WebSocket";
    };
  };

  const loadBootLogs = async () => {
    const limit = 64;
    let offset = 0;
    let loaded = 0;
    let entries = 0;
    let dropped = 0;
    let state = "unknown";
    bootlogBtn.disabled = true;
    status.textContent = "Boot logs : chargement...";
    try {
      while (true) {
        const response = await fetch(`/api/logs/boot?offset=${encodeURIComponent(offset)}&limit=${limit}`);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const page = await response.json();
        const lines = Array.isArray(page.lines) ? page.lines : [];
        entries = Number(page.entries) || 0;
        dropped = Number(page.dropped) || 0;
        state = String(page.state || "unknown");
        if (offset === 0) {
          append(`===== BOOT LOGS BEGIN - ${entries} lignes disponibles, dropped=${dropped}, state=${state} =====`, true);
        }
        for (const line of lines) append(String(line || ""), true);
        loaded += Number(page.count) || lines.length;
        status.textContent = `Boot logs : ${loaded}/${entries} lignes affichees, ${dropped} perdues, etat=${state}`;
        if (page.complete || page.next == null || Number(page.count) === 0) break;
        offset = Number(page.next);
        if (!Number.isFinite(offset) || offset < 0) break;
        await new Promise((resolve) => setTimeout(resolve, 10));
      }
      append(`===== BOOT LOGS END - ${loaded}/${entries} lignes affichees =====`, true);
      status.textContent = `Boot logs : ${loaded}/${entries} lignes affichees, ${dropped} perdues, etat=${state}`;
    } catch (err) {
      const msg = `Impossible de charger les logs de boot : ${err && err.message ? err.message : String(err)}`;
      append(msg, true);
      status.textContent = msg;
      console.error(err);
    } finally {
      bootlogBtn.disabled = false;
    }
  };

  pauseBtn.addEventListener('click', () => {
    paused = !paused;
    pauseBtn.textContent = paused ? "Reprendre" : "Pause";
  });
  bootlogBtn.addEventListener('click', () => {
    loadBootLogs();
  });
  clearBtn.addEventListener('click', () => { out.textContent = ""; });

  open();
})();
</script>
</body>
</html>
)HTML";

WebInterfaceModule::WebInterfaceModule(const BoardSpec& board)
{
    const UartSpec* uart = webBridgeUartSpec_(board);
    if (uart) {
        bridgeUartConfigured_ = true;
        uartBaud_ = uart->baud;
        uartRxPin_ = uart->rxPin;
        uartTxPin_ = uart->txPin;
    } else {
        bridgeUartConfigured_ = false;
    }
    provisioningDisableAfterConfigured_ = board.provisioning.disableAfterConfigured;
    provisioningRequireMqttForConfigured_ = board.provisioning.requireMqttForConfigured;
}

WebInterfaceModule::~WebInterfaceModule()
{
    freeLocalLogQueue_();
    freeRuntimeValuesBodyScratch_();
#if defined(FLOW_PROFILE_WAVESHARE)
    if (ioResponseSnapshot_) heap_caps_free(ioResponseSnapshot_);
#endif
}

#if defined(FLOW_PROFILE_WAVESHARE)
void WebInterfaceModule::refreshIoResponseCaches_()
{
    constexpr uint32_t kRuntimeRefreshMs = 1000U;
    constexpr uint32_t kTopologySafetyRefreshMs = 60000U;
    constexpr uint32_t kRetryDelayMs = 250U;
    constexpr size_t kTopologyCapacity = 32U * 1024U;
    constexpr size_t kRuntimeCapacity = 24U * 1024U;

    const uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - ioLastResponseBuildAttemptMs_) < kRetryDelayMs) return;

    bool hasTopology = false;
    bool hasRuntime = false;
    uint32_t topologyChangeGeneration = 0U;
    uint32_t topologyBuiltGeneration = 0U;
    portENTER_CRITICAL(&ioResponseMux_);
    hasTopology = (bool)ioTopologyResponse_;
    hasRuntime = (bool)ioRuntimeResponse_;
    topologyChangeGeneration = ioTopologyChangeGeneration_;
    topologyBuiltGeneration = ioTopologyBuiltGeneration_;
    portEXIT_CRITICAL(&ioResponseMux_);

    const bool topologyDue = topologyChangeGeneration != topologyBuiltGeneration || !hasTopology ||
        (uint32_t)(nowMs - ioLastTopologyBuildMs_) >= kTopologySafetyRefreshMs;
    const bool runtimeDue = !hasRuntime ||
        (uint32_t)(nowMs - ioLastRuntimeBuildMs_) >= kRuntimeRefreshMs;
    if (!topologyDue && !runtimeDue) return;
    ioLastResponseBuildAttemptMs_ = nowMs;

    if (!ioSvc_ && services_) {
        ioSvc_ = services_->get<IOServiceV2>(ServiceId::Io);
    }
    const DomainStatusService* domainStatusSvc = services_
        ? services_->get<DomainStatusService>(ServiceId::DomainStatus)
        : nullptr;
    if (!ioSvc_ || !domainStatusSvc) return;

    if (!ioResponseSnapshot_) {
        ioResponseSnapshot_ = heap_caps_malloc(sizeof(WaveshareIoResponseSnapshot),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    auto* snapshot = static_cast<WaveshareIoResponseSnapshot*>(ioResponseSnapshot_);
    if (!snapshot) {
        LOGW("IO web snapshot allocation failed bytes=%u", (unsigned)sizeof(WaveshareIoResponseSnapshot));
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    waveshareCollectIoResponseSnapshot_(ioSvc_, domainStatusSvc, *snapshot);

    uint32_t publishedRevision = ioTopologyRevision_;
    if (topologyDue) {
        const uint32_t nextRevision = ioTopologyRevision_ + 1U;
        auto next = std::make_shared<WebJsonBuffer>(kTopologyCapacity);
        if (next && next->valid()) {
            buildWaveshareIoTopologyResponse_(*next, *snapshot, nextRevision);
        }
        if (next && next->finish()) {
            std::shared_ptr<WebJsonBuffer> previous;
            portENTER_CRITICAL(&ioResponseMux_);
            previous.swap(ioTopologyResponse_);
            ioTopologyResponse_.swap(next);
            ioTopologyRevision_ = nextRevision;
            ioTopologyBuiltGeneration_ = topologyChangeGeneration;
            publishedRevision = nextRevision;
            portEXIT_CRITICAL(&ioResponseMux_);
            ioLastTopologyBuildMs_ = nowMs;
            LOGI("IO topology cache ready revision=%lu bytes=%u capacity=%u memory=psram",
                 (unsigned long)nextRevision,
                 (unsigned)ioTopologyResponse_->length(),
                 (unsigned)kTopologyCapacity);
        } else {
            LOGE("IO topology cache overflow/allocation failure capacity=%u overflow=%u",
                 (unsigned)kTopologyCapacity,
                 (unsigned)(next && next->overflowed()));
        }
    }

    if (runtimeDue) {
        std::shared_ptr<WebJsonBuffer> next;
        portENTER_CRITICAL(&ioResponseMux_);
        if (ioRuntimeSpareResponse_ && ioRuntimeSpareResponse_.use_count() == 1) {
            next.swap(ioRuntimeSpareResponse_);
        }
        portEXIT_CRITICAL(&ioResponseMux_);
        if (next) next->reset();
        else next = std::make_shared<WebJsonBuffer>(kRuntimeCapacity);
        if (next && next->valid()) {
            buildWaveshareIoRuntimeResponse_(*next, *snapshot, publishedRevision);
        }
        if (next && next->finish()) {
            std::shared_ptr<WebJsonBuffer> previous;
            portENTER_CRITICAL(&ioResponseMux_);
            previous.swap(ioRuntimeResponse_);
            ioRuntimeResponse_.swap(next);
            if (!ioRuntimeSpareResponse_) {
                ioRuntimeSpareResponse_.swap(previous);
            }
            portEXIT_CRITICAL(&ioResponseMux_);
            ioLastRuntimeBuildMs_ = nowMs;
        } else {
            LOGE("IO runtime cache overflow/allocation failure capacity=%u overflow=%u",
                 (unsigned)kRuntimeCapacity,
                 (unsigned)(next && next->overflowed()));
        }
    }

}

void WebInterfaceModule::sendIoResponseCache_(AsyncWebServerRequest* request, bool topology)
{
    if (!request) return;
    std::shared_ptr<WebJsonBuffer> state;
    portENTER_CRITICAL(&ioResponseMux_);
    state = topology ? ioTopologyResponse_ : ioRuntimeResponse_;
    portEXIT_CRITICAL(&ioResponseMux_);

    if (!state || !state->valid() || state->length() == 0U) {
        request->send(503,
                      "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"io.cache\"}}");
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(
        "application/json",
        state->length(),
        [state](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
            return state->fillAt(buffer, maxLen, index);
        }
    );
    addNoCacheHeaders_(response);
    request->send(response);
}

#endif

void WebInterfaceModule::initRuntimeValuesBodyScratch_()
{
#if defined(FLOW_PROFILE_WAVESHARE)
    if (runtimeValuesBodyScratch_) return;
    void* ptr = nullptr;
    bool allocatedInPsram = false;
    if (psramFound()) {
        ptr = heap_caps_malloc(kRuntimeValuesBodyMax + 1U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        allocatedInPsram = (ptr != nullptr);
    }
    if (!ptr) {
        ptr = heap_caps_malloc(kRuntimeValuesBodyMax + 1U, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!ptr) {
        LOGW("Web runtime body scratch unavailable size=%u", (unsigned)kRuntimeValuesBodyMax);
        return;
    }
    runtimeValuesBodyScratch_ = static_cast<char*>(ptr);
    runtimeValuesBodyScratch_[0] = '\0';
    runtimeValuesBodyScratchOwned_ = true;
    runtimeValuesBodyScratchInPsram_ = allocatedInPsram;
    LOGI("Web runtime body scratch allocated size=%u memory=%s free_psram=%luKB",
         (unsigned)kRuntimeValuesBodyMax,
         runtimeValuesBodyScratchInPsram_ ? "psram" : "internal",
         (unsigned long)(ESP.getFreePsram() / 1024U));
#endif
}

void WebInterfaceModule::freeRuntimeValuesBodyScratch_()
{
#if defined(FLOW_PROFILE_WAVESHARE)
    if (runtimeValuesBodyScratchOwned_ && runtimeValuesBodyScratch_) {
        heap_caps_free(runtimeValuesBodyScratch_);
    }
    runtimeValuesBodyScratch_ = nullptr;
    runtimeValuesBodyScratchInPsram_ = false;
    runtimeValuesBodyScratchOwned_ = false;
#endif
}

bool WebInterfaceModule::initLocalLogQueue_()
{
    if (localLogQueue_) return true;

    const size_t storageBytes = (size_t)kLocalLogQueueLen * kLocalLogLineMax;
#if defined(FLOW_PROFILE_WAVESHARE)
    if (psramFound()) {
        localLogQueueStorage_ = static_cast<uint8_t*>(
            heap_caps_malloc(storageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
        if (localLogQueueStorage_) {
            localLogQueueStorageInPsram_ = true;
        } else {
            LOGW("wslog queue PSRAM allocation failed, fallback to internal RAM lines=%u bytes=%u",
                 (unsigned)kLocalLogQueueLen,
                 (unsigned)storageBytes);
        }
    } else {
        LOGW("wslog queue PSRAM unavailable, fallback to internal RAM lines=%u bytes=%u",
             (unsigned)kLocalLogQueueLen,
             (unsigned)storageBytes);
    }
#endif
    if (!localLogQueueStorage_) {
        localLogQueueStorage_ = static_cast<uint8_t*>(
            heap_caps_malloc(storageBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
        );
    }
    if (localLogQueueStorage_) {
        localLogQueueControl_ = static_cast<StaticQueue_t*>(
            heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
        );
    }
    if (localLogQueueStorage_ && localLogQueueControl_) {
        localLogQueue_ = xQueueCreateStatic(kLocalLogQueueLen,
                                            kLocalLogLineMax,
                                            localLogQueueStorage_,
                                            localLogQueueControl_);
        localLogQueueStatic_ = (localLogQueue_ != nullptr);
    }

    if (localLogQueue_) {
        LOGI(localLogQueueStorageInPsram_
                 ? "wslog queue allocated in PSRAM lines=%u bytes=%u"
                 : "wslog queue allocated in internal RAM lines=%u bytes=%u",
             (unsigned)kLocalLogQueueLen,
             (unsigned)storageBytes);
        return true;
    }

    if (localLogQueueStorage_) {
        heap_caps_free(localLogQueueStorage_);
        localLogQueueStorage_ = nullptr;
    }
    if (localLogQueueControl_) {
        heap_caps_free(localLogQueueControl_);
        localLogQueueControl_ = nullptr;
    }
    localLogQueueStorageInPsram_ = false;
    localLogQueueStatic_ = false;

    localLogQueue_ = xQueueCreate(kLocalLogQueueLen, kLocalLogLineMax);
    if (localLogQueue_) {
        LOGW("wslog queue static allocation failed, fallback to FreeRTOS dynamic lines=%u bytes=%u",
             (unsigned)kLocalLogQueueLen,
             (unsigned)storageBytes);
        return true;
    }

    LOGW("WebInterface local log queue alloc failed lines=%u bytes=%u",
         (unsigned)kLocalLogQueueLen,
         (unsigned)storageBytes);
    return false;
}

void WebInterfaceModule::freeLocalLogQueue_()
{
    if (localLogQueue_) {
        vQueueDelete(localLogQueue_);
        localLogQueue_ = nullptr;
    }
    if (localLogQueueStorage_) {
        heap_caps_free(localLogQueueStorage_);
        localLogQueueStorage_ = nullptr;
    }
    if (localLogQueueControl_) {
        heap_caps_free(localLogQueueControl_);
        localLogQueueControl_ = nullptr;
    }
    localLogQueueStatic_ = false;
    localLogQueueStorageInPsram_ = false;
}

bool WebInterfaceModule::writeBootLogJsonLine_(void* writerCtx,
                                               const LogEntry& e,
                                               uint16_t,
                                               uint16_t)
{
    BootLogJsonPageCtx* ctx = static_cast<BootLogJsonPageCtx*>(writerCtx);
    if (!ctx || !ctx->self || !ctx->response) return false;

    char line[kLocalLogLineMax] = {0};
    formatLogEntryLine_(ctx->self, e, line, sizeof(line), false);
    if (line[0] == '\0') return true;

    if (!ctx->first) {
        ctx->response->print(',');
    }
    ctx->first = false;
    printJsonEscaped_(*ctx->response, line);
    ++ctx->count;
    return true;
}

void WebInterfaceModule::sendBootLogHttpResponse_(AsyncWebServerRequest* request, bool statusOnly)
{
    if (!request) return;
    noteHttpActivity_();

#if FLOW_ENABLE_BOOT_LOG_CAPTURE
    if (!bootLogCapture_) {
        bootLogCapture_ = bootLogCaptureService();
    }
#endif
    BootLogCaptureStats stats{};
    bool available = false;
    if (bootLogCapture_ && bootLogCapture_->getStats) {
        bootLogCapture_->getStats(bootLogCapture_->ctx, &stats);
        available = (stats.capacity > 0U);
    }
    const char* state = available
        ? (stats.capturing ? "capture" : (stats.complete ? "complete" : "idle"))
        : "unavailable";

    int32_t requestedOffset = statusOnly ? 0 : requestIntParam_(request, "offset", 0);
    int32_t requestedLimit = statusOnly ? 0 : requestIntParam_(request, "limit", 64);
    if (requestedOffset < 0) requestedOffset = 0;
    if (requestedLimit <= 0) requestedLimit = statusOnly ? 0 : 64;
    if (requestedLimit > 128) requestedLimit = 128;

    const uint16_t offset = (requestedOffset > UINT16_MAX) ? UINT16_MAX : (uint16_t)requestedOffset;
    const uint16_t limit = (requestedLimit > UINT16_MAX) ? UINT16_MAX : (uint16_t)requestedLimit;

    LOGI("bootlog status entries=%u capacity=%u dropped=%lu state=%s",
         (unsigned)stats.count,
         (unsigned)stats.capacity,
         (unsigned long)stats.droppedCount,
         state);

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    addNoCacheHeaders_(response);
    response->printf("{\"capacity\":%u,\"entries\":%u,\"dropped\":%lu,\"state\":\"%s\"",
                     (unsigned)stats.capacity,
                     (unsigned)stats.count,
                     (unsigned long)stats.droppedCount,
                     state);

    if (statusOnly) {
        response->print('}');
        request->send(response);
        return;
    }

    BootLogJsonPageCtx pageCtx{};
    pageCtx.self = this;
    pageCtx.response = response;

    const uint16_t writableLimit = available ? limit : 0U;
    uint16_t expectedCount = 0U;
    if (available && bootLogCapture_ && bootLogCapture_->readPage && offset < stats.count && writableLimit > 0U) {
        const uint16_t remaining = (uint16_t)(stats.count - offset);
        expectedCount = (writableLimit < remaining) ? writableLimit : remaining;
    }
    const bool expectedComplete = ((uint32_t)offset + (uint32_t)expectedCount >= stats.count) ||
                                  expectedCount == 0U;
    const int32_t expectedNext = expectedComplete ? -1 : (int32_t)offset + (int32_t)expectedCount;

    response->printf(",\"offset\":%u,\"limit\":%u,\"count\":%u,\"next\":",
                     (unsigned)offset,
                     (unsigned)limit,
                     (unsigned)expectedCount);
    if (expectedNext < 0) {
        response->print("null");
    } else {
        response->print((unsigned)expectedNext);
    }
    response->printf(",\"complete\":%s,\"lines\":[", expectedComplete ? "true" : "false");

    uint16_t count = 0;
    if (available && bootLogCapture_->readPage && offset < stats.count && writableLimit > 0U) {
        count = bootLogCapture_->readPage(bootLogCapture_->ctx,
                                          offset,
                                          writableLimit,
                                          &WebInterfaceModule::writeBootLogJsonLine_,
                                          &pageCtx);
    }
    response->print("]}");

    if (available && bootLogCapture_->readPage && offset < stats.count && writableLimit > 0U) {
        const bool complete = ((uint32_t)offset + (uint32_t)count >= stats.count) || count == 0U;
        const int32_t next = complete ? -1 : (int32_t)offset + (int32_t)count;
        LOGI("bootlog page offset=%u limit=%u count=%u entries=%u complete=%s next=%ld",
             (unsigned)offset,
             (unsigned)limit,
             (unsigned)count,
             (unsigned)stats.count,
             complete ? "true" : "false",
             (long)next);
    } else {
        LOGI("bootlog page offset=%u limit=%u count=0 entries=%u complete=true",
             (unsigned)offset,
             (unsigned)limit,
             (unsigned)stats.count);
    }

    request->send(response);
}

void WebInterfaceModule::sendActivityLogHttpResponse_(AsyncWebServerRequest* request, bool statusOnly)
{
    if (!request) return;
    noteHttpActivity_();

    if (!activityLog_ && services_) {
        activityLog_ = services_->get<ActivityLogService>(ServiceId::ActivityLog);
    }

    ActivityLogStats stats{};
    bool available = false;
    if (activityLog_ && activityLog_->getStats) {
        activityLog_->getStats(activityLog_->ctx, &stats);
        available = (stats.capacity > 0U);
    }

    int32_t requestedOffset = statusOnly ? 0 : requestIntParam_(request, "offset", 0);
    int32_t requestedLimit = statusOnly ? 0 : requestIntParam_(request, "limit", 32);
    if (requestedOffset < 0) requestedOffset = 0;
    if (requestedLimit <= 0) requestedLimit = statusOnly ? 0 : 32;
    if (requestedLimit > 64) requestedLimit = 64;

    const uint16_t offset = (requestedOffset > UINT16_MAX) ? UINT16_MAX : (uint16_t)requestedOffset;
    const uint16_t limit = (requestedLimit > UINT16_MAX) ? UINT16_MAX : (uint16_t)requestedLimit;

    if (statusOnly) {
        char out[320] = {0};
        snprintf(out,
                 sizeof(out),
                 "{\"available\":%s,\"capacity\":%u,\"entries\":%u,\"dropped\":%lu,"
                 "\"persisted\":%lu,\"persist_dropped\":%lu,\"psram\":%s,\"spiffs\":%s}",
                 available ? "true" : "false",
                 (unsigned)stats.capacity,
                 (unsigned)stats.count,
                 (unsigned long)stats.droppedCount,
                 (unsigned long)stats.persistedCount,
                 (unsigned long)stats.persistDropCount,
                 stats.psram ? "true" : "false",
                 stats.spiffs ? "true" : "false");
        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", out);
        addNoCacheHeaders_(response);
        request->send(response);
        return;
    }

    char order[8] = {0};
    copyRequestParamValue_(request, "order", false, order, sizeof(order), "asc");
    const bool descending = strcasecmp(order, "desc") == 0;

    auto state = std::make_shared<ActivityLogChunkState>();
    if (!state || !state->begin(available ? activityLog_ : nullptr,
                                stats,
                                offset,
                                available ? limit : 0U,
                                descending)) {
        request->send(503,
                      "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"LowMemory\",\"where\":\"activity.snapshot\"}}");
        return;
    }

    AsyncWebServerResponse* response =
        request->beginChunkedResponse("application/json",
                                      [state](uint8_t* buffer, size_t maxLen, size_t) -> size_t {
                                          return state->fill(buffer, maxLen);
                                      });
    addNoCacheHeaders_(response);
    request->send(response);
}

void WebInterfaceModule::emitConfigActivity_(const char* contextLabel, const char* modulesLabel, uint16_t fieldCount)
{
    if (fieldCount == 0U) return;
    if (!activityLog_ && services_) {
        activityLog_ = services_->get<ActivityLogService>(ServiceId::ActivityLog);
    }
    if (!activityLog_ || !activityLog_->emit) return;

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
    if (modulesLabel && modulesLabel[0] != '\0') {
        snprintf(event.detail,
                 sizeof(event.detail),
                 "%s: %u champ(s) appliqué(s) dans %s.",
                 (contextLabel && contextLabel[0] != '\0') ? contextLabel : "Configuration",
                 (unsigned)fieldCount,
                 modulesLabel);
    } else {
        snprintf(event.detail,
                 sizeof(event.detail),
                 "%s: %u champ(s) appliqué(s).",
                 (contextLabel && contextLabel[0] != '\0') ? contextLabel : "Configuration",
                 (unsigned)fieldCount);
    }
    snprintf(event.icon, sizeof(event.icon), "settings");
    (void)activityLog_->emit(activityLog_->ctx, &event);
}

void WebInterfaceModule::emitConfigPatchActivity_(const char* contextLabel, const char* patchJson)
{
    char modules[72] = {0};
    const uint16_t fieldCount = summarizeConfigPatch_(patchJson, modules, sizeof(modules));
    emitConfigActivity_(contextLabel, modules, fieldCount);
}

void WebInterfaceModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;
    initRuntimeValuesBodyScratch_();

    services_ = &services;
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    logSinkReg_ = services.get<LogSinkRegistryService>(ServiceId::LogSinks);
#if FLOW_ENABLE_BOOT_LOG_CAPTURE
    bootLogCapture_ = bootLogCaptureService();
#else
    bootLogCapture_ = nullptr;
#endif
    activityLog_ = services.get<ActivityLogService>(ServiceId::ActivityLog);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    hmiSvc_ = services.get<HmiService>(ServiceId::Hmi);
    flowCfgSvc_ = services.get<FlowCfgRemoteService>(ServiceId::FlowCfg);
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    auto* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    fwUpdateSvc_ = services.get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &WebInterfaceModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::ConfigChanged, &WebInterfaceModule::onEventStatic_, this);
    }

    if (!services.add(ServiceId::WebInterface, &webInterfaceSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::WebInterface));
    }

    netReady_ = dataStore_ ? networkReady(*dataStore_) : false;

    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&healthMux_);
    health_.snapshotMs = nowMs;
    health_.lastLoopMs = nowMs;
    health_.lastHttpActivityMs = 0U;
    health_.lastWsActivityMs = 0U;
    health_.wsSerialClients = 0U;
    health_.wsLogClients = 0U;
    health_.started = started_;
    health_.paused = uartPaused_;
    portEXIT_CRITICAL(&healthMux_);

#if defined(FLOW_PROFILE_MICRONOVA) || defined(FLOW_PROFILE_WAVESHARE)
    LOGI("WebInterface local runtime deferred (server deferred)");
#else
    startLocalRuntime_();
#endif
}

void WebInterfaceModule::onStart(ConfigStore&, ServiceRegistry&)
{
#if defined(FLOW_PROFILE_MICRONOVA) || defined(FLOW_PROFILE_WAVESHARE)
    startLocalRuntime_();
#endif
}

void WebInterfaceModule::startLocalRuntime_()
{
#if defined(FLOW_PROFILE_WAVESHARE)
    // flow.io exposes its own LogHub on /wslog; there is no secondary UART
    // bridge to read, especially now that USB CDC on boot is disabled.
    bridgeUartEnabled_ = false;
#endif

    if (!initLocalLogQueue_()) return;
    if (!localLogSinkRegistered_ && localLogQueue_ && logSinkReg_ && logSinkReg_->add) {
        const LogSinkService sink{&WebInterfaceModule::onLocalLogSinkWrite_, this};
        if (logSinkReg_->add(logSinkReg_->ctx, sink)) {
            localLogSinkRegistered_ = true;
        } else {
            LOGW("WebInterface local log sink registration failed");
        }
    }

    if (provisioningOnly_) {
        bridgeUartEnabled_ = false;
        LOGI("WebInterface local runtime disabled in provisioning-only mode (wslog only)");
        return;
    }

#if defined(FLOW_PROFILE_WAVESHARE)
    LOGI("WebInterface local log runtime enabled on flow.io (serial bridge disabled)");
    return;
#endif

#if !FLOW_ENABLE_READONLY_SERIAL_LOG
    bridgeUartEnabled_ = false;
    LOGI("WebInterface serial log bridge disabled");
    return;
#endif

    if (!bridgeUartConfigured_) {
        bridgeUartEnabled_ = false;
        LOGI("WebInterface bridge UART disabled: no 'bridge' UART in BoardSpec (wslog only)");
        return;
    }

    uart_.setRxBufferSize(kUartRxBufferSize);
    uart_.begin(uartBaud_, SERIAL_8N1, uartRxPin_, uartTxPin_);
    bridgeUartEnabled_ = true;

    LOGI("WebInterface local runtime uart=Serial2 baud=%lu rx=%d tx=%d line_buf=%u rx_buf=%u",
         (unsigned long)uartBaud_,
         uartRxPin_,
         uartTxPin_,
         (unsigned)kLineBufferSize,
         (unsigned)kUartRxBufferSize);
}

void WebInterfaceModule::startServer_()
{
    if (started_) return;
    gHttpActivityHook = &WebInterfaceModule::onHttpActivityHook_;
    gHttpActivityHookCtx = this;

    spiffsReady_ = SPIFFS.begin(false);
    if (!spiffsReady_) {
        LOGW("SPIFFS mount failed; web assets unavailable");
    } else {
        LOGI("SPIFFS mounted for web assets");
    }

    auto spiffsAssetExists = [this](const char* assetPath, const char* gzipOverridePath = nullptr) -> bool {
        if (!spiffsReady_ || !assetPath || assetPath[0] == '\0') return false;
        if (gzipOverridePath && gzipOverridePath[0] != '\0') {
            return SPIFFS.exists(gzipOverridePath);
        }
        char gzipPath[128] = {0};
        const int gzipPathLen = snprintf(gzipPath, sizeof(gzipPath), "%s.gz", assetPath);
        if ((gzipPathLen > 0) && ((size_t)gzipPathLen < sizeof(gzipPath)) && SPIFFS.exists(gzipPath)) {
            return true;
        }
        return SPIFFS.exists(assetPath);
    };

    auto beginSpiffsAssetResponse =
        [this](AsyncWebServerRequest* request,
               const char* assetPath,
               const char* contentType,
               bool cacheAware,
               const char* gzipOverridePath = nullptr,
               SpiffsAssetForensicMeta* forensicMeta = nullptr,
               bool* heapRejected = nullptr,
               bool* buildBusy = nullptr) -> AsyncWebServerResponse* {
        if (!request || !assetPath || !contentType || !spiffsReady_) return nullptr;
        if (heapRejected) *heapRejected = false;
        if (buildBusy) *buildBusy = false;

        const size_t assetPathLen = strlen(assetPath);
        if (assetPathLen == 0U || assetPathLen >= 112U) return nullptr;

        if (!tryAcquireAssetBuildSlot_()) {
            if (buildBusy) *buildBusy = true;
            LOGW("Web asset busy path=%s reason=asset_build_busy in_flight=%u rejects=%lu",
                 assetPath,
                 (unsigned)gAssetBuildInFlight,
                 (unsigned long)gAssetBuildRejectCount);
            return nullptr;
        }
        struct AssetBuildSlotGuard {
            ~AssetBuildSlotGuard() { releaseAssetBuildSlot_(); }
        } slotGuard{};

        uint32_t freeBytes = 0U;
        uint32_t largestBytes = 0U;
        if (shouldRejectAssetByFreeHeap_(assetPath, &freeBytes, &largestBytes)) {
            if (heapRejected) *heapRejected = true;
            LOGW("Web asset busy path=%s reason=low_heap free=%lu largest=%lu",
                 assetPath,
                 (unsigned long)freeBytes,
                 (unsigned long)largestBytes);
            return nullptr;
        }

#if FLOW_WEB_HEAP_FORENSICS
        const uint32_t forensicStartUs = micros();
        const HeapForensicSnapshot forensicStartHeap = captureHeapForensicSnapshot_();
        SpiffsAssetForensicMeta localMeta{};
#endif

        char gzipPath[128] = {0};
        const char* servedPath = assetPath;
        bool hasGzip = false;
        bool servedExists = false;
        if (gzipOverridePath && gzipOverridePath[0] != '\0') {
            if (SPIFFS.exists(gzipOverridePath)) {
                servedPath = gzipOverridePath;
                hasGzip = true;
                servedExists = true;
            }
        } else {
            const int gzipPathLen = snprintf(gzipPath, sizeof(gzipPath), "%s.gz", assetPath);
            if ((gzipPathLen > 0) && ((size_t)gzipPathLen < sizeof(gzipPath)) && SPIFFS.exists(gzipPath)) {
                servedPath = gzipPath;
                hasGzip = true;
                servedExists = true;
            } else if (SPIFFS.exists(assetPath)) {
                servedPath = assetPath;
                servedExists = true;
            }
        }
        if (!servedExists) return nullptr;

#if FLOW_WEB_HEAP_FORENSICS
        uint32_t servedSize = 0U;
        File servedFile = SPIFFS.open(servedPath, FILE_READ);
        if (servedFile) {
            servedSize = (uint32_t)servedFile.size();
            servedFile.close();
        }
        fillSpiffsAssetForensicMeta_(&localMeta, servedPath, servedSize, hasGzip);
        if (forensicMeta) {
            *forensicMeta = localMeta;
        }
#endif

        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, servedPath, contentType);
        if (!response) {
#if FLOW_WEB_HEAP_FORENSICS
            logSpiffsAssetHeapForensic_("null", localMeta, forensicStartUs, forensicStartHeap);
#endif
            return nullptr;
        }
        response->addHeader("Vary", "Accept-Encoding");
        response->addHeader("Connection", "close");
        if (hasGzip) {
            response->addHeader("Content-Encoding", "gzip");
        }
        if (cacheAware) {
            addCacheAwareAssetHeaders_(request, response);
        } else {
            addNoCacheHeaders_(response);
        }
#if FLOW_WEB_HEAP_FORENSICS
        logSpiffsAssetHeapForensic_("prep", localMeta, forensicStartUs, forensicStartHeap);
#endif
        return response;
    };

    auto sendPreparedAssetResponse =
        [](AsyncWebServerRequest* request,
           AsyncWebServerResponse* response,
           const SpiffsAssetForensicMeta* forensicMeta = nullptr) {
        if (!request || !response) return;
#if FLOW_WEB_HEAP_FORENSICS
        const uint32_t forensicStartUs = micros();
        const HeapForensicSnapshot forensicStartHeap = captureHeapForensicSnapshot_();
#endif
        request->send(response);
#if FLOW_WEB_HEAP_FORENSICS
        if (forensicMeta) {
            logSpiffsAssetHeapForensic_("send", *forensicMeta, forensicStartUs, forensicStartHeap);
        }
#endif
    };

    auto webInterfaceLandingUrl = []() -> const char* {
        return "/webinterface";
    };

    auto sendRescuePage = [](AsyncWebServerRequest* request) {
        if (!request) return;
        AsyncWebServerResponse* response =
            request->beginResponse(200,
                                   "text/html",
                                   reinterpret_cast<const uint8_t*>(kWebInterfaceFallbackPage),
                                   sizeof(kWebInterfaceFallbackPage) - 1U);
        addNoCacheHeaders_(response);
        request->send(response);
    };

    const bool lightUiAssetsReady =
        spiffsAssetExists("/webinterface/light.html") &&
        spiffsAssetExists("/webinterface/light.css") &&
        spiffsAssetExists("/webinterface/light.js");

    const bool fullUiAssetsReady =
        spiffsAssetExists("/webinterface/index.html") &&
        spiffsAssetExists("/webinterface/app-core.js") &&
        spiffsAssetExists("/webinterface/app.js") &&
        spiffsAssetExists("/webinterface/app-core.css");

    const bool provisioningUiAssetsReady =
        spiffsAssetExists("/webinterface/prov.html") &&
        spiffsAssetExists("/webinterface/prov.js") &&
        spiffsAssetExists("/webinterface/app-core.css");

    auto lightUiAssetsAvailable = [lightUiAssetsReady]() -> bool {
        return lightUiAssetsReady;
    };

    auto fullUiAssetsAvailable = [fullUiAssetsReady]() -> bool {
        return fullUiAssetsReady;
    };

    auto provisioningUiAssetsAvailable = [provisioningUiAssetsReady]() -> bool {
        return provisioningUiAssetsReady;
    };

    server_.on("/", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });

    server_.on("/rescue", HTTP_GET, [sendRescuePage](AsyncWebServerRequest* request) {
        sendRescuePage(request);
    });

    server_.on("/webinterface/rescue", HTTP_GET, [sendRescuePage](AsyncWebServerRequest* request) {
        sendRescuePage(request);
    });

    server_.on("/webinterface/app.css", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/app-core.css", "text/css", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/app-core.css", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/app-core.css", "text/css", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/sh.html", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/sh.html", "text/html", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/app.js", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/app.js", "application/javascript", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/app-core.js", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/app-core.js", "application/javascript", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/i18n/fr.json", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/i18n/fr.json", "application/json", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "application/json", "{\"ok\":false,\"err\":{\"code\":\"NotFound\",\"where\":\"web.i18n.fr\"}}");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/i18n/en.json", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/i18n/en.json", "application/json", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "application/json", "{\"ok\":false,\"err\":{\"code\":\"NotFound\",\"where\":\"web.i18n.en\"}}");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/light.css", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/light.css", "text/css", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/light.js", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/light.js", "application/javascript", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/prov.html", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/prov.html", "text/html", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/prov.js", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/prov.js", "application/javascript", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/favicon.svg", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/favicon.svg", "image/svg+xml", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/favicon.png", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/favicon.png", "image/png", false, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/logo-flowio.png", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/logo-flowio.png", "image/png", false, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/runtimeui.json", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/runtimeui.json", "application/json", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response) {
            if (heapRejected || buildBusy) {
                sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                return;
            }
            request->send(404, "text/plain", "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/api/logs/boot/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/logs/boot/status");
        sendBootLogHttpResponse_(request, true);
    });
    server_.on("/api/logs/boot", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/logs/boot");
        sendBootLogHttpResponse_(request, false);
    });
    server_.on("/api/activity/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/activity/status");
        sendActivityLogHttpResponse_(request, true);
    });
    server_.on("/api/activity/logs", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/activity/logs");
        sendActivityLogHttpResponse_(request, false);
    });
    server_.on("/api/activity/purge", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/activity/purge");
        noteHttpActivity_();
        if (!activityLog_ && services_) {
            activityLog_ = services_->get<ActivityLogService>(ServiceId::ActivityLog);
        }
        if (!activityLog_ || !activityLog_->clear) {
            request->send(503, "application/json", "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"activity.clear\"}}");
            return;
        }
        const bool ok = activityLog_->clear(activityLog_->ctx);
        request->send(ok ? 200 : 500,
                      "application/json",
                      ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"activity.clear\"}}");
    });
    server_.on("/api/cfgdoc/index", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/cfgdoc/index");
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/wc/i.j", "application/json", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (response) {
            sendPreparedAssetResponse(request, response, &forensicMeta);
            return;
        }
        if (heapRejected || buildBusy) {
            sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
            return;
        }
        AsyncWebServerResponse* fallbackResponse =
            request->beginResponse(200, "application/json", "{\"ok\":true,\"modules\":{},\"docs\":{},\"meta\":{}}");
        addNoCacheHeaders_(fallbackResponse);
        request->send(fallbackResponse);
    });
    server_.on("/api/cfgdoc/i18n", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/cfgdoc/i18n");
        char localeRaw[24] = {0};
        copyRequestParamValue_(request, "locale", false, localeRaw, sizeof(localeRaw), "fr");

        char locale[24] = {0};
        size_t wi = 0U;
        for (size_t i = 0; localeRaw[i] != '\0' && wi + 1U < sizeof(locale); ++i) {
            char c = localeRaw[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!allowed) continue;
            locale[wi++] = c;
        }
        locale[wi] = '\0';
        if (locale[0] == '\0') {
            strncpy(locale, "fr", sizeof(locale) - 1);
            locale[sizeof(locale) - 1] = '\0';
        }

        char assetPath[64] = {0};
        int n = snprintf(assetPath, sizeof(assetPath), "/wc/i18n.%s.j", locale);
        if (n <= 0 || (size_t)n >= sizeof(assetPath)) {
            request->send(400, "application/json", "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"cfgdoc.i18n\"}}");
            return;
        }

        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, assetPath, "application/json", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (!response && strncmp(locale, "fr", sizeof(locale)) != 0) {
            response = beginSpiffsAssetResponse(
                request, "/wc/i18n.fr.j", "application/json", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        }
        if (response) {
            sendPreparedAssetResponse(request, response, &forensicMeta);
            return;
        }
        if (heapRejected || buildBusy) {
            sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
            return;
        }
        request->send(404, "application/json", "{\"ok\":false,\"err\":{\"code\":\"NotFound\",\"where\":\"cfgdoc.i18n\"}}");
    });
    server_.on("/api/cfgdoc/module", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/cfgdoc/module");
        char moduleName[96] = {0};
        copyRequestParamValue_(request, "name", false, moduleName, sizeof(moduleName), "");
        for (size_t i = 0; moduleName[i] != '\0'; ++i) {
            const char c = moduleName[i];
            if (c >= 'A' && c <= 'Z') {
                moduleName[i] = (char)(c - 'A' + 'a');
            }
        }
        if (moduleName[0] == '\0') {
            strncpy(moduleName, "__root", sizeof(moduleName) - 1);
            moduleName[sizeof(moduleName) - 1] = '\0';
        }

        char normalized[96] = {0};
        size_t wi = 0U;
        for (size_t i = 0; moduleName[i] != '\0' && wi + 1U < sizeof(normalized); ++i) {
            const char c = moduleName[i];
            const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/';
            if (!allowed) continue;
            normalized[wi++] = c;
        }
        normalized[wi] = '\0';
        if (normalized[0] == '\0') {
            strncpy(normalized, "__root", sizeof(normalized) - 1);
            normalized[sizeof(normalized) - 1] = '\0';
        }

        const uint32_t digest = fnv1a32_(normalized);
        char assetPath[48] = {0};
        const int n = snprintf(assetPath, sizeof(assetPath), "/wc/m%08lx.j", (unsigned long)digest);
        if (n <= 0 || (size_t)n >= sizeof(assetPath)) {
            request->send(400, "application/json", "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"cfgdoc.module\"}}");
            return;
        }

        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        bool buildBusy = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, assetPath, "application/json", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
        if (response) {
            sendPreparedAssetResponse(request, response, &forensicMeta);
            return;
        }
        if (heapRejected || buildBusy) {
            sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
            return;
        }
        request->send(404, "application/json", "{\"ok\":false,\"err\":{\"code\":\"NotFound\",\"where\":\"cfgdoc.module\"}}");
    });
    server_.on("/api/web/meta", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/web/meta");
        StaticJsonDocument<1024> doc;
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        } else if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
            mode = NetworkAccessMode::Station;
        }
        const char* modeTxt = "none";
        if (mode == NetworkAccessMode::Station) modeTxt = "station";
        else if (mode == NetworkAccessMode::AccessPoint) modeTxt = "ap";
        const char* transportTxt = networkTransport_(mode);

        doc["ok"] = true;
        doc["web_asset_version"] = webAssetVersion_();
        doc["firmware_version"] = FirmwareVersion::Full;
        doc["profile"] = FLOW_BUILD_PROFILE_NAME;
        doc["profile_name"] = FLOW_BUILD_PROFILE_NAME;
        char deviceName[48] = {0};
        loadConfiguredDeviceName_(cfgStore_, deviceName, sizeof(deviceName));
        doc["devicename"] = deviceName;
        doc["network_mode"] = modeTxt;
        doc["network_transport"] = transportTxt;
        doc["is_ap_portal"] = (mode == NetworkAccessMode::AccessPoint);
        doc["provisioning_only"] = provisioningOnly_;
        doc["full_ui_enabled"] = !provisioningOnly_;
        doc["reboot_after_wifi_save"] = provisioningOnly_ || (mode == NetworkAccessMode::AccessPoint);
        char nextionDisplayVersion[9]{};
        if (!hmiSvc_ && services_) {
            hmiSvc_ = services_->get<HmiService>(ServiceId::Hmi);
        }
        const bool nextionVersionDetected =
            hmiSvc_ && hmiSvc_->getDisplayVersion &&
            hmiSvc_->getDisplayVersion(hmiSvc_->ctx, nextionDisplayVersion, sizeof(nextionDisplayVersion));
        const bool nextionVersionCompatible =
            nextionVersionDetected && strcmp(nextionDisplayVersion, TFT_FIRMW) == 0;
        doc["nextion_display_version_detected"] = nextionVersionDetected;
        doc["nextion_display_version_compatible"] = nextionVersionCompatible;
        doc["nextion_display_expected_version"] = TFT_FIRMW;
        if (nextionVersionDetected) {
            doc["nextion_display_version"] = nextionDisplayVersion;
        }
#if defined(FLOW_PROFILE_MICRONOVA)
        doc["local_runtime"] = true;
        doc["local_config_label"] = "Config Store Micronova";
        doc["remote_config_enabled"] = false;
#elif defined(FLOW_PROFILE_WAVESHARE)
        doc["local_runtime"] = true;
        doc["local_config_label"] = "Config Store flow.io";
        doc["remote_config_enabled"] = false;
#else
        doc["local_runtime"] = false;
        doc["local_config_label"] = "Config Store Supervisor";
        doc["remote_config_enabled"] = true;
#endif
        doc["unify_status_card_icons"] = (FLOW_WEB_UNIFY_STATUS_CARD_ICONS != 0);
        SystemStatsSnapshot snap{};
        SystemStats::collect(snap);

        doc["upms"] = snap.uptimeMs;
        JsonObject heap = doc.createNestedObject("heap");
        heap["free"] = snap.heap.freeBytes;
        heap["min_free"] = snap.heap.minFreeBytes;
        heap["largest"] = snap.heap.largestFreeBlock;
        heap["frag"] = snap.heap.fragPercent;
        heap["internal_free"] = snap.heap.internalFreeBytes;
        heap["internal_min_free"] = snap.heap.internalMinFreeBytes;
        heap["internal_largest"] = snap.heap.internalLargestFreeBlock;
        heap["internal_frag"] = snap.heap.internalFragPercent;

        char out[960] = {0};
        const size_t n = serializeJson(doc, out, sizeof(out));
        if (n == 0 || n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"web.meta\"}}");
            return;
        }

        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", out);
        addNoCacheHeaders_(response);
        request->send(response);
    });
    server_.on("/webinterface", HTTP_GET, [this,
                                           spiffsAssetExists,
                                           beginSpiffsAssetResponse,
                                           sendPreparedAssetResponse,
                                           sendRescuePage,
                                           lightUiAssetsAvailable,
                                           fullUiAssetsAvailable,
                                           provisioningUiAssetsAvailable](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/webinterface");
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        } else if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
            mode = NetworkAccessMode::Station;
        }
        const bool useLightUi =
            !request->hasParam("full") &&
            (mode == NetworkAccessMode::AccessPoint
#if defined(FLOW_PROFILE_MICRONOVA)
             || true
#endif
            );
#if defined(FLOW_PROFILE_WAVESHARE)
        if (mode == NetworkAccessMode::AccessPoint && !request->hasParam("full")) {
            if (provisioningUiAssetsAvailable()) {
                SpiffsAssetForensicMeta forensicMeta{};
                bool heapRejected = false;
                bool buildBusy = false;
                AsyncWebServerResponse* response =
                    beginSpiffsAssetResponse(
                        request, "/webinterface/prov.html", "text/html", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
                if (!response) {
                    if (heapRejected || buildBusy) {
                        sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                        return;
                    }
                    request->send(500, "text/plain", "Failed to load provisioning web interface");
                    return;
                }
                sendPreparedAssetResponse(request, response, &forensicMeta);
                return;
            }
            LOGW("Provisioning web assets missing; serving PROGMEM rescue UI");
            sendRescuePage(request);
            return;
        }
#endif
        if (useLightUi) {
            if (lightUiAssetsAvailable()) {
                SpiffsAssetForensicMeta forensicMeta{};
                bool heapRejected = false;
                bool buildBusy = false;
                AsyncWebServerResponse* response =
                    beginSpiffsAssetResponse(
                        request, "/webinterface/light.html", "text/html", true, nullptr, &forensicMeta, &heapRejected, &buildBusy);
                if (!response) {
                    if (heapRejected || buildBusy) {
                        sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                        return;
                    }
                    request->send(500, "text/plain", "Failed to load light web interface");
                    return;
                }
                sendPreparedAssetResponse(request, response, &forensicMeta);
                return;
            }
            LOGW("Light web assets missing; serving PROGMEM rescue UI");
            sendRescuePage(request);
            return;
        }
        if (!request->hasParam("page")) {
            if (mode == NetworkAccessMode::AccessPoint) {
                if (request->hasParam("full")) {
                    request->redirect("/webinterface?full=1&page=page-wifi");
                    return;
                }
                request->redirect("/webinterface?page=page-wifi");
                return;
            }
        }
        if (fullUiAssetsAvailable()) {
            SpiffsAssetForensicMeta forensicMeta{};
            bool heapRejected = false;
            bool buildBusy = false;
            AsyncWebServerResponse* response =
                beginSpiffsAssetResponse(
                    request, "/webinterface/index.html", "text/html", false, nullptr, &forensicMeta, &heapRejected, &buildBusy);
            if (!response) {
                if (heapRejected || buildBusy) {
                    sendTinyBusyJson_(request, heapRejected ? "low_memory" : "asset_build_busy");
                    return;
                }
                request->send(500, "text/plain", "Failed to load web interface");
                return;
            }
            sendPreparedAssetResponse(request, response, &forensicMeta);
            return;
        }
        LOGW("Full web assets missing; serving PROGMEM rescue UI");
        sendRescuePage(request);
    });
    server_.on("/webinterface/", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) { request->redirect("/webinterface/favicon.png"); });
    server_.on("/webserial", HTTP_GET, [this](AsyncWebServerRequest* request) {
        noteHttpActivity_();
        AsyncWebServerResponse* response = request->beginResponse(200, "text/html", kWebSerialLogPage);
        addNoCacheHeaders_(response);
        request->send(response);
    });

    server_.on("/webinterface/health", HTTP_GET, [this](AsyncWebServerRequest* request) {
        noteHttpActivity_();
        request->send(200, "text/plain", "ok");
    });
    server_.on("/webserial/health", HTTP_GET, [this](AsyncWebServerRequest* request) {
        noteHttpActivity_();
        request->redirect("/webinterface/health");
    });
    server_.on("/api/network/mode", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/network/mode");
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        } else if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
            mode = NetworkAccessMode::Station;
        }

        const char* modeTxt = "none";
        if (mode == NetworkAccessMode::Station) modeTxt = "station";
        else if (mode == NetworkAccessMode::AccessPoint) modeTxt = "ap";
        const char* transportTxt = networkTransport_(mode);

        char ip[16] = {0};
        (void)getNetworkIp_(ip, sizeof(ip), nullptr);

        char out[128] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"mode\":\"%s\",\"transport\":\"%s\",\"ip\":\"%s\"}",
                               modeTxt,
                               transportTxt,
                               ip);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"network.mode\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });
    server_.on("/generate_204", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/gen_204", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/hotspot-detect.html", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/connecttest.txt", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/ncsi.txt", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });

    auto fwStatusHandler = [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/status");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->statusJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.status\"}}");
            return;
        }

        char out[320] = {0};
        if (!fwUpdateSvc_->statusJson(fwUpdateSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.status\"}}");
            return;
        }
        request->send(200, "application/json", out);
    };
    server_.on("/fwupdate/status", HTTP_GET, fwStatusHandler);
    server_.on("/api/fwupdate/status", HTTP_GET, fwStatusHandler);

    server_.on("/api/fwupdate/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/config");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->configJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.config\"}}");
            return;
        }

        char out[512] = {0};
        if (!fwUpdateSvc_->configJson(fwUpdateSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.config\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/fwupdate/check", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/check");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->startManifestCheck) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.check\"}}");
            return;
        }

        char err[128] = {0};
        uint32_t requestId = 0U;
        if (!fwUpdateSvc_->startManifestCheck(fwUpdateSvc_->ctx,
                                              &requestId,
                                              err,
                                              sizeof(err))) {
            sanitizeJsonString_(err);
            char msg[320] = {0};
            const int n = snprintf(msg,
                                   sizeof(msg),
                                   "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.check\",\"msg\":\"%s\"}}",
                                   err[0] ? err : "failed");
            const int status =
                strstr(err, "storage unavailable") ? 503 : 409;
            request->send(status,
                          "application/json",
                          (n > 0 && (size_t)n < sizeof(msg))
                              ? msg
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.check\"}}");
            return;
        }

        char out[128] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"request_id\":%lu,\"state\":\"queued\"}",
                               (unsigned long)requestId);
        request->send((n > 0 && (size_t)n < sizeof(out)) ? 202 : 500,
                      "application/json",
                      (n > 0 && (size_t)n < sizeof(out))
                          ? out
                          : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.check\"}}");
    });

    server_.on("/api/fwupdate/check", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/check");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->manifestCheckStatus ||
            !fwUpdateSvc_->copyManifestResult) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.check\"}}");
            return;
        }

        const int32_t requestIdRaw = requestIntParam_(request, "request_id", 0);
        if (requestIdRaw <= 0) {
            request->send(400,
                          "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"fwupdate.check.request_id\"}}");
            return;
        }

        FirmwareManifestCheckSnapshot snapshot{};
        const uint32_t requestId = (uint32_t)requestIdRaw;
        if (!fwUpdateSvc_->manifestCheckStatus(fwUpdateSvc_->ctx,
                                               requestId,
                                               &snapshot)) {
            request->send(404,
                          "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotFound\",\"where\":\"fwupdate.check.request_id\"}}");
            return;
        }

        if (snapshot.state == FirmwareManifestCheckState::Queued ||
            snapshot.state == FirmwareManifestCheckState::Downloading) {
            char out[160] = {0};
            const int n = snprintf(out,
                                   sizeof(out),
                                   "{\"ok\":true,\"request_id\":%lu,\"state\":\"%s\",\"ts_ms\":%lu}",
                                   (unsigned long)snapshot.requestId,
                                   firmwareManifestCheckStateName_(snapshot.state),
                                   (unsigned long)snapshot.updatedAtMs);
            request->send((n > 0 && (size_t)n < sizeof(out)) ? 202 : 500,
                          "application/json",
                          (n > 0 && (size_t)n < sizeof(out))
                              ? out
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.check\"}}");
            return;
        }

        if (snapshot.state == FirmwareManifestCheckState::Error) {
            sanitizeJsonString_(snapshot.message);
            char out[320] = {0};
            const int n = snprintf(out,
                                   sizeof(out),
                                   "{\"ok\":false,\"request_id\":%lu,\"state\":\"error\","
                                   "\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.check\",\"msg\":\"%s\"}}",
                                   (unsigned long)snapshot.requestId,
                                   snapshot.message[0] ? snapshot.message : "failed");
            request->send((n > 0 && (size_t)n < sizeof(out)) ? 502 : 500,
                          "application/json",
                          (n > 0 && (size_t)n < sizeof(out))
                              ? out
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.check\"}}");
            return;
        }

        if (snapshot.state != FirmwareManifestCheckState::Ready) {
            request->send(409,
                          "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidState\",\"where\":\"fwupdate.check\"}}");
            return;
        }

        auto state = std::make_shared<FirmwareManifestResponseChunkState>();
        if (!state || !state->begin(fwUpdateSvc_, snapshot)) {
            request->send(503,
                          "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"LowMemory\",\"where\":\"fwupdate.check.response\"}}");
            return;
        }
        AsyncWebServerResponse* response =
            request->beginChunkedResponse("application/json",
                                          [state](uint8_t* buffer, size_t maxLen, size_t) -> size_t {
                                              return state->fill(buffer, maxLen);
                                          });
        addNoCacheHeaders_(response);
        request->send(response);
    });

    server_.on("/api/fwupdate/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/config");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->setConfig) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.set_config\"}}");
            return;
        }

        char hostStr[192] = {0};
        char updatePathStr[192] = {0};
        const bool hasHost = copyRequestParamValue_(request, "update_host", true, hostStr, sizeof(hostStr), "");
        const bool hasUpdatePath =
            copyRequestParamValue_(request, "update_path", true, updatePathStr, sizeof(updatePathStr), "");

        char err[96] = {0};
        if (!fwUpdateSvc_->setConfig(fwUpdateSvc_->ctx,
                                     hasHost ? hostStr : nullptr,
                                     hasUpdatePath ? updatePathStr : nullptr,
                                     err,
                                     sizeof(err))) {
            sanitizeJsonString_(err);
            char out[288] = {0};
            const int n = snprintf(out,
                                   sizeof(out),
                                   "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.set_config\",\"msg\":\"%s\"}}",
                                   err[0] ? err : "failed");
            request->send(409,
                          "application/json",
                          (n > 0 && (size_t)n < sizeof(out))
                              ? out
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.set_config\"}}");
            return;
        }

        emitConfigActivity_("Mise à jour", "fwupdate", (uint16_t)((hasHost ? 1U : 0U) + (hasUpdatePath ? 1U : 0U)));
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/wifi/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.config.get\"}}");
            return;
        }

        char wifiJson[320] = {0};
        if (!cfgStore_->toJsonModule("wifi", wifiJson, sizeof(wifiJson), nullptr, false)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.get\"}}");
            return;
        }

        StaticJsonDocument<320> doc;
        const DeserializationError err = deserializeJson(doc, wifiJson);
        if (err || !doc.is<JsonObjectConst>()) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidData\",\"where\":\"wifi.config.get\"}}");
            return;
        }

        JsonObjectConst root = doc.as<JsonObjectConst>();
        bool enabled = root["enabled"] | true;
        const char* ssid = root["ssid"] | "";
        const char* pass = root["pass"] | "";

        char ssidSafe[96] = {0};
        char passSafe[96] = {0};
        snprintf(ssidSafe, sizeof(ssidSafe), "%s", ssid ? ssid : "");
        snprintf(passSafe, sizeof(passSafe), "%s", pass ? pass : "");
        sanitizeJsonString_(ssidSafe);
        sanitizeJsonString_(passSafe);

        char out[360] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"enabled\":%s,\"ssid\":\"%s\",\"pass\":\"%s\"}",
                               enabled ? "true" : "false",
                               ssidSafe,
                               passSafe);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.get\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/wifi/ap", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/ap");

        NetworkAccessMode mode = NetworkAccessMode::None;
        char ip[24] = {0};
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        }
        if (netAccessSvc_ && netAccessSvc_->getIP) {
            (void)netAccessSvc_->getIP(netAccessSvc_->ctx, ip, sizeof(ip));
        }
        if (ip[0] == '\0' && mode == NetworkAccessMode::AccessPoint) {
            const IPAddress apIp = WiFi.softAPIP();
            snprintf(ip, sizeof(ip), "%u.%u.%u.%u", apIp[0], apIp[1], apIp[2], apIp[3]);
        }

        char ssid[48] = {0};
        buildProvisioningApSsid_(ssid, sizeof(ssid));
        sanitizeJsonString_(ssid);
        sanitizeJsonString_(ip);

        char out[256] = {0};
        const char* modeTxt = "none";
        if (mode == NetworkAccessMode::Station) modeTxt = "sta";
        else if (mode == NetworkAccessMode::AccessPoint) modeTxt = "ap";
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"active\":%s,\"mode\":\"%s\",\"ssid\":\"%s\",\"pass\":\"flowio1234\",\"ip\":\"%s\",\"clients\":%u}",
                               mode == NetworkAccessMode::AccessPoint ? "true" : "false",
                               modeTxt,
                               ssid,
                               ip,
                               (unsigned)WiFi.softAPgetStationNum());
        request->send(200,
                      "application/json",
                      (n > 0 && (size_t)n < sizeof(out))
                          ? out
                          : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.ap\"}}");
    });

    server_.on("/api/mqtt/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/mqtt/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"mqtt.config.get\"}}");
            return;
        }

        bool enabled = false;
        int32_t port = Limits::Mqtt::Defaults::Port;
        char host[96] = {0};
        char user[64] = {0};
        char pass[64] = {0};
        char baseTopic[48] = "flowio";
        char topicDeviceId[48] = {0};
        char deviceName[48] = {0};

        char mqttJson[640] = {0};
        if (cfgStore_->toJsonModule("mqtt", mqttJson, sizeof(mqttJson), nullptr, false)) {
            StaticJsonDocument<640> doc;
            const DeserializationError err = deserializeJson(doc, mqttJson);
            if (err || !doc.is<JsonObjectConst>()) {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"InvalidData\",\"where\":\"mqtt.config.get\"}}");
                return;
            }

            JsonObjectConst root = doc.as<JsonObjectConst>();
            enabled = root["enabled"] | false;
            port = root["port"] | Limits::Mqtt::Defaults::Port;
            snprintf(host, sizeof(host), "%s", root["host"] | "");
            snprintf(user, sizeof(user), "%s", root["user"] | "");
            snprintf(pass, sizeof(pass), "%s", root["pass"] | "");
            snprintf(baseTopic, sizeof(baseTopic), "%s", root["baseTopic"] | "flowio");
            snprintf(topicDeviceId, sizeof(topicDeviceId), "%s", root["topicDeviceId"] | "");
            snprintf(deviceName, sizeof(deviceName), "%s", root["deviceName"] | "");
        } else {
            LOGW("mqtt.config.get: module unavailable, returning defaults");
        }

        sanitizeJsonString_(host);
        sanitizeJsonString_(user);
        sanitizeJsonString_(pass);
        sanitizeJsonString_(baseTopic);
        sanitizeJsonString_(topicDeviceId);
        sanitizeJsonString_(deviceName);

        char out[640] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"enabled\":%s,\"host\":\"%s\",\"port\":%ld,"
                               "\"user\":\"%s\",\"pass\":\"%s\",\"baseTopic\":\"%s\","
                               "\"topicDeviceId\":\"%s\",\"deviceName\":\"%s\"}",
                               enabled ? "true" : "false",
                               host,
                               (long)port,
                               user,
                               pass,
                               baseTopic,
                               topicDeviceId,
                               deviceName);
        request->send(200,
                      "application/json",
                      (n > 0 && (size_t)n < sizeof(out))
                          ? out
                          : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"mqtt.config.get\"}}");
    });

    server_.on("/api/wifi/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.config.set\"}}");
            return;
        }

        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        const bool wasApProvisioning = netAccessSvc_ &&
                                       netAccessSvc_->mode &&
                                       (netAccessSvc_->mode(netAccessSvc_->ctx) == NetworkAccessMode::AccessPoint);

        char enabledStr[8] = {0};
        char ssid[96] = {0};
        char pass[96] = {0};
        copyRequestParamValue_(request, "enabled", true, enabledStr, sizeof(enabledStr), "1");
        const bool enabled = parseBoolParam_(enabledStr, true);
        copyRequestParamValue_(request, "ssid", true, ssid, sizeof(ssid), "");
        copyRequestParamValue_(request, "pass", true, pass, sizeof(pass), "");

        StaticJsonDocument<320> patch;
        JsonObject root = patch.to<JsonObject>();
        JsonObject wifi = root.createNestedObject("wifi");
        wifi["enabled"] = enabled;
        wifi["ssid"] = ssid;
        wifi["pass"] = pass;

        char patchJson[320] = {0};
        if (serializeJson(patch, patchJson, sizeof(patchJson)) == 0) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.set\"}}");
            return;
        }

        if (!cfgStore_->applyJson(patchJson)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.set\"}}");
            return;
        }
        emitConfigPatchActivity_("WiFi", patchJson);

        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->notifyWifiConfigChanged) {
            netAccessSvc_->notifyWifiConfigChanged(netAccessSvc_->ctx);
        }

        bool flowSyncAttempted = false;
        bool flowSyncOk = false;
        char flowSyncErr[96] = {0};
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (flowCfgSvc_ && flowCfgSvc_->applyPatchJson) {
            flowSyncAttempted = true;

            StaticJsonDocument<320> flowPatchDoc;
            JsonObject flowRoot = flowPatchDoc.to<JsonObject>();
            JsonObject flowWifi = flowRoot.createNestedObject("wifi");
            flowWifi["enabled"] = enabled;
            flowWifi["ssid"] = ssid;
            flowWifi["pass"] = pass;

            char flowPatchJson[320] = {0};
            const size_t flowPatchLen = serializeJson(flowPatchDoc, flowPatchJson, sizeof(flowPatchJson));
            if (flowPatchLen > 0 && flowPatchLen < sizeof(flowPatchJson)) {
                char flowAck[Limits::Mqtt::Buffers::Ack] = {0};
                flowSyncOk = flowCfgSvc_->applyPatchJson(flowCfgSvc_->ctx, flowPatchJson, flowAck, sizeof(flowAck));
                if (!flowSyncOk) {
                    snprintf(flowSyncErr, sizeof(flowSyncErr), "flowcfg.apply failed");
                }
            } else {
                snprintf(flowSyncErr, sizeof(flowSyncErr), "flowcfg.patch serialize failed");
            }
        }

        if (flowSyncAttempted && flowSyncOk) {
            LOGI("WiFi config synced to flow.io");
        } else if (flowSyncAttempted) {
            LOGW("WiFi config sync to flow.io skipped/failed attempted=%d err=%s",
                 (int)flowSyncAttempted,
                 flowSyncErr[0] ? flowSyncErr : "none");
        }

        bool flowRebootAttempted = false;
        bool flowRebootOk = false;
        char flowRebootErr[96] = {0};
        if (wasApProvisioning && flowSyncAttempted && flowSyncOk) {
            flowRebootAttempted = true;
            if (!cmdSvc_ && services_) {
                cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
            }
            if (cmdSvc_ && cmdSvc_->execute) {
                char rebootReply[220] = {0};
                flowRebootOk = cmdSvc_->execute(cmdSvc_->ctx,
                                                "flow.system.reboot",
                                                "{}",
                                                nullptr,
                                                rebootReply,
                                                sizeof(rebootReply));
                if (!flowRebootOk) {
                    snprintf(flowRebootErr, sizeof(flowRebootErr), "flow.system.reboot failed");
                }
            } else {
                snprintf(flowRebootErr, sizeof(flowRebootErr), "command service unavailable");
            }
        }

        if (flowRebootAttempted && flowRebootOk) {
            LOGI("flow.io reboot requested after AP WiFi provisioning");
        } else if (flowRebootAttempted) {
            LOGW("flow.io reboot request failed err=%s", flowRebootErr[0] ? flowRebootErr : "unknown");
        }

#if defined(FLOW_PROFILE_WAVESHARE)
        if (wasApProvisioning) {
            scheduleReboot_(1200U, "prov.done.wifi");
            request->send(200, "application/json", "{\"ok\":true,\"reboot_scheduled\":true}");
            return;
        }
#endif

        char out[384] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,"
                               "\"flowio_sync\":{\"attempted\":%s,\"ok\":%s,\"err\":\"%s\"},"
                               "\"flowio_reboot\":{\"attempted\":%s,\"ok\":%s,\"err\":\"%s\"}}",
                               flowSyncAttempted ? "true" : "false",
                               flowSyncOk ? "true" : "false",
                               flowSyncErr,
                               flowRebootAttempted ? "true" : "false",
                               flowRebootOk ? "true" : "false",
                               flowRebootErr);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(200, "application/json", "{\"ok\":true}");
            return;
        }
        const bool provisioningConfigured =
            provisioningOnly_ &&
            provisioningDisableAfterConfigured_ &&
            isProvisioningConfigured_(cfgStore_, provisioningRequireMqttForConfigured_);
        if (provisioningConfigured) {
            scheduleReboot_(1200U, "prov.done.wifi");
            request->send(200, "application/json", "{\"ok\":true,\"reboot_scheduled\":true}");
            return;
        }

        request->send(200, "application/json", out);
    });

    server_.on("/api/mqtt/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/mqtt/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"mqtt.config.set\"}}");
            return;
        }

        char enabledStr[8] = {0};
        char host[96] = {0};
        char portStr[12] = {0};
        char user[64] = {0};
        char pass[64] = {0};
        char baseTopic[48] = {0};
        char topicDeviceId[48] = {0};
        char deviceName[48] = {0};
        copyRequestParamValue_(request, "enabled", true, enabledStr, sizeof(enabledStr), "0");
        copyRequestParamValue_(request, "host", true, host, sizeof(host), "");
        copyRequestParamValue_(request, "port", true, portStr, sizeof(portStr), "1883");
        copyRequestParamValue_(request, "user", true, user, sizeof(user), "");
        copyRequestParamValue_(request, "pass", true, pass, sizeof(pass), "");
        copyRequestParamValue_(request, "baseTopic", true, baseTopic, sizeof(baseTopic), "flowio");
        copyRequestParamValue_(request, "topicDeviceId", true, topicDeviceId, sizeof(topicDeviceId), "");
        copyRequestParamValue_(request, "deviceName", true, deviceName, sizeof(deviceName), "");

        const bool enabled = parseBoolParam_(enabledStr, false);
        int32_t port = (int32_t)atoi(portStr);
        if (port <= 0 || port > 65535) port = Limits::Mqtt::Defaults::Port;
        sanitizeJsonString_(host);
        sanitizeJsonString_(user);
        sanitizeJsonString_(pass);
        sanitizeJsonString_(baseTopic);
        sanitizeJsonString_(topicDeviceId);
        sanitizeJsonString_(deviceName);

        char patchJson[640] = {0};
        const int n = snprintf(patchJson,
                               sizeof(patchJson),
                               "{\"mqtt\":{\"enabled\":%s,\"host\":\"%s\",\"port\":%ld,"
                               "\"user\":\"%s\",\"pass\":\"%s\",\"baseTopic\":\"%s\","
                               "\"topicDeviceId\":\"%s\",\"deviceName\":\"%s\"}}",
                               enabled ? "true" : "false",
                               host,
                               (long)port,
                               user,
                               pass,
                               baseTopic,
                               topicDeviceId,
                               deviceName);
        if (n <= 0 || (size_t)n >= sizeof(patchJson)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"ArgsTooLarge\",\"where\":\"mqtt.config.set\"}}");
            return;
        }

        if (!cfgStore_->applyJson(patchJson)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"mqtt.config.set\"}}");
            return;
        }
        emitConfigPatchActivity_("MQTT", patchJson);
        const bool provisioningConfigured =
            provisioningOnly_ &&
            provisioningDisableAfterConfigured_ &&
            isProvisioningConfigured_(cfgStore_, provisioningRequireMqttForConfigured_);
        if (provisioningConfigured) {
            scheduleReboot_(1200U, "prov.done.mqtt");
            request->send(200, "application/json", "{\"ok\":true,\"reboot_scheduled\":true}");
            return;
        }

        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/scan");
        if (!wifiSvc_ && services_) {
            wifiSvc_ = services_->get<WifiService>(ServiceId::Wifi);
        }
        if (!wifiSvc_ || !wifiSvc_->scanStatusJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.scan.get\"}}");
            return;
        }

        char out[Limits::Wifi::Buffers::ScanStatusJson] = {0};
        if (!wifiSvc_->scanStatusJson(wifiSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.scan.get\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/wifi/scan", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/scan");
        if (!wifiSvc_ && services_) {
            wifiSvc_ = services_->get<WifiService>(ServiceId::Wifi);
        }
        if (!wifiSvc_ || !wifiSvc_->requestScan) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.scan.start\"}}");
            return;
        }

        char forceStr[8] = {0};
        copyRequestParamValue_(request, "force", true, forceStr, sizeof(forceStr), "1");
        const bool force = parseBoolParam_(forceStr, true);
        if (!wifiSvc_->requestScan(wifiSvc_->ctx, force)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.scan.start\"}}");
            return;
        }

        request->send(202, "application/json", "{\"ok\":true,\"accepted\":true}");
    });

    server_.on("/api/flow/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flow/status",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        char srcStr[24] = {0};
        copyRequestParamValue_(request, "src", false, srcStr, sizeof(srcStr), "");
        LOGD("runtime.call route=/api/flow/status src=%s", srcStr[0] ? srcStr : "-");
#if defined(FLOW_PROFILE_WAVESHARE)
        const AlarmService* alarmSvc = services_ ? services_->get<AlarmService>(ServiceId::Alarm) : nullptr;
        if (!sendWaveshareStatusCompactResponse_(request, dataStore_, cfgStore_, alarmSvc)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.status.local\"}}");
        }
        return;
#else
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->runtimeStatusDomainJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status.link\"}}");
            return;
        }

        (void)sendFlowStatusCompactResponse_(request, flowCfgSvc_);
#endif
    });

    server_.on("/api/flow/status/domain", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flow/status/domain",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        char srcStr[24] = {0};
        copyRequestParamValue_(request, "src", false, srcStr, sizeof(srcStr), "");
        LOGD("runtime.call route=/api/flow/status/domain src=%s", srcStr[0] ? srcStr : "-");
        if (!request->hasParam("d")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"flow.status.domain\"}}");
            return;
        }
        FlowStatusDomain domain = FlowStatusDomain::System;
        char domainStr[16] = {0};
        copyRequestParamValue_(request, "d", false, domainStr, sizeof(domainStr), "");
        LOGD("runtime.call route=/api/flow/status/domain src=%s domain=%s",
             srcStr[0] ? srcStr : "-",
             domainStr);
        if (!parseFlowStatusDomainParam_(domainStr, domain)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadDomain\",\"where\":\"flow.status.domain\"}}");
            return;
        }

#if defined(FLOW_PROFILE_WAVESHARE)
        char domainBuf[768] = {0};
        const AlarmService* alarmSvc = services_ ? services_->get<AlarmService>(ServiceId::Alarm) : nullptr;
        if (!waveshareBuildStatusDomainJson_(domain, dataStore_, cfgStore_, alarmSvc, domainBuf, sizeof(domainBuf))) {
            request->send(200, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.status.domain.local\"}}");
            return;
        }
        LOGD("runtime.call route=/api/flow/status/domain src=%s domain=%s result=ok",
             srcStr[0] ? srcStr : "-",
             domainStr);
        request->send(200, "application/json", domainBuf);
        return;
#else
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->runtimeStatusDomainJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status.domain\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status.domain.link\"}}");
            return;
        }

        char domainBuf[640] = {0};
        if (!flowCfgSvc_->runtimeStatusDomainJson(flowCfgSvc_->ctx, domain, domainBuf, sizeof(domainBuf))) {
            LOGD("runtime.call route=/api/flow/status/domain src=%s domain=%s result=call_fail",
                 srcStr[0] ? srcStr : "-",
                 domainStr);
            if (domainBuf[0] != '\0') {
                // Domain can be temporarily unavailable (or unsupported remotely).
                // Return structured JSON without HTTP error to avoid noisy client console
                // and let the UI render the domain as unavailable.
                request->send(200, "application/json", domainBuf);
            } else {
                request->send(200, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.status.domain.fetch\"}}");
            }
            return;
        }

        LOGD("runtime.call route=/api/flow/status/domain src=%s domain=%s result=ok",
             srcStr[0] ? srcStr : "-",
             domainStr);
        request->send(200, "application/json", domainBuf);
#endif
    });

    server_.on("/api/runtime/manifest", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/runtime/manifest");
        LOGD("runtime.call route=/api/runtime/manifest");
#if defined(FLOW_PROFILE_MICRONOVA)
        AsyncWebServerResponse* response =
            request->beginResponse(200,
                                   "application/json",
                                   reinterpret_cast<const uint8_t*>(kMicronovaRuntimeManifestJson),
                                   sizeof(kMicronovaRuntimeManifestJson) - 1U);
        addNoCacheHeaders_(response);
        request->send(response);
#else
        AsyncWebServerResponse* response =
            request->beginResponse(200,
                                   "application/json",
                                   reinterpret_cast<const uint8_t*>(kRuntimeUiManifestJson),
                                   sizeof(kRuntimeUiManifestJson) - 1U);
        addNoCacheHeaders_(response);
        request->send(response);
#endif
    });

    server_.on("/api/runtime/alarms", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/runtime/alarms",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        LOGD("runtime.call route=/api/runtime/alarms");
        request->send(503, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"Disabled\",\"where\":\"runtime.alarms.disabled\"}}");
    });

    server_.on("/api/io/topology", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/io/topology");
        LOGD("runtime.call route=/api/io/topology");
#if defined(FLOW_PROFILE_WAVESHARE)
        sendIoResponseCache_(request, true);
#else
        request->send(404, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"Unsupported\",\"where\":\"io.topology\"}}");
#endif
    });

    server_.on("/api/io/runtime", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/io/runtime");
        LOGD("runtime.call route=/api/io/runtime");
#if defined(FLOW_PROFILE_WAVESHARE)
        sendIoResponseCache_(request, false);
#else
        request->send(404, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"Unsupported\",\"where\":\"io.runtime\"}}");
#endif
    });

    server_.on("/api/io/summary", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(410,
                      "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"Replaced\",\"where\":\"io.summary\","
                      "\"detail\":\"Use /api/io/topology and /api/io/runtime\"}}");
    });

    server_.on("/api/runtime/dashboard_slots", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/runtime/dashboard_slots");
        LOGD("runtime.call route=/api/runtime/dashboard_slots");
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"slots\":[");
        bool first = true;
#if defined(FLOW_PROFILE_WAVESHARE)
        if (!ioSvc_ && services_) {
            ioSvc_ = services_->get<IOServiceV2>(ServiceId::Io);
        }
        {
            const AlarmService* alarmSvc = services_ ? services_->get<AlarmService>(ServiceId::Alarm) : nullptr;
            sendWaveshareDashboardSlotsResponse_(*response, first, dataStore_, cfgStore_, alarmSvc, ioSvc_);
        }
#elif !defined(FLOW_PROFILE_MICRONOVA)
        if (dataStore_) {
            const FlowRemoteRuntimeData& flow = flowRemoteRuntime(*dataStore_);
            for (uint8_t i = 0U; i < kFlowRemoteDashboardSlotCount; ++i) {
                const FlowRemoteDashboardSlotRuntime& slot = flow.dashboardSlots[i];
                if (!slot.enabled) continue;

                char valueBuf[40] = {0};
                char bgColorBuf[8] = {0};
                formatDashboardSlotValueText_(slot, valueBuf, sizeof(valueBuf));
                dashboardSlotBgColorHex_(slot.bgColor565, bgColorBuf, sizeof(bgColorBuf));
                if (!first) response->print(',');
                response->print("{\"slot\":");
                response->print((unsigned)i);
                response->print(",\"runtime_ui_id\":");
                response->print((unsigned long)slot.runtimeUiId);
                const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(slot.runtimeUiId);
                const char* unit = (item && item->unit) ? item->unit : "";
                response->print(",\"label\":");
                printJsonEscaped_(*response, slot.label[0] != '\0' ? slot.label : "Mesure");
                response->print(",\"value\":");
                printJsonEscaped_(*response, valueBuf);
                response->print(",\"unit\":");
                if (unit && unit[0] != '\0') {
                    printJsonEscaped_(*response, dashboardSlotDegreeCUnit_(unit) ? "\xC2\xB0""C" : unit);
                } else {
                    printJsonEscaped_(*response, "");
                }
                response->print(",\"bg_color\":");
                printJsonEscaped_(*response, bgColorBuf);
                response->print(",\"available\":");
                response->print(slot.available ? "true" : "false");
                response->print("}");
                first = false;
            }
        }
#else
        (void)first;
#endif
        response->print("],\"alarm_slots\":[");
        bool firstAlarmSlot = true;
#if defined(FLOW_PROFILE_WAVESHARE)
        {
            const AlarmService* alarmSvc = services_ ? services_->get<AlarmService>(ServiceId::Alarm) : nullptr;
            sendWaveshareAlarmDashboardSlotsResponse_(*response, firstAlarmSlot, cfgStore_, alarmSvc);
        }
#else
        (void)firstAlarmSlot;
#endif
        response->print("]}");
        request->send(response);
    });

    server_.on("/api/runtime/values", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/runtime/values",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        LOGD("runtime.call route=/api/runtime/values method=GET");
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!request->hasParam("ids")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
            return;
        }

        RuntimeUiId ids[kMaxRuntimeHttpIds] = {};
        size_t idCount = 0U;
        char idsCsv[768] = {0};
        const bool hasIds = copyRequestParamValue_(request, "ids", false, idsCsv, sizeof(idsCsv), "");
        if (!hasIds || !parseRuntimeUiIdsCsv_(idsCsv, ids, kMaxRuntimeHttpIds, idCount)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
            return;
        }
        LOGD("runtime.call route=/api/runtime/values method=GET ids=%u", (unsigned)idCount);

#if defined(FLOW_PROFILE_MICRONOVA)
        sendMicronovaLocalRuntimeValuesResponse_(request, dataStore_, ids, idCount);
#elif defined(FLOW_PROFILE_WAVESHARE)
        {
            const AlarmService* alarmSvc = services_ ? services_->get<AlarmService>(ServiceId::Alarm) : nullptr;
            sendWaveshareLocalRuntimeValuesResponse_(request, dataStore_, cfgStore_, alarmSvc, ids, idCount);
        }
#else
        sendRuntimeUiValuesResponse_(request, flowCfgSvc_, ids, idCount);
#endif
    });

    server_.on(
        "/api/runtime/values",
        HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            HttpLatencyScope latency(request,
                                     "/api/runtime/values",
                                     kHttpLatencyFlowCfgInfoMs,
                                     kHttpLatencyFlowCfgWarnMs);
            LOGD("runtime.call route=/api/runtime/values method=POST");
            if (request->_tempObject == reinterpret_cast<void*>(1)) {
                request->_tempObject = nullptr;
                return;
            }
            if (!flowCfgSvc_ && services_) {
                flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
            }
            if (!request->_tempObject || request->_tempObject != runtimeValuesBodyScratch_) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.body\"}}");
                return;
            }

            char* body = static_cast<char*>(request->_tempObject);
            request->_tempObject = nullptr;

            StaticJsonDocument<kRuntimeValuesJsonDocCapacity> reqDoc;
            const DeserializationError reqErr = deserializeJson(reqDoc, body);
            releaseRuntimeValuesBodyScratch_();
            if (reqErr) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.json\"}}");
                return;
            }

            JsonArrayConst idsIn = reqDoc["ids"].as<JsonArrayConst>();
            if (idsIn.isNull()) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
                return;
            }

            RuntimeUiId ids[kMaxRuntimeHttpIds] = {};
            size_t idCount = 0U;
            for (JsonVariantConst item : idsIn) {
                if (!item.is<uint32_t>()) continue;
                if (idCount >= kMaxRuntimeHttpIds) break;
                const uint32_t raw = item.as<uint32_t>();
                if (raw == 0U || raw > 65535U) continue;
                ids[idCount++] = (RuntimeUiId)raw;
            }
            if (idCount == 0U) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
                return;
            }
            LOGD("runtime.call route=/api/runtime/values method=POST ids=%u", (unsigned)idCount);

#if defined(FLOW_PROFILE_MICRONOVA)
            sendMicronovaLocalRuntimeValuesResponse_(request, dataStore_, ids, idCount);
#elif defined(FLOW_PROFILE_WAVESHARE)
            {
                const AlarmService* alarmSvc = services_ ? services_->get<AlarmService>(ServiceId::Alarm) : nullptr;
                sendWaveshareLocalRuntimeValuesResponse_(request, dataStore_, cfgStore_, alarmSvc, ids, idCount);
            }
#else
            sendRuntimeUiValuesResponse_(request, flowCfgSvc_, ids, idCount);
#endif
        },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0U) {
                if (total == 0U || total > kRuntimeValuesBodyMax) {
                    request->_tempObject = reinterpret_cast<void*>(1);
                    request->send(413, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"ArgsTooLarge\",\"where\":\"runtime.values.body\"}}");
                    return;
                }
                if (!acquireRuntimeValuesBodyScratch_()) {
                    request->_tempObject = reinterpret_cast<void*>(1);
                    request->send(503, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"Busy\",\"where\":\"runtime.values.body\"}}");
                    return;
                }
                if (!runtimeValuesBodyScratch_) {
                    request->_tempObject = reinterpret_cast<void*>(1);
                    request->send(500, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"NoMemory\",\"where\":\"runtime.values.body\"}}");
                    return;
                }
                request->_tempObject = runtimeValuesBodyScratch_;
            }

            if (request->_tempObject == reinterpret_cast<void*>(1)) return;
            char* body = static_cast<char*>(request->_tempObject);
            if (!body) return;
            memcpy(body + index, data, len);
            if ((index + len) < total) return;
            body[total] = '\0';
        });

    server_.on("/api/flowcfg/modules", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/modules",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
#if defined(FLOW_PROFILE_WAVESHARE)
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.modules\"}}");
            return;
        }

        constexpr uint8_t kMaxModules = Limits::Config::Capacity::ModuleListMax;
        const char* modules[kMaxModules] = {0};
        const uint8_t moduleCount = cfgStore_->listModules(modules, kMaxModules);

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"modules\":[");
        bool first = true;
        for (uint8_t i = 0; i < moduleCount; ++i) {
            if (!modules[i] || modules[i][0] == '\0') continue;
            if (!first) response->print(',');
            printJsonEscaped_(*response, modules[i]);
            first = false;
        }
        response->print("]}");
        request->send(response);
        return;
#else
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->listModulesJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.modules\"}}");
            return;
        }
        char out[Limits::Mqtt::Buffers::Ack] = {0};
        if (!flowCfgSvc_->listModulesJson(flowCfgSvc_->ctx, out, sizeof(out))) {
            if (out[0] != '\0') {
                LOGW("flowcfg.modules failed details=%s", out);
                request->send(500, "application/json", out);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.modules\"}}");
            }
            return;
        }

        request->send(200, "application/json", out);
#endif
    });

    server_.on("/api/flowcfg/children", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/children",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
#if defined(FLOW_PROFILE_WAVESHARE)
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.children\"}}");
            return;
        }

        char prefix[128] = {0};
        copyRequestParamValue_(request, "prefix", false, prefix, sizeof(prefix), "");
        size_t prefixLen = strnlen(prefix, sizeof(prefix));
        while (prefixLen > 0 && prefix[0] == '/') {
            memmove(prefix, prefix + 1, prefixLen);
            --prefixLen;
        }
        while (prefixLen > 0 && prefix[prefixLen - 1] == '/') {
            prefix[prefixLen - 1] = '\0';
            --prefixLen;
        }

        constexpr uint8_t kMaxModules = Limits::Config::Capacity::ModuleListMax;
        const char* modules[kMaxModules] = {0};
        const uint8_t moduleCount = cfgStore_->listModules(modules, kMaxModules);

        const char* childStarts[kMaxModules] = {0};
        size_t childLens[kMaxModules] = {0};
        uint8_t childCount = 0;
        bool hasExact = false;

        for (uint8_t i = 0; i < moduleCount; ++i) {
            const char* childStart = nullptr;
            size_t childLen = 0;
            bool exact = false;
            if (!childTokenForPrefix_(modules[i], prefix, prefixLen, childStart, childLen, exact)) {
                if (exact) hasExact = true;
                continue;
            }

            bool duplicate = false;
            for (uint8_t j = 0; j < childCount; ++j) {
                if (tokensEqual_(childStart, childLen, childStarts[j], childLens[j])) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            childStarts[childCount] = childStart;
            childLens[childCount] = childLen;
            ++childCount;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"has_exact\":");
        response->print(hasExact ? "true" : "false");
        response->print(",\"children\":[");
        for (uint8_t i = 0; i < childCount; ++i) {
            if (i > 0) response->print(',');
            response->print('\"');
            for (size_t j = 0; j < childLens[i]; ++j) {
                response->print(childStarts[i][j]);
            }
            response->print('\"');
        }
        response->print("]}");
        request->send(response);
        return;
#else
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->listChildrenJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.children\"}}");
            return;
        }
        char prefix[128] = {0};
        copyRequestParamValue_(request, "prefix", false, prefix, sizeof(prefix), "");
        char out[Limits::Mqtt::Buffers::Ack] = {0};
        if (!flowCfgSvc_->listChildrenJson(flowCfgSvc_->ctx, prefix, out, sizeof(out))) {
            if (out[0] != '\0') {
                LOGW("flowcfg.children failed prefix=%s details=%s", prefix, out);
                request->send(500, "application/json", out);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.children\"}}");
            }
            return;
        }
        request->send(200, "application/json", out);
#endif
    });

    server_.on("/api/flowcfg/module", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/module",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
#if defined(FLOW_PROFILE_WAVESHARE)
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.module\"}}");
            return;
        }
        if (!request->hasParam("name")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.module.name\"}}");
            return;
        }

        char moduleStr[64] = {0};
        copyRequestParamValue_(request, "name", false, moduleStr, sizeof(moduleStr), "");
        if (moduleStr[0] == '\0') {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.module.name\"}}");
            return;
        }

        char moduleName[64] = {0};
        snprintf(moduleName, sizeof(moduleName), "%s", moduleStr);
        sanitizeJsonString_(moduleName);

        bool truncated = false;
        WebHeapCharBuffer moduleJson(Limits::Mqtt::Buffers::StateCfg);
        if (!moduleJson) {
            sendTinyBusyJson_(request, "web_scratch_alloc");
            return;
        }
        if (!cfgStore_->toJsonModule(moduleStr, moduleJson.data, moduleJson.capacity, &truncated)) {
            request->send(404, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotFound\",\"where\":\"flowcfg.module.get\"}}");
            return;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"module\":");
        printJsonEscaped_(*response, moduleName);
        response->print(",\"truncated\":");
        response->print(truncated ? "true" : "false");
        response->print(",\"data\":");
        response->print(moduleJson.data);
        response->print('}');
        request->send(response);
        return;
#else
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->getModuleJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.module\"}}");
            return;
        }
        if (!request->hasParam("name")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.module.name\"}}");
            return;
        }

        char moduleStr[64] = {0};
        copyRequestParamValue_(request, "name", false, moduleStr, sizeof(moduleStr), "");
        if (moduleStr[0] == '\0') {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.module.name\"}}");
            return;
        }

        char moduleName[64] = {0};
        snprintf(moduleName, sizeof(moduleName), "%s", moduleStr);
        sanitizeJsonString_(moduleName);

        bool truncated = false;
        WebHeapCharBuffer moduleJson(Limits::Mqtt::Buffers::StateCfg);
        if (!moduleJson) {
            sendTinyBusyJson_(request, "web_scratch_alloc");
            return;
        }
        if (!flowCfgSvc_->getModuleJson(flowCfgSvc_->ctx, moduleStr, moduleJson.data, moduleJson.capacity, &truncated)) {
            if (moduleJson.data[0] != '\0') {
                LOGW("flowcfg.module failed module=%s details=%s", moduleStr, moduleJson.data);
                request->send(500, "application/json", moduleJson.data);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.module.get\"}}");
            }
            return;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"module\":");
        printJsonEscaped_(*response, moduleName);
        response->print(",\"truncated\":");
        response->print(truncated ? "true" : "false");
        response->print(",\"data\":");
        response->print(moduleJson.data);
        response->print('}');
        request->send(response);
#endif
    });

    server_.on("/api/flowcfg/apply", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/apply",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
#if defined(FLOW_PROFILE_WAVESHARE)
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.apply\"}}");
            return;
        }
        if (!request->hasParam("patch", true)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.apply.patch\"}}");
            return;
        }

        WebHeapCharBuffer patchStr(Limits::Mqtt::Buffers::StateCfg);
        if (!patchStr) {
            sendTinyBusyJson_(request, "web_scratch_alloc");
            return;
        }
        copyRequestParamValue_(request, "patch", true, patchStr.data, patchStr.capacity, "");
        if (!cfgStore_->applyJson(patchStr.data)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.apply.exec\"}}");
            return;
        }
        emitConfigPatchActivity_("Config flow.io", patchStr.data);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
#else
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->applyPatchJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.apply\"}}");
            return;
        }
        if (!request->hasParam("patch", true)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.apply.patch\"}}");
            return;
        }

        WebHeapCharBuffer patchStr(Limits::Mqtt::Buffers::StateCfg);
        if (!patchStr) {
            sendTinyBusyJson_(request, "web_scratch_alloc");
            return;
        }
        copyRequestParamValue_(request, "patch", true, patchStr.data, patchStr.capacity, "");
        char ack[Limits::Mqtt::Buffers::Ack] = {0};
        if (!flowCfgSvc_->applyPatchJson(flowCfgSvc_->ctx, patchStr.data, ack, sizeof(ack))) {
            if (ack[0] != '\0') {
                request->send(flowCfgApplyHttpStatus_(ack), "application/json", ack);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.apply.exec\"}}");
            }
            return;
        }
        request->send(200, "application/json", ack);
#endif
    });

    server_.on("/api/supervisorcfg/modules", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/modules");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.modules\"}}");
            return;
        }

        constexpr uint8_t kMaxModules = Limits::Config::Capacity::ModuleListMax;
        const char* modules[kMaxModules] = {0};
        const uint8_t moduleCount = cfgStore_->listModules(modules, kMaxModules);

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"modules\":[");
        bool first = true;
        for (uint8_t i = 0; i < moduleCount; ++i) {
            if (!modules[i] || modules[i][0] == '\0') continue;
            if (!first) response->print(',');
            printJsonEscaped_(*response, modules[i]);
            first = false;
        }
        response->print("]}");
        request->send(response);
    });

    server_.on("/api/supervisorcfg/children", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/children");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.children\"}}");
            return;
        }

        char prefix[128] = {0};
        copyRequestParamValue_(request, "prefix", false, prefix, sizeof(prefix), "");

        size_t prefixLen = strnlen(prefix, sizeof(prefix));
        while (prefixLen > 0 && prefix[0] == '/') {
            memmove(prefix, prefix + 1, prefixLen);
            --prefixLen;
        }
        while (prefixLen > 0 && prefix[prefixLen - 1] == '/') {
            prefix[prefixLen - 1] = '\0';
            --prefixLen;
        }

        constexpr uint8_t kMaxModules = Limits::Config::Capacity::ModuleListMax;
        const char* modules[kMaxModules] = {0};
        const uint8_t moduleCount = cfgStore_->listModules(modules, kMaxModules);

        const char* childStarts[kMaxModules] = {0};
        size_t childLens[kMaxModules] = {0};
        uint8_t childCount = 0;
        bool hasExact = false;

        for (uint8_t i = 0; i < moduleCount; ++i) {
            const char* childStart = nullptr;
            size_t childLen = 0;
            bool exact = false;
            if (!childTokenForPrefix_(modules[i], prefix, prefixLen, childStart, childLen, exact)) {
                if (exact) hasExact = true;
                continue;
            }

            bool duplicate = false;
            for (uint8_t j = 0; j < childCount; ++j) {
                if (tokensEqual_(childStart, childLen, childStarts[j], childLens[j])) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            childStarts[childCount] = childStart;
            childLens[childCount] = childLen;
            ++childCount;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"has_exact\":");
        response->print(hasExact ? "true" : "false");
        response->print(",\"children\":[");
        for (uint8_t i = 0; i < childCount; ++i) {
            if (i > 0) response->print(',');
            response->print('\"');
            for (size_t j = 0; j < childLens[i]; ++j) {
                response->print(childStarts[i][j]);
            }
            response->print('\"');
        }
        response->print("]}");
        request->send(response);
    });

    server_.on("/api/supervisorcfg/module", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/module");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.module\"}}");
            return;
        }
        if (!request->hasParam("name")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"supervisorcfg.module.name\"}}");
            return;
        }

        char moduleStr[64] = {0};
        copyRequestParamValue_(request, "name", false, moduleStr, sizeof(moduleStr), "");
        if (moduleStr[0] == '\0') {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"supervisorcfg.module.name\"}}");
            return;
        }

        char moduleName[64] = {0};
        snprintf(moduleName, sizeof(moduleName), "%s", moduleStr);
        sanitizeJsonString_(moduleName);

        bool truncated = false;
        WebHeapCharBuffer moduleJson(Limits::Mqtt::Buffers::StateCfg);
        if (!moduleJson) {
            sendTinyBusyJson_(request, "web_scratch_alloc");
            return;
        }
        if (!cfgStore_->toJsonModule(moduleStr, moduleJson.data, moduleJson.capacity, &truncated)) {
            request->send(404, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotFound\",\"where\":\"supervisorcfg.module.get\"}}");
            return;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"module\":");
        printJsonEscaped_(*response, moduleName);
        response->print(",\"truncated\":");
        response->print(truncated ? "true" : "false");
        response->print(",\"data\":");
        response->print(moduleJson.data);
        response->print('}');
        request->send(response);
    });

    server_.on("/api/supervisorcfg/apply", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/apply");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.apply\"}}");
            return;
        }
        if (!request->hasParam("patch", true)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"supervisorcfg.apply.patch\"}}");
            return;
        }

        WebHeapCharBuffer patchStr(Limits::Mqtt::Buffers::StateCfg);
        if (!patchStr) {
            sendTinyBusyJson_(request, "web_scratch_alloc");
            return;
        }
        copyRequestParamValue_(request, "patch", true, patchStr.data, patchStr.capacity, "");
        if (!cfgStore_->applyJson(patchStr.data)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"supervisorcfg.apply.exec\"}}");
            return;
        }
        emitConfigPatchActivity_("Config locale", patchStr.data);
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/system/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/system/reboot");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"system.reboot\"}}");
            return;
        }

        char reply[196] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "system.reboot", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"system.reboot\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/api/system/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/system/factory-reset");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"system.factory_reset\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "system.factory_reset", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"system.factory_reset\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/api/system/nextion/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/system/nextion/reboot");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fw.nextion.reboot\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "fw.nextion.reboot", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fw.nextion.reboot\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/api/flow/system/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/flow/system/reboot");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.system.reboot\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "flow.system.reboot", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.system.reboot\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/api/flow/system/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/flow/system/factory-reset");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.system.factory_reset\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "flow.system.factory_reset", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.system.factory_reset\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/fwupdate/waveshare", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/waveshare");
        handleUpdateRequest_(request, FirmwareUpdateTarget::Waveshare);
    });

    server_.on("/fwupdate/nextion", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/nextion");
        handleUpdateRequest_(request, FirmwareUpdateTarget::Nextion);
    });
    server_.on("/fwupdate/spiffs", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/spiffs");
        handleUpdateRequest_(request, FirmwareUpdateTarget::Spiffs);
    });

    server_.onNotFound([this, webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        noteHttpActivity_();
        request->redirect(webInterfaceLandingUrl());
    });

    if (!provisioningOnly_) {
        wsLog_.onEvent([this](AsyncWebSocket* server,
                              AsyncWebSocketClient* client,
                              AwsEventType type,
                              void* arg,
                              uint8_t* data,
                              size_t len) {
            this->onWsLogEvent_(server, client, type, arg, data, len);
        });

        server_.addHandler(&wsLog_);
        if (!bridgeUartEnabled_) {
            LOGI("WebInterface flow serial stream unavailable (bridge UART unavailable)");
        }
    } else {
        LOGI("WebInterface WS handlers disabled in provisioning-only mode");
    }
    server_.begin();
    started_ = true;
    noteServerStarted_();
    LOGI("WebInterface server started, listening on 0.0.0.0:%d", kServerPort);

    if (hmiSvc_ && hmiSvc_->setStatusLedState && hmiSvc_->setStatusLedAutoWifiMode) {
        bool prevAutoMode = true;
        if (hmiSvc_->isStatusLedAutoWifiMode) {
            prevAutoMode = hmiSvc_->isStatusLedAutoWifiMode(hmiSvc_->ctx);
        }
        webStartLedPrevAutoMode_ = prevAutoMode;
        webStartLedPrevAutoModeValid_ = true;

        HmiStatusLedState webStartState{};
        webStartState.enabled = true;
        webStartState.blinkEnabled = true;
        webStartState.red = 0;
        webStartState.green = 255;
        webStartState.blue = 0;
        webStartState.brightness = 128;
        webStartState.blinkOnMs = 60;
        webStartState.blinkOffMs = 60;

        const bool autoModeDisabled = hmiSvc_->setStatusLedAutoWifiMode(hmiSvc_->ctx, false);
        const bool stateApplied = hmiSvc_->setStatusLedState(hmiSvc_->ctx, &webStartState);
        if (autoModeDisabled && stateApplied) {
            webStartLedPulseActive_ = true;
            webStartLedPulseUntilMs_ = millis() + 2000U;
            LOGI("Web start LED pulse active color=green duration_ms=2000");
        } else {
            if (autoModeDisabled && webStartLedPrevAutoModeValid_) {
                hmiSvc_->setStatusLedAutoWifiMode(hmiSvc_->ctx, webStartLedPrevAutoMode_);
            }
            webStartLedPulseActive_ = false;
            LOGW("Web start LED pulse failed auto_disabled=%d state_applied=%d",
                 autoModeDisabled ? 1 : 0,
                 stateApplied ? 1 : 0);
        }
    }

    char ip[16] = {0};
    NetworkAccessMode mode = NetworkAccessMode::None;
    if (getNetworkIp_(ip, sizeof(ip), &mode) && ip[0] != '\0') {
        if (mode == NetworkAccessMode::AccessPoint) {
            LOGI("WebInterface URL (AP): http://%s/webinterface", ip);
        } else {
            LOGI("WebInterface URL: http://%s/webinterface", ip);
        }
    } else {
        LOGI("WebInterface URL: waiting for network IP");
    }
}

void WebInterfaceModule::handleUpdateRequest_(AsyncWebServerRequest* request, FirmwareUpdateTarget target)
{
    if (!request) return;
    if (!fwUpdateSvc_ && services_) {
        fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    }
    if (!fwUpdateSvc_ || !fwUpdateSvc_->start) {
        request->send(503, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.start\"}}");
        return;
    }

    char urlBuf[224] = {0};
    copyRequestParamValue_(request, "url", true, urlBuf, sizeof(urlBuf), "");
    const char* url = (urlBuf[0] != '\0') ? urlBuf : nullptr;

    char err[144] = {0};
    if (!fwUpdateSvc_->start(fwUpdateSvc_->ctx, target, url, err, sizeof(err))) {
        sanitizeJsonString_(err);
        char out[336] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.start\",\"msg\":\"%s\"}}",
                               err[0] ? err : "failed");
        request->send(409,
                      "application/json",
                      (n > 0 && (size_t)n < sizeof(out))
                          ? out
                          : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.start\"}}");
        return;
    }

    request->send(202, "application/json", "{\"ok\":true,\"accepted\":true}");
}

bool WebInterfaceModule::isWebReachable_() const
{
    if (netAccessSvc_ && netAccessSvc_->isWebReachable) {
        return netAccessSvc_->isWebReachable(netAccessSvc_->ctx);
    }
    if (wifiSvc_ && wifiSvc_->isConnected) {
        return wifiSvc_->isConnected(wifiSvc_->ctx);
    }
    return netReady_;
}

bool WebInterfaceModule::getNetworkIp_(char* out, size_t len, NetworkAccessMode* modeOut) const
{
    if (out && len > 0) out[0] = '\0';
    if (modeOut) *modeOut = NetworkAccessMode::None;
    if (!out || len == 0) return false;

    if (netAccessSvc_ && netAccessSvc_->getIP) {
        if (netAccessSvc_->getIP(netAccessSvc_->ctx, out, len)) {
            if (modeOut && netAccessSvc_->mode) {
                *modeOut = netAccessSvc_->mode(netAccessSvc_->ctx);
            }
            return out[0] != '\0';
        }
    }

    if (wifiSvc_ && wifiSvc_->getIP) {
        if (wifiSvc_->getIP(wifiSvc_->ctx, out, len)) {
            if (modeOut) *modeOut = NetworkAccessMode::Station;
            return out[0] != '\0';
        }
    }

    return false;
}

const char* WebInterfaceModule::networkTransport_(NetworkAccessMode mode) const
{
    if (mode != NetworkAccessMode::Station) return "none";

    const IPAddress ethIp = ETH.localIP();
    if (ethIp[0] != 0 || ethIp[1] != 0 || ethIp[2] != 0 || ethIp[3] != 0) {
        return "ethernet";
    }

    if (WiFi.isConnected()) return "wifi";
    return "none";
}
