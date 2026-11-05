/**
 * @file WifiProvisioningModule.cpp
 * @brief WiFi provisioning overlay implementation.
 */

#include "WifiProvisioningModule.h"

#include "App/BuildFlags.h"
#include "Core/FirmwareVersion.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::WifiProvisioningModule)
#include "Core/ModuleLog.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <string.h>
#include <strings.h>

namespace {
constexpr const char* kDefaultApPass = "flowio1234";
WifiProvisioningModule* gWifiProvisioningInstance = nullptr;
constexpr wifi_auth_mode_t kProvisioningApAuthMode = WIFI_AUTH_WPA2_PSK;
constexpr wifi_cipher_type_t kProvisioningApCipher = WIFI_CIPHER_TYPE_CCMP;
constexpr uint8_t kProvisioningApChannel = 1U;
constexpr uint8_t kProvisioningApMaxConnections = 2U;

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

const char* wifiAuthName_(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
#if defined(WIFI_AUTH_WPA2_ENTERPRISE)
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
#endif
#if defined(WIFI_AUTH_WPA3_PSK)
    case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
#endif
#if defined(WIFI_AUTH_WPA2_WPA3_PSK)
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
#endif
#if defined(WIFI_AUTH_WAPI_PSK)
    case WIFI_AUTH_WAPI_PSK: return "WAPI_PSK";
#endif
    default: return "?";
    }
}

const char* wifiCipherName_(wifi_cipher_type_t cipher)
{
    switch (cipher) {
    case WIFI_CIPHER_TYPE_NONE: return "NONE";
    case WIFI_CIPHER_TYPE_WEP40: return "WEP40";
    case WIFI_CIPHER_TYPE_WEP104: return "WEP104";
    case WIFI_CIPHER_TYPE_TKIP: return "TKIP";
    case WIFI_CIPHER_TYPE_CCMP: return "CCMP";
#if defined(WIFI_CIPHER_TYPE_TKIP_CCMP)
    case WIFI_CIPHER_TYPE_TKIP_CCMP: return "TKIP_CCMP";
#endif
#if defined(WIFI_CIPHER_TYPE_AES_CMAC128)
    case WIFI_CIPHER_TYPE_AES_CMAC128: return "AES_CMAC128";
#endif
    default: return "?";
    }
}
}

void WifiProvisioningModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;
    services_ = &services;
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    hmiSvc_ = services.get<HmiService>(ServiceId::Hmi);
    const EventBusService* eventBusSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = eventBusSvc ? eventBusSvc->bus : nullptr;
    if (eventBus_) {
        eventBus_->subscribe(EventId::NetworkShutdownPending, &WifiProvisioningModule::onEventStatic_, this);
    }
    bootMs_ = millis();
    networkManager_.begin(bootMs_);
    lastCfgPollMs_ = 0;
    buildApCredentials_();

    gWifiProvisioningInstance = this;
    if (wifiEventHandlerId_ != 0U) {
        WiFi.removeEvent(wifiEventHandlerId_);
        wifiEventHandlerId_ = 0U;
    }
    wifiEventHandlerId_ = WiFi.onEvent(WifiProvisioningModule::onWifiEventSys_);

    LOGI("[NET] Provisioning overlay initialized (eth_timeout=%lu ms, wifi_timeout=%lu ms, AP SSID=%s)",
         (unsigned long)ETH_TIMEOUT_MS,
         (unsigned long)WIFI_TIMEOUT_MS,
         apSsid_);
}

void WifiProvisioningModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    services_ = &services;
    hmiSvc_ = services.get<HmiService>(ServiceId::Hmi);
    refreshWifiConfig_();
    observedNetAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    if (!ethernetEnabled_ && !services.has(ServiceId::NetworkAccess)) {
        if (!services.add(ServiceId::NetworkAccess, &netAccessSvc_)) {
            LOGE("service registration failed: %s", toString(ServiceId::NetworkAccess));
        }
        observedNetAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    }
    syncNetworkManagerState_();
    LOGI("[NET] Provisioning config loaded: ethernet=%d wifi_enabled=%d wifi_configured=%d ssid_len=%u pass_len=%u eth_timeout_ms=%lu wifi_timeout_ms=%lu",
         (int)ethernetEnabled_,
         (int)wifiEnabled_,
         (int)wifiConfigured_,
         (unsigned)wifiSsidLen_,
         (unsigned)wifiPassLen_,
         (unsigned long)ETH_TIMEOUT_MS,
         (unsigned long)WIFI_TIMEOUT_MS);
#if !defined(FLOW_PROFILE_MICRONOVA)
    ensurePortalStarted_();
#endif
}

void WifiProvisioningModule::onStart(ConfigStore&, ServiceRegistry&)
{
#if defined(FLOW_PROFILE_MICRONOVA)
    ensurePortalStarted_();
#endif
}

void WifiProvisioningModule::loop()
{
    uint32_t now = millis();
    if (apStartDuringStartEventPending_) {
        apStartDuringStartEventPending_ = false;
        LOGD("Provisioning AP start event during setup mode=%s", wifiModeName_(WiFi.getMode()));
    }
    if (apStopDuringStartEventPending_) {
        apStopDuringStartEventPending_ = false;
        LOGD("Provisioning AP stop event during setup mode=%s", wifiModeName_(WiFi.getMode()));
    }
    if (apProbeEventCount_ != 0U) {
        const uint32_t probeCount = apProbeEventCount_;
        const int probeRssi = apProbeLastRssi_;
        apProbeEventCount_ = 0;
        apProbeCount_ += probeCount;
        if ((now - lastApProbeLogMs_) >= 2000U) {
            lastApProbeLogMs_ = now;
            LOGI("AP probe activity probes=%lu clients=%u rssi=%d",
                 (unsigned long)apProbeCount_,
                 (unsigned)WiFi.softAPgetStationNum(),
                 probeRssi);
        }
    }
    if (apClientConnectedEventPending_) {
        apClientConnectedEventPending_ = false;
        LOGI("AP client connected count=%u", (unsigned)WiFi.softAPgetStationNum());
    }
    if (apClientDisconnectedEventPending_) {
        const uint8_t reason = apClientDisconnectReason_;
        apClientDisconnectedEventPending_ = false;
        const char* reasonName = WiFi.disconnectReasonName((wifi_err_reason_t)reason);
        LOGW("AP client disconnected reason=%u(%s) count=%u",
             (unsigned)reason,
             reasonName ? reasonName : "?",
             (unsigned)WiFi.softAPgetStationNum());
    }
    if (apClientRefreshPending_) {
        apClientRefreshPending_ = false;
        refreshApClientState_(now, true);
    }
    if (apStopEventPending_) {
        apStopEventPending_ = false;
        if (shutdownPending_) {
            LOGI("Provisioning AP stop observed during shutdown; restart suppressed");
        } else if (apActive_) {
            LOGE("Provisioning AP stopped unexpectedly; scheduling restart mode=%s wl=%d",
                 wifiModeName_(WiFi.getMode()),
                 (int)WiFi.status());
            apRestartPending_ = true;
        }
    }
    if (apRestartPending_) {
        apRestartPending_ = false;
        dns_.stop();
#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
        stopLightPortal_();
#endif
        apActive_ = false;
        networkManager_.setCaptivePortalRunning(false);
        setHmiCaptivePortalCondition_(false);
        portalLatched_ = false;
        apClientCount_ = 0;
        nextApStartAttemptMs_ = millis() + kApStartRetryMs;
    }
#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
    if (portalRebootPending_ && (int32_t)(now - portalRebootAtMs_) >= 0) {
        LOGI("Provisioning reboot now");
        delay(20);
        esp_restart();
    }
#endif
    if (configDirty_ || (now - lastCfgPollMs_) >= kConfigPollMs) {
        lastCfgPollMs_ = now;
        configDirty_ = false;
        refreshWifiConfig_();
    }
    syncNetworkManagerState_();
    const bool forceHmiPortalSync = apActive_ &&
        ((uint32_t)(now - lastHmiCaptivePortalSyncMs_) >= kHmiPortalResyncMs);
    setHmiCaptivePortalCondition_(apActive_, forceHmiPortalSync);

    if (hasStationNetwork_()) {
        if (apActive_) {
            stopCaptivePortal_(networkManager_.hasEthernetIP()
                                   ? "Ethernet is available"
                                   : "STA connected");
        }
        networkManager_.setNormalServicesStarted(true);
        portalLatched_ = false;
        vTaskDelay(pdMS_TO_TICKS(250));
        return;
    }

    ensurePortalStarted_();

    if (apActive_) {
        // Refresh timestamp after potential AP start delays to avoid stale-now
        // wraparound in probe interval checks.
        now = millis();
        handleStaProbePolicy_(now);
        dns_.processNextRequest();
#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
        handleLightPortalClient_();
#endif
    }

    vTaskDelay(pdMS_TO_TICKS(20));
}

void WifiProvisioningModule::ensurePortalStarted_()
{
    if (apActive_ || portalLatched_) return;
    syncNetworkManagerState_();
    if (networkManager_.hasNetwork()) return;

    const NetworkPortalReason reason = evaluatePortalReason_();
    if (reason == NetworkPortalReason::None) return;

    if (startCaptivePortal_(reason)) {
        portalLatched_ = true;
    }
}

bool WifiProvisioningModule::isWebReachable_() const
{
    return isStaConnected_() || apActive_;
}

NetworkAccessMode WifiProvisioningModule::mode_() const
{
    if (isStaConnected_()) return NetworkAccessMode::Station;
    if (apActive_) return NetworkAccessMode::AccessPoint;
    return NetworkAccessMode::None;
}

bool WifiProvisioningModule::getIP_(char* out, size_t len) const
{
    if (!out || len == 0) return false;
    if (isStaConnected_()) {
        return getStaIp_(out, len);
    }
    if (apActive_) {
        return getApIp_(out, len);
    }
    out[0] = '\0';
    return false;
}

bool WifiProvisioningModule::notifyWifiConfigChanged_()
{
    if (shutdownPending_) return true;
    configDirty_ = true;
    if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
        (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, true);
    }
    if (wifiSvc_ && wifiSvc_->requestReconnect) {
        wifiSvc_->requestReconnect(wifiSvc_->ctx);
    }
    return true;
}

bool WifiProvisioningModule::notifyShutdownPending_()
{
    shutdownPending_ = true;
    apRestartPending_ = false;
    apStopEventPending_ = false;
    portalLatched_ = false;
    if (apActive_) {
        LOGI("Provisioning shutdown pending; AP restart disabled");
    }
    return true;
}

void WifiProvisioningModule::onEventStatic_(const Event& e, void* user)
{
    WifiProvisioningModule* self = static_cast<WifiProvisioningModule*>(user);
    if (self) self->onEvent_(e);
}

void WifiProvisioningModule::onEvent_(const Event& e)
{
    if (e.id != EventId::NetworkShutdownPending) return;
    (void)notifyShutdownPending_();
}

void WifiProvisioningModule::buildApCredentials_()
{
    const uint64_t chipId = ESP.getEfuseMac();
    //const uint8_t b0 = (uint8_t)(chipId >> 16);
    //const uint8_t b1 = (uint8_t)(chipId >> 8);
    //const uint8_t b2 = (uint8_t)(chipId >> 0);
    //snprintf(apSsid_, sizeof(apSsid_), "flow.io-%02X%02X%02X", b0, b1, b2);
    uint64_t id=0;
    for(int i=0; i<17; i=i+8) id |= ((chipId >> (40 - i)) & 0xff) << i;
    snprintf(apSsid_, sizeof(apSsid_), "flow.io-%06X", id);
    snprintf(apPass_, sizeof(apPass_), "%s", kDefaultApPass);
}

void WifiProvisioningModule::refreshWifiConfig_()
{
    if (!cfgStore_) return;

    ethernetEnabled_ = false;
    char ethernetJson[96] = {0};
    if (cfgStore_->toJsonModule("ethernet", ethernetJson, sizeof(ethernetJson), nullptr)) {
        StaticJsonDocument<96> ethDoc;
        if (deserializeJson(ethDoc, ethernetJson) == DeserializationError::Ok && ethDoc.is<JsonObjectConst>()) {
            JsonObjectConst ethRoot = ethDoc.as<JsonObjectConst>();
            ethernetEnabled_ = ethRoot["enabled"] | false;
        }
    }

    if (ethernetEnabled_) {
#if defined(FLOW_PROFILE_WAVESHARE)
        fastPortalStart_ = false;
#endif
    }

    char wifiJson[320] = {0};
    if (!cfgStore_->toJsonModule("wifi", wifiJson, sizeof(wifiJson), nullptr)) {
        wifiConfigured_ = false;
        wifiEnabled_ = true;
        wifiSsidLen_ = 0;
        wifiPassLen_ = 0;
#if defined(FLOW_PROFILE_WAVESHARE)
        fastPortalStart_ = !ethernetEnabled_;
#endif
        networkManager_.updateConfig(ethernetEnabled_, wifiEnabled_, wifiConfigured_);
        return;
    }

    StaticJsonDocument<320> doc;
    const DeserializationError err = deserializeJson(doc, wifiJson);
    if (err || !doc.is<JsonObjectConst>()) {
        LOGW("Cannot parse wifi config for provisioning");
        wifiConfigured_ = false;
        wifiEnabled_ = true;
        wifiSsidLen_ = 0;
        wifiPassLen_ = 0;
#if defined(FLOW_PROFILE_WAVESHARE)
        fastPortalStart_ = !ethernetEnabled_;
#endif
        networkManager_.updateConfig(ethernetEnabled_, wifiEnabled_, wifiConfigured_);
        return;
    }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* ssid = root["ssid"] | "";
    const char* pass = root["pass"] | "";
    wifiEnabled_ = root["enabled"] | true;
    wifiSsidLen_ = (uint8_t)strnlen(ssid ? ssid : "", 32U);
    wifiPassLen_ = (uint8_t)strnlen(pass ? pass : "", 64U);
    wifiConfigured_ = wifiEnabled_ && ssid && ssid[0] != '\0';
#if defined(FLOW_PROFILE_WAVESHARE)
    fastPortalStart_ = !ethernetEnabled_ && wifiEnabled_ && !wifiConfigured_;
#endif
    networkManager_.updateConfig(ethernetEnabled_, wifiEnabled_, wifiConfigured_);
}

NetworkPortalReason WifiProvisioningModule::evaluatePortalReason_() const
{
    return networkManager_.portalReason(millis());
}

bool WifiProvisioningModule::startCaptivePortal_(NetworkPortalReason reason)
{
    if (apActive_) return true;
    syncNetworkManagerState_();
    if (networkManager_.hasNetwork()) return false;

    const uint32_t now = millis();
    if ((int32_t)(now - nextApStartAttemptMs_) < 0) {
        return false;
    }
    const uint32_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if ((now - lastApStartPrecheckLogMs_) >= kApStartLogIntervalMs) {
        lastApStartPrecheckLogMs_ = now;
        LOGI("Provisioning AP start precheck mode=%s wl=%d internal_free=%lu largest_internal=%lu",
             wifiModeName_(WiFi.getMode()),
             (int)WiFi.status(),
             (unsigned long)internalFree,
             (unsigned long)internalLargest);
    }
    const bool lowHeap = internalLargest < kApStartMinLargestInternalBytes;
    if (lowHeap) {
        ++apStartDeferredCount_;
        const bool forcedAttempt = (apStartDeferredCount_ >= kApStartForceAfterDefers);
        if (!forcedAttempt) {
            nextApStartAttemptMs_ = now + kApStartRetryMs;
        }
        if ((now - lastApStartDeferredLogMs_) >= kApStartLogIntervalMs) {
            lastApStartDeferredLogMs_ = now;
            LOGE("Provisioning AP start deferred: internal heap too low largest=%lu threshold=%lu deferred=%u",
                 (unsigned long)internalLargest,
                 (unsigned long)kApStartMinLargestInternalBytes,
                 (unsigned)apStartDeferredCount_);
        }
        if (!forcedAttempt) {
            return false;
        }
        LOGW("Provisioning AP forcing low-heap start largest=%lu deferred=%u threshold=%lu",
             (unsigned long)internalLargest,
             (unsigned)apStartDeferredCount_,
             (unsigned long)kApStartMinLargestInternalBytes);
    } else {
        apStartDeferredCount_ = 0;
    }

    if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
        (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, false);
    }
    if (wifiSvc_ && wifiSvc_->requestReconnect) {
        (void)wifiSvc_->requestReconnect(wifiSvc_->ctx);
    }
    delay(80);
    (void)WiFi.disconnect(false, false);
    (void)WiFi.scanDelete();

    // Force strict AP mode during provisioning: avoids STA scans/channel moves
    // that can break client association on some phones.
    (void)WiFi.enableSTA(false);
    const uint32_t staStopWaitStartMs = millis();
    while ((WiFi.getMode() & WIFI_MODE_STA) != 0 &&
           (millis() - staStopWaitStartMs) < kStaStopWaitMs) {
        delay(20);
    }
    if ((WiFi.getMode() & WIFI_MODE_STA) != 0) {
        LOGW("Provisioning AP start: STA still enabled after wait mode=%s wl=%d",
             wifiModeName_(WiFi.getMode()),
             (int)WiFi.status());
    }

    // Reset WiFi mode cleanly before softAP start, then expose the AP only
    // after Arduino/IDF have finished their setup events.
    const wifi_mode_t modeBefore = WiFi.getMode();
    if (modeBefore != WIFI_MODE_NULL) {
        const bool nullOk = WiFi.mode(WIFI_MODE_NULL);
        if (!nullOk) {
            LOGW("Provisioning AP reset to NULL failed current=%s", wifiModeName_(WiFi.getMode()));
        }
        delay(kModeResetSettleMs);
    }
    apStarting_ = true;
    const bool ok = WiFi.softAP(apSsid_,
                                apPass_,
                                kProvisioningApChannel,
                                0,
                                kProvisioningApMaxConnections,
                                false,
                                kProvisioningApAuthMode,
                                kProvisioningApCipher);
    if (!ok) {
        apStarting_ = false;
        LOGE("Cannot start AP portal");
        nextApStartAttemptMs_ = millis() + kApStartRetryMs;
        return false;
    }

    delay(kApStartStableDelayMs);
    apStarting_ = false;
    const wifi_mode_t modeAfterStart = WiFi.getMode();
    if ((modeAfterStart & WIFI_MODE_AP) == 0) {
        LOGE("Provisioning AP start unstable: AP mode missing after settle mode=%s wl=%d",
             wifiModeName_(modeAfterStart),
             (int)WiFi.status());
        nextApStartAttemptMs_ = millis() + kApStartRetryMs;
        return false;
    }

    wifi_config_t apCfg{};
    const esp_err_t apCfgErr = esp_wifi_get_config(WIFI_IF_AP, &apCfg);
    if (apCfgErr == ESP_OK) {
        char appliedSsid[33] = {0};
        const size_t apSsidLen = (size_t)((apCfg.ap.ssid_len <= 32U) ? apCfg.ap.ssid_len : 32U);
        memcpy(appliedSsid, apCfg.ap.ssid, apSsidLen);
        appliedSsid[apSsidLen] = '\0';
        const size_t appliedPassLen = strnlen((const char*)apCfg.ap.password, sizeof(apCfg.ap.password));
        LOGI("Provisioning AP config applied ssid='%s' ssid_len=%u pass_len=%u auth=%s(%u) cipher=%s(%u) ch=%u hidden=%u max_conn=%u",
             appliedSsid,
             (unsigned)apCfg.ap.ssid_len,
             (unsigned)appliedPassLen,
             wifiAuthName_(apCfg.ap.authmode),
             (unsigned)apCfg.ap.authmode,
             wifiCipherName_((wifi_cipher_type_t)apCfg.ap.pairwise_cipher),
             (unsigned)apCfg.ap.pairwise_cipher,
             (unsigned)apCfg.ap.channel,
             (unsigned)apCfg.ap.ssid_hidden,
             (unsigned)apCfg.ap.max_connection);
    } else {
        LOGE("Provisioning AP config read failed err=%d", (int)apCfgErr);
    }

    const IPAddress apIp = WiFi.softAPIP();
    if (apIp[0] == 0 && apIp[1] == 0 && apIp[2] == 0 && apIp[3] == 0) {
        LOGE("Provisioning AP start unstable: AP IP is 0.0.0.0");
        nextApStartAttemptMs_ = millis() + kApStartRetryMs;
        return false;
    }
    dns_.start(kDnsPort, "*", apIp);
#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
    startLightPortal_();
    portalCredentialsSaved_ = false;
#endif
    apActive_ = true;
    networkManager_.setCaptivePortalRunning(true);
    setHmiCaptivePortalCondition_(true);
    staProbeActive_ = false;
    apClientEverSeen_ = false;
    lastStaProbeStartMs_ = millis();
    nextApStartAttemptMs_ = 0;
    apStartDeferredCount_ = 0;
    refreshApClientState_(lastStaProbeStartMs_, false);

    const char* reasonTxt = (reason == NetworkPortalReason::MissingCredentials) ? "missing credentials" : "connect timeout";
    LOGW("[NET] No network available, starting captive portal (%s) SSID=%s pass_len=%u auth=%s cipher=%s channel=%u max_conn=%u IP=%u.%u.%u.%u",
         reasonTxt,
         apSsid_,
         (unsigned)strlen(apPass_),
         wifiAuthName_(kProvisioningApAuthMode),
         wifiCipherName_(kProvisioningApCipher),
         (unsigned)kProvisioningApChannel,
         (unsigned)kProvisioningApMaxConnections,
         apIp[0], apIp[1], apIp[2], apIp[3]);
    return true;
}

void WifiProvisioningModule::stopCaptivePortal_(const char* reason)
{
    if (!apActive_) return;

    stopStaProbe_(reason ? reason : "network available");
    if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
        (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, true);
    }
#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
    stopLightPortal_();
#endif
    dns_.stop();
    WiFi.softAPdisconnect(true);
    apActive_ = false;
    apClientCount_ = 0;
    lastApClientSeenMs_ = 0;
    lastApClientPollMs_ = 0;
    networkManager_.setCaptivePortalRunning(false);
    setHmiCaptivePortalCondition_(false);
    LOGI("[NET] Captive portal stopped because %s", reason ? reason : "network is available");
}

void WifiProvisioningModule::onWifiEventSys_(arduino_event_t* event)
{
    if (!event) return;
    WifiProvisioningModule* self = gWifiProvisioningInstance;
    if (!self) return;
    self->onWifiEvent_(event);
}

void WifiProvisioningModule::onWifiEvent_(arduino_event_t* event)
{
    if (!event) return;
    switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_AP_START:
        if (apStarting_) {
            apStartDuringStartEventPending_ = true;
        }
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        if (apStarting_) {
            apStopDuringStartEventPending_ = true;
        } else if (apActive_) {
            apStopEventPending_ = true;
        }
        break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED: {
        apProbeEventCount_ = apProbeEventCount_ + 1U;
        apProbeLastRssi_ = (int)event->event_info.wifi_ap_probereqrecved.rssi;
        break;
    }
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        apClientConnectedEventPending_ = true;
        apClientRefreshPending_ = true;
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
        const uint8_t reason = (uint8_t)event->event_info.wifi_ap_stadisconnected.reason;
        apClientDisconnectReason_ = reason;
        apClientDisconnectedEventPending_ = true;
        apClientRefreshPending_ = true;
        break;
    }
    default:
        break;
    }
}

void WifiProvisioningModule::refreshApClientState_(uint32_t nowMs, bool fromEvent)
{
    if (!apActive_) {
        apClientCount_ = 0;
        return;
    }

    const uint8_t count = WiFi.softAPgetStationNum();
    if (count > 0) {
        lastApClientSeenMs_ = nowMs;
        apClientEverSeen_ = true;
    }
    if (count != apClientCount_) {
        apClientCount_ = count;
        if (fromEvent) {
            LOGI("AP clients changed (event) count=%u", (unsigned)apClientCount_);
        } else {
            LOGI("AP clients changed (poll) count=%u", (unsigned)apClientCount_);
        }
    }
}

void WifiProvisioningModule::startStaProbe_(uint32_t nowMs)
{
    if (staProbeActive_) return;
    if (!wifiEnabled_ || !wifiConfigured_) return;
    if (!apClientEverSeen_) return;

    staProbeActive_ = true;
    lastStaProbeStartMs_ = nowMs;
    if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
        (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, true);
    }
    if (wifiSvc_ && wifiSvc_->requestReconnect) {
        (void)wifiSvc_->requestReconnect(wifiSvc_->ctx);
    }
    LOGI("STA probe started window_ms=%lu ap_clients=%u",
         (unsigned long)kStaProbeWindowMs,
         (unsigned)apClientCount_);
}

void WifiProvisioningModule::stopStaProbe_(const char* reason)
{
    if (!staProbeActive_) return;
    staProbeActive_ = false;

    if (!isStaConnected_()) {
        if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
            (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, false);
        }
        if (wifiSvc_ && wifiSvc_->requestReconnect) {
            (void)wifiSvc_->requestReconnect(wifiSvc_->ctx);
        }
        delay(120);
        if (apActive_ && WiFi.getMode() != WIFI_MODE_AP) {
            LOGI("STA probe forcing AP mode from mode=%s wl=%d",
                 wifiModeName_(WiFi.getMode()),
                 (int)WiFi.status());
            (void)WiFi.mode(WIFI_MODE_AP);
        }
    }

    LOGI("STA probe stopped reason=%s", reason ? reason : "unknown");
}

void WifiProvisioningModule::handleStaProbePolicy_(uint32_t nowMs)
{
    if (!apActive_) return;
    (void)nowMs;
    // Keep provisioning AP in strict AP-only mode until user credentials are
    // updated; do not probe STA in background.
    return;
}

bool WifiProvisioningModule::isStaConnected_() const
{
    if (!wifiSvc_ || !wifiSvc_->isConnected) return false;
    return wifiSvc_->isConnected(wifiSvc_->ctx);
}

bool WifiProvisioningModule::hasStationNetwork_() const
{
    if (isStaConnected_()) return true;
    const NetworkAccessService* net = observedNetAccessSvc_;
    if (!net || !net->mode) return false;
    return net->mode(net->ctx) == NetworkAccessMode::Station;
}

void WifiProvisioningModule::syncNetworkManagerState_()
{
    const bool wifiHasIP = isStaConnected_();
    bool stationNetwork = wifiHasIP;
    if (observedNetAccessSvc_ && observedNetAccessSvc_->mode) {
        stationNetwork = observedNetAccessSvc_->mode(observedNetAccessSvc_->ctx) == NetworkAccessMode::Station;
    }
    const bool ethHasIP = ethernetEnabled_ && stationNetwork && !wifiHasIP;
    networkManager_.updateConfig(ethernetEnabled_, wifiEnabled_, wifiConfigured_);
    networkManager_.updateInterfaces(ethHasIP, wifiHasIP);
    networkManager_.setCaptivePortalRunning(apActive_);
}

void WifiProvisioningModule::setHmiCaptivePortalCondition_(bool active, bool force)
{
    const uint32_t now = millis();
    if (!force &&
        hmiCaptivePortalConditionSynced_ &&
        hmiCaptivePortalActive_ == active) {
        return;
    }
    if (!hmiSvc_ && services_) {
        hmiSvc_ = services_->get<HmiService>(ServiceId::Hmi);
    }
    if (!hmiSvc_) {
        hmiCaptivePortalConditionSynced_ = false;
        return;
    }
    if (hmiSvc_->setLedCondition) {
        (void)hmiSvc_->setLedCondition(hmiSvc_->ctx, HmiLedCondition::CaptivePortalActive, active);
        hmiCaptivePortalConditionSynced_ = true;
        lastHmiCaptivePortalSyncMs_ = now;
    } else {
        hmiCaptivePortalConditionSynced_ = false;
        return;
    }
    hmiCaptivePortalActive_ = active;
}

bool WifiProvisioningModule::getStaIp_(char* out, size_t len) const
{
    if (!out || len == 0) return false;
    if (!wifiSvc_ || !wifiSvc_->getIP) {
        out[0] = '\0';
        return false;
    }
    return wifiSvc_->getIP(wifiSvc_->ctx, out, len);
}

bool WifiProvisioningModule::getApIp_(char* out, size_t len) const
{
    if (!out || len == 0) return false;
    const IPAddress ip = WiFi.softAPIP();
    if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
        out[0] = '\0';
        return false;
    }
    snprintf(out, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return true;
}

#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
void WifiProvisioningModule::startLightPortal_()
{
    if (portalHttpActive_) return;
    portalSpiffsReady_ = SPIFFS.begin(false);
    if (!portalSpiffsReady_) {
        LOGW("Flow Connect Display provisioning SPIFFS mount failed; fallback page will be used");
    }
    portalServer_.begin();
    portalHttpActive_ = true;
    LOGI("Provisioning light HTTP started on port 80 spiffs=%d", (int)portalSpiffsReady_);
}

void WifiProvisioningModule::stopLightPortal_()
{
    if (!portalHttpActive_) return;
    portalServer_.stop();
    portalHttpActive_ = false;
}

void WifiProvisioningModule::handleLightPortalClient_()
{
    if (!portalHttpActive_) return;

    WiFiClient client = portalServer_.available();
    if (!client) return;

    client.setTimeout(250);
    const bool haveLine = readRequestLine_(client, portalReqLine_, sizeof(portalReqLine_));
    if (!haveLine) {
        client.stop();
        return;
    }

    const char* target = nullptr;
    if (strncmp(portalReqLine_, "GET ", 4) == 0) {
        snprintf(portalMethod_, sizeof(portalMethod_), "GET");
        target = portalReqLine_ + 4;
    } else if (strncmp(portalReqLine_, "POST ", 5) == 0) {
        snprintf(portalMethod_, sizeof(portalMethod_), "POST");
        target = portalReqLine_ + 5;
    } else if (strncmp(portalReqLine_, "HEAD ", 5) == 0) {
        snprintf(portalMethod_, sizeof(portalMethod_), "HEAD");
        target = portalReqLine_ + 5;
    }

    if (!target) {
        sendPlain_(client, "405 Method Not Allowed", "Method not allowed");
        client.stop();
        return;
    }

    size_t pathLen = 0;
    while (target[pathLen] != '\0' && target[pathLen] != ' ' && pathLen < (sizeof(portalPath_) - 1U)) {
        portalPath_[pathLen] = target[pathLen];
        ++pathLen;
    }
    portalPath_[pathLen] = '\0';

    char* query = strchr(portalPath_, '?');
    if (query) {
        *query = '\0';
        ++query;
    }

    size_t contentLen = 0U;
    while (readHeaderLine_(client, portalBody_, sizeof(portalBody_))) {
        if (portalBody_[0] == '\0') {
            break;
        }
        if (strncasecmp(portalBody_, "Content-Length:", 15) == 0) {
            const char* value = portalBody_ + 15;
            while (*value == ' ' || *value == '\t') ++value;
            contentLen = (size_t)strtoul(value, nullptr, 10);
        }
    }

    portalBody_[0] = '\0';
    if (contentLen > 0U && !readRequestBody_(client, contentLen, portalBody_, sizeof(portalBody_))) {
        sendPlain_(client, "413 Payload Too Large", "Payload too large");
        client.stop();
        return;
    }

    (void)handleLightPortalRequest_(client, portalMethod_, portalPath_, query, portalBody_);
    client.flush();
    client.stop();
}

bool WifiProvisioningModule::handleLightPortalRequest_(WiFiClient& client,
                                                       const char* method,
                                                       const char* path,
                                                       const char* query,
                                                       const char* body)
{
    const bool isGet = method && strcmp(method, "GET") == 0;
    const bool isPost = method && strcmp(method, "POST") == 0;
    const bool isHead = method && strcmp(method, "HEAD") == 0;
    if (!path || path[0] == '\0') {
        path = "/";
    }

    if ((isGet || isHead) && (strcmp(path, "/") == 0 || strcmp(path, "/webinterface") == 0 ||
                              strcmp(path, "/webinterface/") == 0 ||
                              strcmp(path, "/generate_204") == 0 ||
                              strcmp(path, "/gen_204") == 0 ||
                              strcmp(path, "/hotspot-detect.html") == 0 ||
                              strcmp(path, "/connecttest.txt") == 0 ||
                              strcmp(path, "/ncsi.txt") == 0)) {
        if (!sendSpiffsAsset_(client, "/webinterface/light.html", "text/html; charset=utf-8")) {
            sendPortalFallbackPage_(client, nullptr, false);
        }
        return true;
    }

    if ((isGet || isHead) && strcmp(path, "/webinterface/light.css") == 0) {
        if (!sendSpiffsAsset_(client, "/webinterface/light.css", "text/css; charset=utf-8")) {
            sendPlain_(client, "404 Not Found", "Not found");
        }
        return true;
    }
    if ((isGet || isHead) && strcmp(path, "/webinterface/light.js") == 0) {
        if (!sendSpiffsAsset_(client, "/webinterface/light.js", "application/javascript; charset=utf-8")) {
            sendPlain_(client, "404 Not Found", "Not found");
        }
        return true;
    }

    if ((isGet || isHead) && strcmp(path, "/api/web/meta") == 0) {
        sendWebMetaJson_(client);
        return true;
    }
    if ((isGet || isHead) && strcmp(path, "/api/wifi/config") == 0) {
        sendWifiConfigJson_(client);
        return true;
    }
    if ((isGet || isHead) && strcmp(path, "/api/wifi/ap") == 0) {
        sendApStatusJson_(client);
        return true;
    }
    if ((isGet || isHead) && strcmp(path, "/api/wifi/scan") == 0) {
        sendWifiScanJson_(client);
        return true;
    }
    if (isPost && strcmp(path, "/api/wifi/scan") == 0) {
        bool requested = false;
        if (wifiSvc_ && wifiSvc_->requestScan) {
            requested = wifiSvc_->requestScan(wifiSvc_->ctx, true);
        }
        sendJson_(client,
                  requested ? "202 Accepted" : "503 Service Unavailable",
                  requested ? "{\"ok\":true,\"accepted\":true}"
                            : "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.scan.start\"}}");
        return true;
    }
    if (isPost && strcmp(path, "/api/wifi/config") == 0) {
        const bool ok = handleSaveRequest_(body);
        sendSaveResponse_(client, ok);
        return true;
    }
    if (isPost && strcmp(path, "/api/system/reboot") == 0) {
        schedulePortalReboot_(800U);
        sendJson_(client, "202 Accepted", "{\"ok\":true,\"reboot_scheduled\":true}");
        return true;
    }
    if ((isGet || isHead) && strcmp(path, "/save") == 0) {
        const bool ok = handleSaveRequest_(query);
        sendPortalFallbackPage_(client,
                                ok ? "Configuration WiFi enregistree. Redemarrage en cours."
                                   : "Impossible d'enregistrer cette configuration WiFi.",
                                ok);
        return true;
    }

    sendPlain_(client, "404 Not Found", "Not found");
    return false;
}

void WifiProvisioningModule::sendHttpHeader_(WiFiClient& client, const char* status, const char* contentType)
{
    client.print(F("HTTP/1.1 "));
    client.print(status ? status : "200 OK");
    client.print(F("\r\nContent-Type: "));
    client.print(contentType ? contentType : "text/html; charset=utf-8");
    client.print(F("\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"));
}

void WifiProvisioningModule::sendJson_(WiFiClient& client, const char* status, const char* body)
{
    sendHttpHeader_(client, status ? status : "200 OK", "application/json");
    client.print(body ? body : "{\"ok\":true}");
}

void WifiProvisioningModule::sendPlain_(WiFiClient& client, const char* status, const char* body)
{
    sendHttpHeader_(client, status ? status : "200 OK", "text/plain; charset=utf-8");
    client.print(body ? body : "");
}

bool WifiProvisioningModule::sendSpiffsAsset_(WiFiClient& client, const char* path, const char* contentType)
{
    if (!portalSpiffsReady_ || !path || !contentType) return false;

    char gzipPath[96] = {0};
    const char* servedPath = path;
    bool gzip = false;
    const int n = snprintf(gzipPath, sizeof(gzipPath), "%s.gz", path);
    if (n > 0 && (size_t)n < sizeof(gzipPath) && SPIFFS.exists(gzipPath)) {
        servedPath = gzipPath;
        gzip = true;
    } else if (!SPIFFS.exists(path)) {
        return false;
    }

    File file = SPIFFS.open(servedPath, FILE_READ);
    if (!file) return false;

    client.print(F("HTTP/1.1 200 OK\r\nContent-Type: "));
    client.print(contentType);
    client.print(F("\r\nCache-Control: no-store\r\nConnection: close\r\n"));
    if (gzip) {
        client.print(F("Content-Encoding: gzip\r\nVary: Accept-Encoding\r\n"));
    }
    client.print(F("\r\n"));

    while (file.available()) {
        const size_t rd = file.read(portalFileBuf_, sizeof(portalFileBuf_));
        if (rd == 0U) break;
        client.write(portalFileBuf_, rd);
    }
    file.close();
    return true;
}

void WifiProvisioningModule::sendPortalFallbackPage_(WiFiClient& client, const char* message, bool success)
{
    sendHttpHeader_(client, "200 OK", "text/html; charset=utf-8");
    client.print(F("<!doctype html><html><head><meta charset=\"utf-8\">"
                   "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                   "<title>flow.io WiFi</title>"
                   "<style>body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:#f7f7f2;color:#17211c}"
                   "main{max-width:420px;margin:8vh auto;padding:24px}h1{font-size:24px;margin:0 0 8px}"
                   "p{line-height:1.45}.msg{padding:12px;border:1px solid #cbd5c8;background:#fff;margin:16px 0}"
                   "label{display:block;margin:14px 0 6px;font-weight:600}input{box-sizing:border-box;width:100%;font-size:18px;padding:12px;border:1px solid #9aa89d}"
                   "button{margin-top:18px;width:100%;font-size:18px;padding:12px;border:0;background:#166b52;color:#fff}</style></head><body><main>"
                   "<h1>flow.io</h1><p>Connexion au reseau WiFi de la piscine.</p>"));
    if (message && message[0] != '\0') {
        client.print(F("<div class=\"msg\" role=\"status\">"));
        client.print(message);
        if (success) {
            client.print(F("<br>Vous pouvez quitter ce reseau de configuration."));
        }
        client.print(F("</div>"));
    }
    client.print(F("<form action=\"/save\" method=\"get\">"
                   "<label for=\"ssid\">SSID WiFi</label><input id=\"ssid\" name=\"ssid\" maxlength=\"32\" autocomplete=\"off\" required>"
                   "<label for=\"pass\">Mot de passe</label><input id=\"pass\" name=\"pass\" maxlength=\"64\" type=\"password\" autocomplete=\"current-password\">"
                   "<button type=\"submit\">Enregistrer</button></form></main></body></html>"));
}

void WifiProvisioningModule::sendWebMetaJson_(WiFiClient& client)
{
    char out[384] = {0};
    const int n = snprintf(out,
                           sizeof(out),
                           "{\"ok\":true,\"firmware_version\":\"%s\",\"profile\":\"%s\","
                           "\"profile_name\":\"%s\",\"product_name\":\"flow.io\","
                           "\"wifi_only\":true,\"mqtt_config_enabled\":false,"
                           "\"runtime_enabled\":false,\"config_browser_enabled\":false,"
                           "\"full_ui_enabled\":false,\"reboot_after_wifi_save\":true}",
                           FirmwareVersion::Full,
                           FLOW_BUILD_PROFILE_NAME,
                           FLOW_BUILD_PROFILE_NAME);
    sendJson_(client,
              (n > 0 && (size_t)n < sizeof(out)) ? "200 OK" : "500 Internal Server Error",
              (n > 0 && (size_t)n < sizeof(out))
                  ? out
                  : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"web.meta\"}}");
}

void WifiProvisioningModule::sendWifiConfigJson_(WiFiClient& client)
{
    bool enabled = wifiEnabled_;
    const char* ssid = "";
    const char* pass = "";
    char wifiJson[320] = {0};
    StaticJsonDocument<320> doc;
    if (cfgStore_ && cfgStore_->toJsonModule("wifi", wifiJson, sizeof(wifiJson), nullptr, false)) {
        const DeserializationError err = deserializeJson(doc, wifiJson);
        if (!err && doc.is<JsonObjectConst>()) {
            JsonObjectConst root = doc.as<JsonObjectConst>();
            enabled = root["enabled"] | true;
            ssid = root["ssid"] | "";
            pass = root["pass"] | "";
        }
    }

    (void)jsonEscape_(ssid, portalEscSsid_, sizeof(portalEscSsid_));
    (void)jsonEscape_(pass, portalEscPass_, sizeof(portalEscPass_));
    char out[320] = {0};
    const int n = snprintf(out,
                           sizeof(out),
                           "{\"ok\":true,\"enabled\":%s,\"ssid\":\"%s\",\"pass\":\"%s\"}",
                           enabled ? "true" : "false",
                           portalEscSsid_,
                           portalEscPass_);
    sendJson_(client,
              (n > 0 && (size_t)n < sizeof(out)) ? "200 OK" : "500 Internal Server Error",
              (n > 0 && (size_t)n < sizeof(out))
                  ? out
                  : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.get\"}}");
}

void WifiProvisioningModule::sendApStatusJson_(WiFiClient& client)
{
    char ip[24] = {0};
    (void)getApIp_(ip, sizeof(ip));
    char ssid[48] = {0};
    snprintf(ssid, sizeof(ssid), "%s", apSsid_);
    (void)jsonEscape_(ssid, portalEscSsid_, sizeof(portalEscSsid_));

    char out[256] = {0};
    const int n = snprintf(out,
                           sizeof(out),
                           "{\"ok\":true,\"active\":%s,\"mode\":\"%s\",\"ssid\":\"%s\","
                           "\"pass\":\"%s\",\"ip\":\"%s\",\"clients\":%u}",
                           apActive_ ? "true" : "false",
                           apActive_ ? "ap" : "none",
                           portalEscSsid_,
                           apPass_,
                           ip,
                           (unsigned)WiFi.softAPgetStationNum());
    sendJson_(client,
              (n > 0 && (size_t)n < sizeof(out)) ? "200 OK" : "500 Internal Server Error",
              (n > 0 && (size_t)n < sizeof(out))
                  ? out
                  : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.ap\"}}");
}

void WifiProvisioningModule::sendWifiScanJson_(WiFiClient& client)
{
    if (!wifiSvc_ || !wifiSvc_->scanStatusJson) {
        sendJson_(client,
                  "503 Service Unavailable",
                  "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.scan.get\"}}");
        return;
    }

    portalScanJson_[0] = '\0';
    if (!wifiSvc_->scanStatusJson(wifiSvc_->ctx, portalScanJson_, sizeof(portalScanJson_))) {
        sendJson_(client,
                  "500 Internal Server Error",
                  "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.scan.get\"}}");
        return;
    }
    sendJson_(client, "200 OK", portalScanJson_);
}

void WifiProvisioningModule::sendSaveResponse_(WiFiClient& client, bool ok)
{
    if (ok) {
        sendJson_(client, "200 OK", "{\"ok\":true,\"reboot_scheduled\":true}");
    } else {
        sendJson_(client,
                  "400 Bad Request",
                  "{\"ok\":false,\"err\":{\"code\":\"InvalidData\",\"where\":\"wifi.config.set\"}}");
    }
}

void WifiProvisioningModule::schedulePortalReboot_(uint32_t delayMs)
{
    portalRebootPending_ = true;
    portalRebootAtMs_ = millis() + delayMs;
    LOGI("Provisioning reboot scheduled delay_ms=%lu", (unsigned long)delayMs);
}

bool WifiProvisioningModule::readRequestLine_(WiFiClient& client, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return false;
    out[0] = '\0';
    size_t pos = 0;
    const uint32_t deadline = millis() + 800U;
    while (client.connected() && ((int32_t)(millis() - deadline) < 0)) {
        while (client.available()) {
            const char c = (char)client.read();
            if (c == '\n') {
                out[pos] = '\0';
                return pos > 0U;
            }
            if (c == '\r') {
                continue;
            }
            if (pos < (outLen - 1U)) {
                out[pos++] = c;
            }
        }
        delay(1);
    }
    out[pos] = '\0';
    return pos > 0U;
}

bool WifiProvisioningModule::readHeaderLine_(WiFiClient& client, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return false;
    out[0] = '\0';
    size_t pos = 0;
    const uint32_t deadline = millis() + 800U;
    while (client.connected() && ((int32_t)(millis() - deadline) < 0)) {
        while (client.available()) {
            const char c = (char)client.read();
            if (c == '\n') {
                out[pos] = '\0';
                return true;
            }
            if (c == '\r') {
                continue;
            }
            if (pos < (outLen - 1U)) {
                out[pos++] = c;
            }
        }
        delay(1);
    }
    out[pos] = '\0';
    return pos > 0U;
}

bool WifiProvisioningModule::readRequestBody_(WiFiClient& client, size_t contentLen, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return false;
    out[0] = '\0';
    if (contentLen >= outLen) return false;

    size_t pos = 0;
    const uint32_t deadline = millis() + 1200U;
    while (pos < contentLen && client.connected() && ((int32_t)(millis() - deadline) < 0)) {
        while (client.available() && pos < contentLen) {
            const int raw = client.read();
            if (raw < 0) break;
            out[pos++] = (char)raw;
        }
        if (pos >= contentLen) break;
        delay(1);
    }
    out[pos] = '\0';
    return pos == contentLen;
}

bool WifiProvisioningModule::handleSaveRequest_(const char* query)
{
    if (!cfgStore_ || !query) return false;

    portalSsid_[0] = '\0';
    portalPass_[0] = '\0';
    char enabledStr[8] = {0};
    const bool hasEnabled = getQueryParam_(query, "enabled", enabledStr, sizeof(enabledStr));
    const bool enabled = !hasEnabled ||
                         enabledStr[0] == '1' ||
                         strcasecmp(enabledStr, "true") == 0 ||
                         strcasecmp(enabledStr, "on") == 0 ||
                         strcasecmp(enabledStr, "yes") == 0;
    if (!getQueryParam_(query, "ssid", portalSsid_, sizeof(portalSsid_))) {
        return false;
    }
    (void)getQueryParam_(query, "pass", portalPass_, sizeof(portalPass_));

    if (portalSsid_[0] == '\0') {
        return false;
    }
    if (!jsonEscape_(portalSsid_, portalEscSsid_, sizeof(portalEscSsid_)) ||
        !jsonEscape_(portalPass_, portalEscPass_, sizeof(portalEscPass_))) {
        return false;
    }

    const int n = snprintf(portalJson_,
                           sizeof(portalJson_),
                           "{\"wifi\":{\"enabled\":%s,\"ssid\":\"%s\",\"pass\":\"%s\"}}",
                           enabled ? "true" : "false",
                           portalEscSsid_,
                           portalEscPass_);
    if (n <= 0 || (size_t)n >= sizeof(portalJson_)) {
        return false;
    }

    const bool ok = cfgStore_->applyJson(portalJson_);
    if (ok) {
        refreshWifiConfig_();
        portalCredentialsSaved_ = true;
        (void)notifyWifiConfigChanged_();
        startStaProbe_(millis());
        schedulePortalReboot_(1200U);
        LOGI("Provisioning credentials saved ssid_len=%u pass_len=%u",
             (unsigned)strnlen(portalSsid_, sizeof(portalSsid_)),
             (unsigned)strnlen(portalPass_, sizeof(portalPass_)));
    }
    return ok;
}

bool WifiProvisioningModule::getQueryParam_(const char* query, const char* key, char* out, size_t outLen)
{
    if (!query || !key || !out || outLen == 0U) return false;
    out[0] = '\0';
    const size_t keyLen = strlen(key);
    const char* p = query;
    while (*p != '\0') {
        const char* name = p;
        const char* eq = strchr(name, '=');
        const char* amp = strchr(name, '&');
        if (!amp) {
            amp = name + strlen(name);
        }
        if (eq && eq < amp && (size_t)(eq - name) == keyLen && strncmp(name, key, keyLen) == 0) {
            return urlDecode_(eq + 1, (size_t)(amp - (eq + 1)), out, outLen);
        }
        p = (*amp == '&') ? (amp + 1) : amp;
    }
    return false;
}

bool WifiProvisioningModule::urlDecode_(const char* in, size_t len, char* out, size_t outLen)
{
    if (!in || !out || outLen == 0U) return false;
    size_t o = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = in[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && (i + 2U) < len) {
            const int hi = hexNibble_(in[i + 1U]);
            const int lo = hexNibble_(in[i + 2U]);
            if (hi < 0 || lo < 0) return false;
            c = (char)((hi << 4) | lo);
            i += 2U;
        }
        if (o >= (outLen - 1U)) return false;
        out[o++] = c;
    }
    out[o] = '\0';
    return true;
}

int WifiProvisioningModule::hexNibble_(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool WifiProvisioningModule::jsonEscape_(const char* in, char* out, size_t outLen)
{
    if (!in || !out || outLen == 0U) return false;
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char)in[i];
        if (c < 0x20U) {
            continue;
        }
        if (c == '"' || c == '\\') {
            if ((o + 2U) >= outLen) return false;
            out[o++] = '\\';
            out[o++] = (char)c;
        } else {
            if ((o + 1U) >= outLen) return false;
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
    return true;
}
#endif
