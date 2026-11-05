/**
 * @file WifiModule.cpp
 * @brief Implementation file.
 */
#include "WifiModule.h"
#include "Core/BufferUsageTracker.h"
#include "Core/EventBus/EventPayloads.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::WifiModule)
#include "Core/ModuleLog.h"
#include "Core/Runtime.h"
#include <ArduinoJson.h>
#include <esp_mac.h>
#include <ctype.h>
#include <string.h>

namespace {
WifiModule* gWifiModuleInstance = nullptr;
static constexpr uint8_t kWifiCfgProducerId = 42;
static constexpr uint8_t kWifiCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kWifiCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Wifi, kWifiCfgBranch}, "wifi", "wifi", (uint8_t)MqttPublishPriority::Normal, nullptr},
};

bool isBlank_(const char* text, size_t maxLen)
{
    if (!text) return true;
    const size_t len = strnlen(text, maxLen);
    if (len == 0U || len >= maxLen) return true;
    for (size_t i = 0; i < len; ++i) {
        if (!isspace((unsigned char)text[i])) return false;
    }
    return true;
}

void formatHostname_(const char* deviceName, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;

    size_t w = 0U;
    if (deviceName) {
        for (size_t i = 0U; deviceName[i] != '\0' && w < (outLen - 1U); ++i) {
            const char c = deviceName[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
                out[w++] = (char)tolower((unsigned char)c);
            } else if (c == ' ' || c == '_' || c == '.') {
                out[w++] = '-';
            }
        }
    }
    out[w] = '\0';

    while (w > 0U && out[0] == '-') {
        memmove(out, out + 1, w);
        --w;
    }
    while (w > 0U && out[w - 1U] == '-') {
        out[--w] = '\0';
    }

    if (out[0] == '\0') {
        snprintf(out, outLen, "flowio");
    }
}

void formatDhcpHostname_(const char* deviceName, char* out, size_t outLen)
{
    formatHostname_(deviceName, out, outLen);
    if (!out || outLen == 0U || strcmp(out, "flowio") != 0) return;

    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return;
    snprintf(out,
             outLen,
             "FlowIO-%02X%02X%02X",
             (unsigned)mac[3],
             (unsigned)mac[4],
             (unsigned)mac[5]);
}

void formatStaMac_(char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out,
             outLen,
             "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)mac[0],
             (unsigned)mac[1],
             (unsigned)mac[2],
             (unsigned)mac[3],
             (unsigned)mac[4],
             (unsigned)mac[5]);
}

const char* wifiModeName_(wifi_mode_t mode)
{
    switch (mode) {
    case WIFI_MODE_NULL: return "NULL";
    case WIFI_MODE_STA: return "STA";
    case WIFI_MODE_AP: return "AP";
    case WIFI_MODE_APSTA: return "APSTA";
    default: return "?";
    }
}
}

WifiModule::WifiModule(const BoardSpec& board)
{
    (void)board;
}

WifiState WifiModule::stateSvc_() const {
    return state;
}

bool WifiModule::isConnected_() const {
    return WiFi.isConnected();
}

bool WifiModule::getIP_(char* out, size_t len) const {
    if (!out || len == 0) return false;

    if (!WiFi.isConnected()) {
        out[0] = '\0';
        return false;
    }

    IPAddress ip = WiFi.localIP();
    snprintf(out, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return true;
}

bool WifiModule::requestReconnect_()
{
    stopMdns_();
    gotIpSent = false;
    if (dataStore) {
        setWifiReady(*dataStore, false);
    }

    (void)WiFi.disconnect(false, false);
    LOGD("requestReconnect: forced disconnect mode=%s wl=%s(%d)",
         wifiModeName_(WiFi.getMode()),
         wlStatusName_(WiFi.status()),
         (int)WiFi.status());
    setState(WifiState::Idle);
    return true;
}

bool WifiModule::scanStatusJson_(char* out, size_t outLen)
{
    return buildScanStatusJson_(out, outLen);
}

bool WifiModule::setStaRetryEnabled_(bool enabled)
{
    if (staRetryEnabled_ == enabled) return true;
    staRetryEnabled_ = enabled;

    LOGD("STA retries %s", enabled ? "enabled" : "disabled");

    if (!enabled) {
        reconnectKickSent_ = true;
        setState(WifiState::Idle);
    } else if (state != WifiState::Connected && state != WifiState::Disabled) {
        setState(WifiState::Idle);
    }

    return true;
}

bool WifiModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);

    switch (valueId) {
        case RuntimeUiReady:
            return writer.writeBool(runtimeId, dataStore ? wifiReady(*dataStore) : false);

        case RuntimeUiIp: {
            if (!dataStore) return writer.writeUnavailable(runtimeId);
            const IpV4 ip = wifiIp(*dataStore);
            char ipText[16] = {0};
            snprintf(ipText,
                     sizeof(ipText),
                     "%u.%u.%u.%u",
                     (unsigned)ip.b[0],
                     (unsigned)ip.b[1],
                     (unsigned)ip.b[2],
                     (unsigned)ip.b[3]);
            return writer.writeString(runtimeId, ipText);
        }

        case RuntimeUiRssi:
            if (!WiFi.isConnected()) return writer.writeUnavailable(runtimeId);
            return writer.writeI32(runtimeId, (int32_t)WiFi.RSSI());

        default:
            return false;
    }
}

void WifiModule::onWifiEventSys_(arduino_event_t* event)
{
    if (!event) return;
    WifiModule* self = gWifiModuleInstance;
    if (!self) return;

    switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_START:
        LOGI("WiFi event: STA_START mode=%s wl=%s(%d) state=%s",
             wifiModeName_(WiFi.getMode()),
             wlStatusName_(WiFi.status()),
             (int)WiFi.status(),
             stateName_(self->state));
        break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
        LOGI("WiFi event: STA_STOP mode=%s wl=%s(%d) state=%s",
             wifiModeName_(WiFi.getMode()),
             wlStatusName_(WiFi.status()),
             (int)WiFi.status(),
             stateName_(self->state));
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        LOGI("WiFi event: STA_CONNECTED mode=%s wl=%s(%d) state=%s",
             wifiModeName_(WiFi.getMode()),
             wlStatusName_(WiFi.status()),
             (int)WiFi.status(),
             stateName_(self->state));
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        LOGI("WiFi event: STA_GOT_IP mode=%s wl=%s(%d) state=%s",
             wifiModeName_(WiFi.getMode()),
             wlStatusName_(WiFi.status()),
             (int)WiFi.status(),
             stateName_(self->state));
        break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        LOGW("WiFi event: STA_LOST_IP mode=%s wl=%s(%d) state=%s",
             wifiModeName_(WiFi.getMode()),
             wlStatusName_(WiFi.status()),
             (int)WiFi.status(),
             stateName_(self->state));
        break;
    case ARDUINO_EVENT_WIFI_AP_START:
        LOGI("WiFi event: AP_START mode=%s wl=%s(%d) state=%s",
             wifiModeName_(WiFi.getMode()),
             wlStatusName_(WiFi.status()),
             (int)WiFi.status(),
             stateName_(self->state));
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        LOGI("WiFi event: AP_STOP mode=%s wl=%s(%d) state=%s",
             wifiModeName_(WiFi.getMode()),
             wlStatusName_(WiFi.status()),
             (int)WiFi.status(),
             stateName_(self->state));
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        const uint8_t reason = (uint8_t)event->event_info.wifi_sta_disconnected.reason;
        self->lastDisconnectReason_ = reason;
        const char* reasonName = WiFi.disconnectReasonName((wifi_err_reason_t)reason);
        if (self->isStartupTransientWindow_()) {
            LOGD("STA disconnected reason=%u(%s) mode=%s wl=%s(%d) state=%s",
                 (unsigned)reason,
                 reasonName ? reasonName : "?",
                 wifiModeName_(WiFi.getMode()),
                 wlStatusName_(WiFi.status()),
                 (int)WiFi.status(),
                 stateName_(self->state));
        } else {
            LOGW("STA disconnected reason=%u(%s) mode=%s wl=%s(%d) state=%s",
                 (unsigned)reason,
                 reasonName ? reasonName : "?",
                 wifiModeName_(WiFi.getMode()),
                 wlStatusName_(WiFi.status()),
                 (int)WiFi.status(),
                 stateName_(self->state));
        }
        break;
    }
    default:
        break;
    }
}

const char* WifiModule::wlStatusName_(wl_status_t st)
{
    switch (st) {
    case WL_NO_SHIELD: return "NO_SHIELD";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
    }
}

const char* WifiModule::stateName_(WifiState s)
{
    switch (s) {
    case WifiState::Disabled: return "Disabled";
    case WifiState::Idle: return "Idle";
    case WifiState::Connecting: return "Connecting";
    case WifiState::Connected: return "Connected";
    case WifiState::ErrorWait: return "ErrorWait";
    default: return "Unknown";
    }
}

bool WifiModule::cmdDumpCfg_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    (void)req;
    WifiModule* self = static_cast<WifiModule*>(userCtx);
    if (!self || !reply || replyLen == 0) return false;

    const size_t ssidLen = strnlen(self->cfgData.ssid, sizeof(self->cfgData.ssid));
    const size_t passLen = strnlen(self->cfgData.pass, sizeof(self->cfgData.pass));
    const size_t deviceNameLen = strnlen(self->deviceName_, sizeof(self->deviceName_));

    StaticJsonDocument<512> doc;
    doc["ok"] = true;
    doc["enabled"] = self->cfgData.enabled;
    doc["state"] = stateName_(self->state);
    doc["wl_status"] = wlStatusName_(WiFi.status());
    doc["ssid"] = self->cfgData.ssid;
    doc["ssid_len"] = (uint32_t)ssidLen;
    doc["pass"] = self->cfgData.pass;
    doc["pass_len"] = (uint32_t)passLen;
    doc["devicename"] = self->deviceName_;
    doc["devicename_len"] = (uint32_t)deviceNameLen;
    doc["mdns"] = self->mdnsApplied;
    doc["connected"] = WiFi.isConnected();
    doc["rssi"] = WiFi.isConnected() ? WiFi.RSSI() : -127;
    doc["last_disconnect_reason"] = (uint32_t)self->lastDisconnectReason_;
    const char* reasonName = WiFi.disconnectReasonName((wifi_err_reason_t)self->lastDisconnectReason_);
    doc["last_disconnect_reason_name"] = reasonName ? reasonName : "?";

    IPAddress ip = WiFi.localIP();
    char ipText[16] = {0};
    snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    doc["ip"] = ipText;
    char macText[18] = {0};
    formatStaMac_(macText, sizeof(macText));
    doc["mac"] = macText;

    const size_t written = serializeJson(doc, reply, replyLen);
    return written > 0 && written < replyLen;
}

void WifiModule::logConfigSummary_() const
{
    const size_t ssidLen = strnlen(cfgData.ssid, sizeof(cfgData.ssid));
    const size_t passLen = strnlen(cfgData.pass, sizeof(cfgData.pass));
    const size_t deviceNameLen = strnlen(deviceName_, sizeof(deviceName_));

    if (ssidLen == 0U) {
        LOGW("WiFi config loaded enabled=%d ethernet=%d ssid=<empty> pass_len=%u devicename='%s' devicename_len=%u",
             (int)cfgData.enabled,
             (int)ethernetEnabled_,
             (unsigned)passLen,
             deviceName_,
             (unsigned)deviceNameLen);
        return;
    }

    LOGI("WiFi config loaded enabled=%d ethernet=%d ssid='%s' ssid_len=%u pass_len=%u devicename='%s' devicename_len=%u",
         (int)cfgData.enabled,
         (int)ethernetEnabled_,
         cfgData.ssid,
         (unsigned)ssidLen,
         (unsigned)passLen,
         deviceName_,
         (unsigned)deviceNameLen);
}

bool WifiModule::isStartupTransientWindow_() const
{
    return !hadSuccessfulConnection_ && startupTransientLogUntilMs_ != 0U && millis() < startupTransientLogUntilMs_;
}

void WifiModule::setState(WifiState s) {
    if (s == state) return;
    const WifiState previous = state;
    state = s;
    stateTs = millis();
    LOGD("State %s -> %s mode=%s wl=%s(%d)",
         stateName_(previous),
         stateName_(state),
         wifiModeName_(WiFi.getMode()),
         wlStatusName_(WiFi.status()),
         (int)WiFi.status());

    if (state == WifiState::Connected) {
        hadSuccessfulConnection_ = true;
        lastDisconnectReason_ = 0;
    }

    if (state == WifiState::Idle || state == WifiState::ErrorWait || state == WifiState::Disabled) {
        stopMdns_();
        if (dataStore) {
            setWifiReady(*dataStore, false);
        }
        gotIpSent = false;
    }
}

void WifiModule::startConnect() {
    const bool transientBoot = isStartupTransientWindow_();
    const uint32_t now = millis();
    cfgData.ssid[sizeof(cfgData.ssid) - 1U] = '\0';
    cfgData.pass[sizeof(cfgData.pass) - 1U] = '\0';

    const size_t ssidLen = strnlen(cfgData.ssid, sizeof(cfgData.ssid));
    const size_t passLen = strnlen(cfgData.pass, sizeof(cfgData.pass));
    if (ssidLen >= sizeof(cfgData.ssid) || passLen >= sizeof(cfgData.pass)) {
        LOGE("Invalid WiFi credentials (missing null terminator) ssid_len=%u pass_len=%u",
             (unsigned)ssidLen,
             (unsigned)passLen);
        setState(WifiState::ErrorWait);
        return;
    }

    char ssidSafe[sizeof(cfgData.ssid)] = {0};
    char passSafe[sizeof(cfgData.pass)] = {0};
    memcpy(ssidSafe, cfgData.ssid, ssidLen);
    ssidSafe[ssidLen] = '\0';
    memcpy(passSafe, cfgData.pass, passLen);
    passSafe[passLen] = '\0';

    bool ssidOnlySpaces = true;
    for (size_t i = 0; i < ssidLen; ++i) {
        if (!isspace((unsigned char)ssidSafe[i])) {
            ssidOnlySpaces = false;
            break;
        }
    }

    if (ssidLen == 0U || ssidOnlySpaces) {
        if ((now - lastEmptySsidLogMs) >= Limits::Wifi::Timing::EmptySsidLogIntervalMs) {
            lastEmptySsidLogMs = now;
            LOGW("SSID empty/blank, skipping connection (enabled=%d)", (int)cfgData.enabled);
        }
        setState(WifiState::Idle);
        return;
    }

    if (lastBeginMs_ != 0U && (now - lastBeginMs_) < beginBackoffMs_) {
        LOGD("Begin throttled elapsed=%lu backoff=%lu mode=%s wl=%s(%d)",
             (unsigned long)(now - lastBeginMs_),
             (unsigned long)beginBackoffMs_,
             wifiModeName_(WiFi.getMode()),
             wlStatusName_(WiFi.status()),
             (int)WiFi.status());
        return;
    }

    ++connectAttempt_;
    LOGD("Connecting #%lu to ssid='%s' pass_len=%u",
         (unsigned long)connectAttempt_,
         ssidSafe,
         (unsigned)passLen);
    reconnectKickSent_ = false;
    lastConnectStatus_ = WL_IDLE_STATUS;
    lastDisconnectReason_ = 0;
    lastConnectingLogMs_ = millis();

    char hostname[sizeof(deviceName_)] = {0};
    formatDhcpHostname_(deviceName_, hostname, sizeof(hostname));
    if (!WiFi.setHostname(hostname)) {
        LOGW("WiFi DHCP hostname default update failed host=%s", hostname);
    }

    if (!WiFi.enableSTA(true)) {
        if (transientBoot) {
            LOGD("enableSTA not ready yet during boot");
            setState(WifiState::Idle);
        } else {
            LOGE("enableSTA failed before connect");
            setState(WifiState::ErrorWait);
        }
        return;
    }

    const wifi_mode_t modeNow = WiFi.getMode();
    const bool keepAp = (modeNow == WIFI_MODE_AP || modeNow == WIFI_MODE_APSTA);
    const wifi_mode_t wantedMode = keepAp ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    const bool modeOk = WiFi.mode(wantedMode);
    if (!modeOk) {
        LOGW("WiFi.mode failed requested=%d current=%d", (int)wantedMode, (int)WiFi.getMode());
    }
    if (!WiFi.STA.setHostname(hostname)) {
        LOGW("WiFi DHCP hostname update failed host=%s", hostname);
    } else {
        LOGI("WiFi DHCP hostname=%s", hostname);
    }
    WiFi.setSleep(false);               ///< ✅ important (stability)

    // Avoid explicit disconnect/restart churn between attempts: the underlying
    // WiFi stack is already auto-reconnect capable and this reduces race windows
    // when AP is unavailable.

    const wl_status_t beginStatus = WiFi.begin(ssidSafe, passSafe);
    lastBeginMs_ = now;
    if (beginStatus == WL_CONNECT_FAILED) {
        beginBackoffMs_ = 8000U;
        if (transientBoot) {
            LOGD("WiFi.begin returned CONNECT_FAILED during boot for ssid='%s'", ssidSafe);
        } else {
            LOGW("WiFi.begin returned CONNECT_FAILED for ssid='%s'", ssidSafe);
        }
        setState(WifiState::ErrorWait);
        return;
    }
    beginBackoffMs_ = 1500U;

    setState(WifiState::Connecting);
}

bool WifiModule::requestScan_(bool force)
{
    const uint32_t now = millis();
    constexpr uint32_t kScanForceMinIntervalMs = 2500U;

    bool running = false;
    bool alreadyRequested = false;
    uint32_t lastDone = 0U;
    portENTER_CRITICAL(&scanMux_);
    running = scanRunning_;
    alreadyRequested = scanRequested_;
    lastDone = scanLastDoneMs_;
    if (!running && !alreadyRequested) {
        if (!force && lastDone != 0U && (now - lastDone) < kScanThrottleMs) {
            portEXIT_CRITICAL(&scanMux_);
            return true;
        }
        if (force && lastDone != 0U && (now - lastDone) < kScanForceMinIntervalMs) {
            portEXIT_CRITICAL(&scanMux_);
            return true;
        }
        scanRequested_ = true;
        scanApRetryCount_ = 0;
        scanFailRetryCount_ = 0;
    }
    portEXIT_CRITICAL(&scanMux_);
    return true;
}

void WifiModule::processScan_()
{
    if (scanRunning_) {
        const int16_t status = WiFi.scanComplete();
        if (status == WIFI_SCAN_RUNNING) return;
        if (status == WIFI_SCAN_FAILED) {
            // Arduino-ESP32 async scan can transiently report FAILED (-2) before
            // posting final scan-done bits; keep waiting until timeout expires.
            const uint32_t now = millis();
            const uint32_t timeoutMs = (scanTimeoutMs_ > 0U) ? scanTimeoutMs_ : (kScanPrimaryDwellMs * 20U);
            if ((now - scanLastStartMs_) < timeoutMs) {
                return;
            }
        }

        if (status < 0) {
            if (scanFailRetryCount_ == 0U) {
                ++scanFailRetryCount_;
                WiFi.scanDelete();
                const int16_t retryStatus = WiFi.scanNetworks(true, false, false, kScanRetryDwellMs);
                if (retryStatus != WIFI_SCAN_FAILED) {
                    portENTER_CRITICAL(&scanMux_);
                    scanRunning_ = true;
                    scanLastStartMs_ = millis();
                    scanTimeoutMs_ = kScanRetryDwellMs * 20U;
                    scanLastError_ = 0;
                    portEXIT_CRITICAL(&scanMux_);
                    LOGW("WiFi scan retry started after status=%d", (int)status);
                    return;
                }
            }
            portENTER_CRITICAL(&scanMux_);
            scanRunning_ = false;
            scanTimeoutMs_ = 0U;
            scanLastError_ = status;
            scanLastDoneMs_ = millis();
            portEXIT_CRITICAL(&scanMux_);
            WiFi.scanDelete();
            LOGW("WiFi scan failed status=%d", (int)status);
            return;
        }

        WifiScanEntry local[kScanMaxResults] = {};
        uint8_t localCount = 0;
        const int16_t total = status;

        for (int16_t i = 0; i < total; ++i) {
            String ssid = WiFi.SSID(i);
            const bool hidden = (ssid.length() == 0);
            if (hidden && !kScanIncludeHidden) {
                continue;
            }
            char ssidBuf[33] = {0};
            if (hidden) {
                snprintf(ssidBuf, sizeof(ssidBuf), "<hidden>");
            } else {
                snprintf(ssidBuf, sizeof(ssidBuf), "%s", ssid.c_str());
            }

            const int32_t rssi = WiFi.RSSI(i);
            const uint8_t auth = (uint8_t)WiFi.encryptionType(i);

            int8_t found = -1;
            for (uint8_t j = 0; j < localCount; ++j) {
                if (strcmp(local[j].ssid, ssidBuf) == 0) {
                    found = (int8_t)j;
                    break;
                }
            }

            if (found >= 0) {
                if (rssi > local[(uint8_t)found].rssi) {
                    local[(uint8_t)found].rssi = (int16_t)rssi;
                    local[(uint8_t)found].auth = auth;
                    local[(uint8_t)found].hidden = hidden;
                }
                continue;
            }

            if (localCount >= kScanMaxResults) continue;

            snprintf(local[localCount].ssid, sizeof(local[localCount].ssid), "%s", ssidBuf);
            local[localCount].rssi = (int16_t)rssi;
            local[localCount].auth = auth;
            local[localCount].hidden = hidden;
            ++localCount;
        }

        for (uint8_t i = 0; i < localCount; ++i) {
            for (uint8_t j = (uint8_t)(i + 1U); j < localCount; ++j) {
                if (local[j].rssi > local[i].rssi) {
                    WifiScanEntry tmp = local[i];
                    local[i] = local[j];
                    local[j] = tmp;
                }
            }
        }

        const wifi_mode_t modeAfterScan = WiFi.getMode();
        if (total == 0 && modeAfterScan == WIFI_MODE_APSTA && scanApRetryCount_ == 0U) {
            // In AP+STA mode, a first async scan can sporadically return 0.
            // Retry once with longer channel dwell before concluding "no network".
            ++scanApRetryCount_;
            WiFi.scanDelete();
            const int16_t retryStatus = WiFi.scanNetworks(true, false, false, kScanRetryDwellMs);
            if (retryStatus != WIFI_SCAN_FAILED) {
                portENTER_CRITICAL(&scanMux_);
                scanRunning_ = true;
                scanLastStartMs_ = millis();
                scanTimeoutMs_ = kScanRetryDwellMs * 20U;
                scanLastError_ = 0;
                portEXIT_CRITICAL(&scanMux_);
                LOGW("WiFi scan AP retry started");
                return;
            }
            LOGW("WiFi scan AP retry start failed");
        }

        portENTER_CRITICAL(&scanMux_);
        scanCount_ = localCount;
        scanTotalFound_ = (uint8_t)((total > 255) ? 255 : total);
        for (uint8_t i = 0; i < localCount; ++i) {
            scanEntries_[i] = local[i];
        }
        scanHasResults_ = true;
        scanRunning_ = false;
        scanTimeoutMs_ = 0U;
        scanFailRetryCount_ = 0;
        scanLastError_ = 0;
        scanLastDoneMs_ = millis();
        ++scanGeneration_;
        portEXIT_CRITICAL(&scanMux_);
        BufferUsageTracker::note(TrackedBufferId::WifiScanEntries,
                                 (size_t)localCount * sizeof(WifiScanEntry),
                                 sizeof(scanEntries_),
                                 "scan",
                                 (localCount > 0U) ? local[0].ssid : nullptr);

        WiFi.scanDelete();
        LOGI("WiFi scan done total=%d kept=%u", (int)total, (unsigned)localCount);
        return;
    }

    if (!scanRequested_) return;
    if (state == WifiState::Connecting) return;

    const wifi_mode_t modeNow = WiFi.getMode();
    if (modeNow == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_MODE_STA);
    } else if (modeNow == WIFI_MODE_AP) {
        WiFi.mode(WIFI_MODE_APSTA);
    }

    scanRequested_ = false;
    const int16_t startStatus = WiFi.scanNetworks(true, false, false, kScanPrimaryDwellMs);
    if (startStatus == WIFI_SCAN_FAILED) {
        portENTER_CRITICAL(&scanMux_);
        scanRunning_ = false;
        scanTimeoutMs_ = 0U;
        scanLastError_ = WIFI_SCAN_FAILED;
        scanLastDoneMs_ = millis();
        portEXIT_CRITICAL(&scanMux_);
        LOGW("WiFi scan start failed");
        return;
    }

    portENTER_CRITICAL(&scanMux_);
    scanRunning_ = true;
    scanLastStartMs_ = millis();
    scanTimeoutMs_ = kScanPrimaryDwellMs * 20U;
    scanLastError_ = 0;
    portEXIT_CRITICAL(&scanMux_);
}

bool WifiModule::buildScanStatusJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;

    WifiScanEntry local[kScanMaxResults] = {};
    bool running = false;
    bool requested = false;
    bool hasResults = false;
    int16_t lastErr = 0;
    uint8_t count = 0;
    uint8_t total = 0;
    uint32_t startedMs = 0;
    uint32_t doneMs = 0;
    uint16_t gen = 0;

    portENTER_CRITICAL(&scanMux_);
    running = scanRunning_;
    requested = scanRequested_;
    hasResults = scanHasResults_;
    lastErr = scanLastError_;
    count = scanCount_;
    total = scanTotalFound_;
    startedMs = scanLastStartMs_;
    doneMs = scanLastDoneMs_;
    gen = scanGeneration_;
    for (uint8_t i = 0; i < count; ++i) {
        local[i] = scanEntries_[i];
    }
    portEXIT_CRITICAL(&scanMux_);

    StaticJsonDocument<Limits::Wifi::Buffers::ScanStatusJson> doc;
    doc["ok"] = true;
    doc["running"] = running;
    doc["requested"] = requested;
    doc["has_results"] = hasResults;
    doc["count"] = count;
    doc["total_found"] = total;
    doc["generation"] = gen;
    doc["last_error"] = lastErr;
    doc["started_ms"] = startedMs;
    doc["updated_ms"] = doneMs;

    JsonArray nets = doc.createNestedArray("networks");
    for (uint8_t i = 0; i < count; ++i) {
        JsonObject n = nets.createNestedObject();
        n["ssid"] = local[i].ssid;
        n["rssi"] = local[i].rssi;
        n["auth"] = local[i].auth;
        n["secure"] = (local[i].auth != WIFI_AUTH_OPEN);
        n["hidden"] = local[i].hidden;
    }

    const size_t wrote = serializeJson(doc, out, outLen);
    return wrote > 0 && wrote < outLen;
}

void WifiModule::registerHaEntities_(ServiceRegistry& services)
{
    if (haEntitiesRegistered_) return;
    if (!haSvc_) haSvc_ = services.get<HAService>(ServiceId::Ha);
    if (!haSvc_ || !haSvc_->addSensor) return;

    static constexpr const char* kWifiAvailabilityTpl =
        "{{ 'online' if value_json.ready else 'offline' }}";

    const HASensorEntry wifiIp{
        "wifi",
        "wifi_ip",
        "IP",
        "rt/network/state",
        "{{ value_json.ip | default('', true) }}",
        "diagnostic",
        "mdi:ip-network-outline",
        nullptr,
        false,
        kWifiAvailabilityTpl,
        true
    };
    const HASensorEntry wifiMac{
        "wifi",
        "wifi_mac",
        "MAC",
        "rt/network/state",
        "{{ value_json.mac | default('', true) }}",
        "diagnostic",
        "mdi:card-account-details-outline",
        nullptr,
        false,
        kWifiAvailabilityTpl,
        true
    };
    const HASensorEntry wifiRssi{
        "wifi",
        "wifi_rssi",
        "WiFi RSSI",
        "rt/network/state",
        "{{ value_json.rssi | int(-127) }}",
        "diagnostic",
        "mdi:wifi",
        "dBm",
        false,
        kWifiAvailabilityTpl,
        false
    };
    const HASensorEntry supervisorIp{
        "wifi",
        "supervisor_ip",
        "Supervisor IP",
        "rt/network/state",
        "{{ value_json.sup_ip | default('', true) }}",
        "diagnostic",
        "mdi:lan-connect",
        nullptr,
        false,
        kWifiAvailabilityTpl,
        true
    };

    bool ok = true;
    ok = haSvc_->addSensor(haSvc_->ctx, &wifiIp) && ok;
    ok = haSvc_->addSensor(haSvc_->ctx, &wifiMac) && ok;
    ok = haSvc_->addSensor(haSvc_->ctx, &wifiRssi) && ok;
    ok = haSvc_->addSensor(haSvc_->ctx, &supervisorIp) && ok;
    if (ok) {
        haEntitiesRegistered_ = true;
    } else {
        LOGW("HA registration failed: wifi entities");
    }
}

void WifiModule::init(ConfigStore& cfg,
                      ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Wifi;
    constexpr uint8_t kCfgBranchId = kWifiCfgBranch;
    /// récupérer service loghub (log async)
    logHub = services.get<LogHubService>(ServiceId::LogHub);
    cfgStore_ = &cfg;

    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore = dsSvc ? dsSvc->store : nullptr;
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    const EventBusService* eventBusSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = eventBusSvc ? eventBusSvc->bus : nullptr;

    /// Register config vars
    cfg.registerVar(enabledVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(ssidVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(passVar, kCfgModuleId, kCfgBranchId);

    /// Register WifiService
    if (!services.add(ServiceId::Wifi, &wifiSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Wifi));
    }
    if (eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &WifiModule::onEventStatic_, this);
    }

    const CommandService* cmdSvc = services.get<CommandService>(ServiceId::Command);
    if (cmdSvc && cmdSvc->registerHandler) {
        const bool ok = cmdSvc->registerHandler(cmdSvc->ctx, "wifi.dump_cfg", &WifiModule::cmdDumpCfg_, this);
        if (!ok) {
            LOGW("wifi.dump_cfg registration failed");
        } else {
            LOGD("Command registered: wifi.dump_cfg");
        }
    } else {
        LOGW("Command service unavailable: wifi.dump_cfg not registered");
    }

    // Keep WiFi credentials managed by ConfigStore only (no duplicate driver persistence).
    WiFi.persistent(false);
    // Keep retries owned by WifiModule state machine (avoid overlap with internal auto-reconnect).
    WiFi.setAutoReconnect(false);
    gWifiModuleInstance = this;
    hadSuccessfulConnection_ = false;
    initialConnectNotBeforeMs_ = isBlank_(cfgData.ssid, sizeof(cfgData.ssid))
        ? 0U
        : millis() + kInitialConnectDelayMs;
    startupTransientLogUntilMs_ = millis() + kStartupTransientLogWindowMs;
    if (wifiEventHandlerId_ != 0U) {
        WiFi.removeEvent(wifiEventHandlerId_);
        wifiEventHandlerId_ = 0U;
    }
    wifiEventHandlerId_ = WiFi.onEvent(WifiModule::onWifiEventSys_);

    LOGD("WifiService registered");
    setState(WifiState::Idle);
}

void WifiModule::onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services)
{
    refreshEthernetConfig_(cfg);
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    loadSystemDeviceName_();
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kWifiCfgProducerId,
                               kWifiCfgRoutes,
                               (uint8_t)(sizeof(kWifiCfgRoutes) / sizeof(kWifiCfgRoutes[0])),
                               services);
    }
    registerHaEntities_(services);

    cfgData.ssid[sizeof(cfgData.ssid) - 1U] = '\0';
    cfgData.pass[sizeof(cfgData.pass) - 1U] = '\0';
    logConfigSummary_();
    if (!cfgData.enabled) {
        LOGW("WiFi disabled in config, disconnecting STA");
        if (WiFi.isConnected()) {
            WiFi.disconnect(false, false);
        }
        setState(WifiState::Disabled);
        return;
    }
    if (ethernetEnabled_ && !isBlank_(cfgData.ssid, sizeof(cfgData.ssid))) {
        initialConnectNotBeforeMs_ = millis() + ETH_TIMEOUT_MS;
        LOGI("[NET] Ethernet preferred; WiFi STA fallback armed after %lu ms",
             (unsigned long)ETH_TIMEOUT_MS);
    } else {
        initialConnectNotBeforeMs_ = isBlank_(cfgData.ssid, sizeof(cfgData.ssid))
            ? 0U
            : millis() + kInitialConnectDelayMs;
    }
    startupTransientLogUntilMs_ = millis() + kStartupTransientLogWindowMs;
    setState(WifiState::Idle);
}

void WifiModule::refreshEthernetConfig_(ConfigStore& cfg)
{
    ethernetEnabled_ = false;
    char ethJson[96] = {0};
    if (!cfg.toJsonModule("ethernet", ethJson, sizeof(ethJson), nullptr)) return;

    StaticJsonDocument<96> doc;
    if (deserializeJson(doc, ethJson) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) {
        return;
    }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    ethernetEnabled_ = root["enabled"] | false;
}

bool WifiModule::preferredEthernetAvailable_() const
{
    if (!ethernetEnabled_) return false;
    if (WiFi.isConnected()) return false;
    if (!netAccessSvc_ || !netAccessSvc_->mode) return false;
    return netAccessSvc_->mode(netAccessSvc_->ctx) == NetworkAccessMode::Station;
}

void WifiModule::onEventStatic_(const Event& e, void* user)
{
    WifiModule* self = static_cast<WifiModule*>(user);
    if (self) self->onEvent_(e);
}

void WifiModule::onEvent_(const Event& e)
{
    if (e.id != EventId::ConfigChanged) return;
    if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;

    const ConfigChangedPayload* p = static_cast<const ConfigChangedPayload*>(e.payload);
    if (p->moduleId != (uint8_t)ConfigModuleId::System) return;
    if (strcmp(p->nvsKey, NvsKeys::System::DeviceName) != 0) return;
    deviceNameDirty_ = true;
}

void WifiModule::loadSystemDeviceName_()
{
    char next[sizeof(deviceName_)] = "flowio";
    if (cfgStore_) {
        char systemJson[128] = {0};
        if (cfgStore_->toJsonModule("system", systemJson, sizeof(systemJson), nullptr, false)) {
            StaticJsonDocument<128> doc;
            if (deserializeJson(doc, systemJson) == DeserializationError::Ok && doc.is<JsonObjectConst>()) {
                const char* configured = doc.as<JsonObjectConst>()["devicename"] | "";
                if (!isBlank_(configured, sizeof(deviceName_))) {
                    snprintf(next, sizeof(next), "%s", configured);
                }
            }
        }
    }
    next[sizeof(next) - 1U] = '\0';
    if (strncmp(deviceName_, next, sizeof(deviceName_)) == 0) return;
    snprintf(deviceName_, sizeof(deviceName_), "%s", next);
    LOGI("System device name loaded: %s", deviceName_);
    if (mdnsStarted) {
        stopMdns_();
    }
}

void WifiModule::stopMdns_()
{
    if (!mdnsStarted) return;
    MDNS.end();
    mdnsStarted = false;
    mdnsApplied[0] = '\0';
    LOGD("mDNS stopped");
}

void WifiModule::syncMdns_()
{
    if (!WiFi.isConnected()) {
        stopMdns_();
        return;
    }

    char host[sizeof(deviceName_)] = {0};
    formatHostname_(deviceName_, host, sizeof(host));

    if (mdnsStarted && strcmp(mdnsApplied, host) == 0) return;

    if (mdnsStarted) {
        MDNS.end();
        mdnsStarted = false;
        mdnsApplied[0] = '\0';
    }

    if (!MDNS.begin(host)) {
        LOGW("mDNS start failed host=%s", host);
        return;
    }

    mdnsStarted = true;
    snprintf(mdnsApplied, sizeof(mdnsApplied), "%s", host);
    LOGD("mDNS started host=%s.local", mdnsApplied);
}

void WifiModule::loop() {
    if (deviceNameDirty_) {
        deviceNameDirty_ = false;
        loadSystemDeviceName_();
    }

    processScan_();

    switch (state) {

    case WifiState::Disabled:
        vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::DisabledLoopDelayMs));
        break;

    case WifiState::Idle:
        if (!staRetryEnabled_) {
            vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::IdleRetryDisabledLoopDelayMs));
            break;
        }
        if (preferredEthernetAvailable_()) {
            vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::IdleConnectPollDelayMs));
            break;
        }
        if (initialConnectNotBeforeMs_ != 0U && millis() < initialConnectNotBeforeMs_) {
            vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::IdleConnectPollDelayMs));
            break;
        }
        startConnect();
        vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::IdlePostConnectDelayMs));
        break;

    case WifiState::Connecting:
    {
        if (!staRetryEnabled_ && !WiFi.isConnected()) {
            setState(WifiState::Idle);
            vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::IdleConnectPollDelayMs));
            break;
        }
        const wl_status_t wl = WiFi.status();
        const uint32_t now = millis();
        if (wl != lastConnectStatus_) {
            lastConnectStatus_ = wl;
        }

        if ((now - lastConnectingLogMs_) >= Limits::Wifi::Timing::ConnectingLogIntervalMs) {
            lastConnectingLogMs_ = now;
            const int rssi = WiFi.isConnected() ? WiFi.RSSI() : -127;
            const char* wlName = wlStatusName_(wl);
            const char* reasonName = WiFi.disconnectReasonName((wifi_err_reason_t)lastDisconnectReason_);
            LOGD("Connecting status=%s(%d) rssi=%d last_reason=%u(%s) elapsed_ms=%lu",
                 wlName,
                 (int)wl,
                 rssi,
                 (unsigned)lastDisconnectReason_,
                 reasonName ? reasonName : "?",
                 (unsigned long)(now - stateTs));
        }

        // Keep connection state machine simple under repeated "AP not found":
        // rely on begin() + timeout retry cycle, avoid manual reconnect kicks.

        if (WiFi.isConnected()) {
            IPAddress ip = WiFi.localIP();
            LOGI("Connected IP=%u.%u.%u.%u RSSI=%d",
            ip[0], ip[1], ip[2], ip[3],
            WiFi.RSSI());
            setState(WifiState::Connected);
        }
        else if ((now - stateTs) > Limits::Wifi::Timing::ConnectTimeoutMs) {
            const char* wlName = wlStatusName_(wl);
            if (lastDisconnectReason_ != 0U) {
                const char* reasonName = WiFi.disconnectReasonName((wifi_err_reason_t)lastDisconnectReason_);
                LOGW("Connect timeout status=%s(%d) reason=%u(%s)",
                     wlName,
                     (int)wl,
                     (unsigned)lastDisconnectReason_,
                     reasonName ? reasonName : "?");
            } else {
                LOGW("Connect timeout status=%s(%d)", wlName, (int)wl);
            }
            setState(WifiState::ErrorWait);
        }
        vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::ConnectingLoopDelayMs));
        break;
    }

    case WifiState::Connected:
        if (!WiFi.isConnected()) {
            LOGW("Disconnected");
            setState(WifiState::ErrorWait);
        }
        if (state == WifiState::Connected) {
            syncMdns_();
        }
        if (state == WifiState::Connected && !gotIpSent) {
            IPAddress ip = WiFi.localIP();
            if (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0) {
                if (dataStore) {
                    IpV4 ip4{};
                    ip4.b[0] = ip[0];
                    ip4.b[1] = ip[1];
                    ip4.b[2] = ip[2];
                    ip4.b[3] = ip[3];

                    setWifiIp(*dataStore, ip4);
                    setWifiReady(*dataStore, true);
                }
                gotIpSent = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::ConnectedLoopDelayMs));
        break;

    case WifiState::ErrorWait:
        if (!staRetryEnabled_) {
            vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::ErrorWaitLoopDelayMs));
            break;
        }
        if ((millis() - stateTs) > Limits::Wifi::Timing::ErrorWaitDurationMs) {
            setState(WifiState::Idle);
        }
        vTaskDelay(pdMS_TO_TICKS(Limits::Wifi::Timing::ErrorWaitLoopDelayMs));
        break;
    }
}
