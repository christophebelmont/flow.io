/**
 * @file WebInterfaceLifecycle.cpp
 * @brief Event handling and task loop for WebInterfaceModule.
 */

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::WebInterfaceModule)
#include "WebInterfaceModule.h"

#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/ModuleLog.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

bool WebInterfaceModule::setPaused_(bool paused)
{
    uartPaused_ = paused;
    if (paused) {
        lineLen_ = 0;
    }
    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&healthMux_);
    health_.snapshotMs = nowMs;
    health_.paused = uartPaused_;
    portEXIT_CRITICAL(&healthMux_);
    return true;
}

uint8_t WebInterfaceModule::wsActiveSource_() const
{
    uint8_t source = 0U;
    portENTER_CRITICAL(&wsSourceMux_);
    source = wsSource_;
    portEXIT_CRITICAL(&wsSourceMux_);
    return source;
}

void WebInterfaceModule::setWsActiveSource_(uint8_t source)
{
    const uint8_t sanitized = (source == 1U) ? 1U : 0U;
    portENTER_CRITICAL(&wsSourceMux_);
    wsSource_ = sanitized;
    portEXIT_CRITICAL(&wsSourceMux_);
}

bool WebInterfaceModule::isPaused_() const
{
    return uartPaused_;
}

bool WebInterfaceModule::getHealth_(WebInterfaceHealth* out) const
{
    if (!out) return false;
    portENTER_CRITICAL(&healthMux_);
    *out = health_;
    portEXIT_CRITICAL(&healthMux_);
    return true;
}

void WebInterfaceModule::noteLoopActivity_()
{
    const uint32_t nowMs = millis();
    const uint16_t wsLogClients = (uint16_t)wsLog_.count();
    const uint16_t wsSerialClients = (wsActiveSource_() == 1U) ? wsLogClients : 0U;
    portENTER_CRITICAL(&healthMux_);
    health_.snapshotMs = nowMs;
    health_.lastLoopMs = nowMs;
    health_.started = started_;
    health_.paused = uartPaused_;
    health_.wsSerialClients = wsSerialClients;
    health_.wsLogClients = wsLogClients;
    portEXIT_CRITICAL(&healthMux_);
}

void WebInterfaceModule::noteHttpActivity_()
{
    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&healthMux_);
    health_.snapshotMs = nowMs;
    health_.lastHttpActivityMs = nowMs;
    health_.started = started_;
    health_.paused = uartPaused_;
    portEXIT_CRITICAL(&healthMux_);
}

void WebInterfaceModule::noteWsActivity_()
{
    const uint32_t nowMs = millis();
    const uint16_t wsLogClients = (uint16_t)wsLog_.count();
    const uint16_t wsSerialClients = (wsActiveSource_() == 1U) ? wsLogClients : 0U;
    portENTER_CRITICAL(&healthMux_);
    health_.snapshotMs = nowMs;
    health_.lastWsActivityMs = nowMs;
    health_.started = started_;
    health_.paused = uartPaused_;
    health_.wsSerialClients = wsSerialClients;
    health_.wsLogClients = wsLogClients;
    portEXIT_CRITICAL(&healthMux_);
}

void WebInterfaceModule::noteServerStarted_()
{
    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&healthMux_);
    health_.snapshotMs = nowMs;
    health_.lastLoopMs = nowMs;
    health_.started = true;
    health_.paused = uartPaused_;
    health_.wsSerialClients = 0U;
    health_.wsLogClients = 0U;
    portEXIT_CRITICAL(&healthMux_);
}

void WebInterfaceModule::onHttpActivityHook_(void* ctx)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(ctx);
    if (!self) return;
    self->noteHttpActivity_();
}

void WebInterfaceModule::scheduleReboot_(uint32_t delayMs, const char* reason)
{
    rebootPending_ = true;
    rebootAtMs_ = millis() + delayMs;
    snprintf(rebootReason_, sizeof(rebootReason_), "%s", (reason && reason[0] != '\0') ? reason : "web");
    if (!netAccessSvc_ && services_) {
        netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
    }
    if (netAccessSvc_ && netAccessSvc_->notifyShutdownPending) {
        (void)netAccessSvc_->notifyShutdownPending(netAccessSvc_->ctx);
    }
    if (eventBus_) {
        (void)eventBus_->post(EventId::NetworkShutdownPending, nullptr, 0, moduleId());
    }
    LOGW("Web reboot scheduled in %lu ms reason=%s", (unsigned long)delayMs, rebootReason_);
}

void WebInterfaceModule::onEventStatic_(const Event& e, void* user)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(user);
    if (!self) return;
    self->onEvent_(e);
}

void WebInterfaceModule::onEvent_(const Event& e)
{
    if (e.id == EventId::ConfigChanged) {
#if defined(FLOW_PROFILE_WAVESHARE)
        portENTER_CRITICAL(&ioResponseMux_);
        ++ioTopologyChangeGeneration_;
        portEXIT_CRITICAL(&ioResponseMux_);
#endif
        return;
    }
    if (e.id != EventId::DataChanged) return;
    if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
    const DataChangedPayload* p = static_cast<const DataChangedPayload*>(e.payload);
    if (p->id != DataKeys::NetworkReady) return;

    netReady_ = dataStore_ ? networkReady(*dataStore_) : false;
}

void WebInterfaceModule::loop()
{
    if (webStartLedPulseActive_ && (int32_t)(millis() - webStartLedPulseUntilMs_) >= 0) {
        if (hmiSvc_ && hmiSvc_->setStatusLedAutoWifiMode && webStartLedPrevAutoModeValid_) {
            hmiSvc_->setStatusLedAutoWifiMode(hmiSvc_->ctx, webStartLedPrevAutoMode_);
        }
        webStartLedPulseActive_ = false;
        webStartLedPrevAutoModeValid_ = false;
        LOGI("Web start LED pulse completed");
    }

    if (rebootPending_ && (int32_t)(millis() - rebootAtMs_) >= 0) {
        LOGW("Web rebooting now reason=%s", rebootReason_);
        delay(80);
        ESP.restart();
    }

    noteLoopActivity_();

    if (!netAccessSvc_ && services_) {
        netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
    }

    if (!started_) {
        char ip[16] = {0};
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!getNetworkIp_(ip, sizeof(ip), &mode) || ip[0] == '\0' || mode == NetworkAccessMode::None) {
            vTaskDelay(pdMS_TO_TICKS(100));
            return;
        }

#if defined(FLOW_PROFILE_WAVESHARE)
        if (mode == NetworkAccessMode::AccessPoint) {
            provisioningOnly_ = true;
            LOGI("Web startup in flow.io AP provisioning mode");
        }
#endif

        const bool bootNetworkReady = (mode == NetworkAccessMode::AccessPoint) ? true : netReady_;
        if (!bootNetworkReady) {
            vTaskDelay(pdMS_TO_TICKS(100));
            return;
        }

        const char* modeText = (mode == NetworkAccessMode::AccessPoint) ? "ap" : "station";
        LOGI("Web startup release mode=%s ip=%s starting server", modeText, ip);
        const uint32_t minHeapBeforeStart = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        const uint32_t internalBeforeStart = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const uint32_t largestInternalBeforeStart =
            (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        startServer_();
        const uint32_t minHeapAfterStart = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        const uint32_t internalAfterStart = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const uint32_t largestInternalAfterStart =
            (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        const long minHeapDelta = (long)minHeapAfterStart - (long)minHeapBeforeStart;
        LOGI("Web heap around startup: min8_before=%lu min8_after=%lu min8_delta=%ld "
             "internal_before=%lu internal_after=%lu largest_internal_before=%lu largest_internal_after=%lu",
             (unsigned long)minHeapBeforeStart,
             (unsigned long)minHeapAfterStart,
             minHeapDelta,
             (unsigned long)internalBeforeStart,
             (unsigned long)internalAfterStart,
             (unsigned long)largestInternalBeforeStart,
             (unsigned long)largestInternalAfterStart);
    }

    if (uartPaused_) {
        flushLocalLogQueue_();
        if (started_) wsLog_.cleanupClients();
        vTaskDelay(pdMS_TO_TICKS(40));
        return;
    }

    if (provisioningOnly_) {
        if (started_) wsLog_.cleanupClients();
        vTaskDelay(pdMS_TO_TICKS(25));
        return;
    }

#if defined(FLOW_PROFILE_WAVESHARE)
    refreshIoResponseCaches_();
#endif

    const bool wsClientActive = started_ && (wsLog_.count() > 0U);
    const bool flowSourceActive = wsClientActive && (wsActiveSource_() == 1U) && bridgeUartEnabled_;

    if (flowSourceActive) {
        while (uart_.available() > 0) {
            int raw = uart_.read();
            if (raw < 0) break;

            const uint8_t c = static_cast<uint8_t>(raw);

            if (c == '\r') continue;
            if (c == '\n') {
                flushLine_(true);
                continue;
            }

            if (lineLen_ >= (kLineBufferSize - 1)) {
                flushLine_(true);
            }

            if (lineLen_ < (kLineBufferSize - 1)) {
                lineBuf_[lineLen_++] = isLogByte_(c) ? static_cast<char>(c) : '.';
            }
        }
    } else {
        lineLen_ = 0;
        flushLocalLogQueue_();
    }

    if (started_) wsLog_.cleanupClients();

    vTaskDelay(pdMS_TO_TICKS(10));
}
