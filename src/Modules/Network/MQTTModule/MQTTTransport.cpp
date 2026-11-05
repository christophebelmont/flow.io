/**
 * @file MQTTTransport.cpp
 * @brief Broker transport setup and ESP-IDF client event handling for MQTTModule.
 */

#include "MQTTModule.h"

#include "Core/BufferUsageTracker.h"
#include "Core/MqttTopics.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"

#include <esp_heap_caps.h>
#include <esp_err.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MQTTModule)
#include "Core/ModuleLog.h"

namespace {
const char* socketErrName(int err)
{
    switch (err) {
        case 0: return "OK";
        case 104: return "ECONNRESET";
        case 107: return "ENOTCONN";
        case 113: return "EHOSTUNREACH";
        case 128: return "ENOTCONN_RTOS";
        default: return "UNKNOWN";
    }
}

const char* tlsEspHint(esp_err_t err)
{
    switch (err) {
        case 0x8008: return "peer closed TCP (FIN)";
        case 0x8006: return "TCP connect timeout";
        case 0x8004: return "connect to host failed";
        case 0x8001: return "cannot resolve host";
        default: return "";
    }
}
}

bool MQTTModule::ensureClient_()
{
    if (client_ && !clientConfigDirty_) return true;

    destroyClient_();

    if (cfgData_.host[0] == '\0') {
        LOGW("mqtt cfg invalid: empty host");
        return false;
    }

    int32_t effectivePort = cfgData_.port;
    if (effectivePort <= 0 || effectivePort > 65535) {
        LOGW("mqtt cfg invalid port=%ld, fallback=%u",
             (long)cfgData_.port,
             (unsigned)Limits::Mqtt::Defaults::Port);
        effectivePort = Limits::Mqtt::Defaults::Port;
    }

    const int uriLen = snprintf(brokerUri_, sizeof(brokerUri_), "mqtt://%s:%ld", cfgData_.host, (long)effectivePort);
    if (!(uriLen > 0 && (size_t)uriLen < sizeof(brokerUri_))) {
        LOGW("mqtt cfg invalid: broker uri too long");
        return false;
    }
    LOGD("mqtt cfg host=%s port=%ld user=%s client_id=%s",
         cfgData_.host,
         (long)effectivePort,
         cfgData_.user[0] ? cfgData_.user : "<empty>",
         deviceId_);

    esp_mqtt_client_config_t cfg = {};
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    cfg.broker.address.uri = brokerUri_;
    cfg.credentials.client_id = deviceId_;
    if (cfgData_.user[0] != '\0') {
        cfg.credentials.username = cfgData_.user;
        cfg.credentials.authentication.password = cfgData_.pass;
    }
    cfg.session.last_will.topic = topicStatus_;
    cfg.session.last_will.msg = "{\"online\":false}";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.network.disable_auto_reconnect = true;
#else
    cfg.uri = brokerUri_;
    cfg.client_id = deviceId_;
    if (cfgData_.user[0] != '\0') {
        cfg.username = cfgData_.user;
        cfg.password = cfgData_.pass;
    }
    cfg.lwt_topic = topicStatus_;
    cfg.lwt_msg = "{\"online\":false}";
    cfg.lwt_qos = 1;
    cfg.lwt_retain = 1;
    cfg.disable_auto_reconnect = true;
    cfg.user_context = this;
#endif

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) {
        LOGW("mqtt client init failed");
        return false;
    }

    if (esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MQTTModule::mqttEventHandlerStatic_, this) != ESP_OK) {
        LOGW("mqtt register event failed");
        destroyClient_();
        return false;
    }

    clientConfigDirty_ = false;
    return true;
}

void MQTTModule::stopClient_(bool intentional)
{
    if (!client_) return;
    if (intentional) suppressDisconnectEvent_ = true;
    (void)esp_mqtt_client_stop(client_);
    clientStarted_ = false;
}

void MQTTModule::destroyClient_()
{
    if (!client_) return;
    (void)esp_mqtt_client_stop(client_);
    (void)esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    clientStarted_ = false;
}

void MQTTModule::connectMqtt_()
{
    buildTopics_();
    suppressDisconnectEvent_ = false;

    if (!ensureClient_()) {
        setState_(MQTTState::ErrorWait);
        return;
    }
    LOGD("mqtt connect attempt broker=%s user=%s client_id=%s",
         brokerUri_,
         cfgData_.user[0] ? cfgData_.user : "<empty>",
         deviceId_);

    esp_err_t err = ESP_FAIL;
    if (!clientStarted_) {
        err = esp_mqtt_client_start(client_);
        if (err == ESP_OK) {
            clientStarted_ = true;
        } else {
            // Defensive fallback: recover when local state says "stopped" but IDF client is already running.
            const esp_err_t reconnectErr = esp_mqtt_client_reconnect(client_);
            if (reconnectErr == ESP_OK) {
                LOGW("mqtt start failed (%s), reconnect fallback ok", esp_err_to_name(err));
                err = ESP_OK;
                clientStarted_ = true;
            }
        }
    } else {
        err = esp_mqtt_client_reconnect(client_);
        if (err != ESP_OK) {
            // Defensive fallback: recover when local state says "started" but IDF client requires start.
            const esp_err_t startErr = esp_mqtt_client_start(client_);
            if (startErr == ESP_OK) {
                LOGW("mqtt reconnect failed (%s), start fallback ok", esp_err_to_name(err));
                err = ESP_OK;
                clientStarted_ = true;
            }
        }
    }

    if (err != ESP_OK) {
        LOGW("mqtt connect failed err=%s (%d)", esp_err_to_name(err), (int)err);
        setState_(MQTTState::ErrorWait);
        return;
    }

    setState_(MQTTState::Connecting);
}

void MQTTModule::onConnect_(bool)
{
    suppressDisconnectEvent_ = false;

    (void)esp_mqtt_client_subscribe(client_, topicCmd_, 0);
    (void)esp_mqtt_client_subscribe(client_, topicCfgSet_, 1);

    const MqttInboundHandler* handlers[MaxInboundHandlers]{};
    uint8_t handlerCount = 0;
    portENTER_CRITICAL(&inboundMux_);
    handlerCount = inboundHandlerCount_;
    if (handlerCount > MaxInboundHandlers) handlerCount = MaxInboundHandlers;
    for (uint8_t i = 0; i < handlerCount; ++i) {
        handlers[i] = inboundHandlers_[i];
    }
    portEXIT_CRITICAL(&inboundMux_);

    for (uint8_t i = 0; i < handlerCount; ++i) {
        const MqttInboundHandler* h = handlers[i];
        if (!h || !h->topicSuffix || h->topicSuffix[0] == '\0') continue;
        char topic[Limits::Mqtt::Buffers::Topic] = {0};
        formatTopic(topic, sizeof(topic), h->topicSuffix);
        if (topic[0] != '\0') {
            (void)esp_mqtt_client_subscribe(client_, topic, 0);
        }
    }

    retryCount_ = 0;
    retryDelayMs_ = Limits::Mqtt::Backoff::MinMs;
    if (dataStore_) {
        setMqttRuntimeFullSnapshotPublished(*dataStore_, false);
    }
    setState_(MQTTState::Connected);

    (void)enqueue(ProducerIdStatus, StatusMsgOnline, MqttPublishPriority::High, 0);
    if (cfgProducer_) {
        cfgProducer_->requestFullSync(MqttPublishPriority::Low);
    }
    runtimeProducerCore_.onConnected();
    enqueueAlarmFullSync_();

    const uint32_t now = millis();
    for (uint8_t i = 0; i < runtimePublisherCount_; ++i) {
        RuntimePublisher& p = runtimePublishers_[i];
        if (!p.used || p.periodMs == 0U) continue;
        p.nextDueMs = now;
        (void)enqueue(ProducerIdStatus, p.messageId, MqttPublishPriority::Normal, 0);
    }
}

void MQTTModule::onDisconnect_(const esp_mqtt_error_codes_t* err)
{
    if (suppressDisconnectEvent_) {
        suppressDisconnectEvent_ = false;
        return;
    }

    // Keep "started" flag for unexpected disconnects: the IDF client task usually remains active and expects reconnect().
    clientStarted_ = (client_ != nullptr);
    if (err) {
        if (err->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            LOGW("mqtt disconnected: refused code=%d", (int)err->connect_return_code);
        } else if (err->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            LOGW("mqtt disconnected: transport sock_errno=%d(%s) tls_esp=%d(%s) hint=%s tls_stack=%d cert_flags=0x%x",
                 err->esp_transport_sock_errno,
                 socketErrName(err->esp_transport_sock_errno),
                 (int)err->esp_tls_last_esp_err,
                 esp_err_to_name(err->esp_tls_last_esp_err),
                 tlsEspHint(err->esp_tls_last_esp_err),
                 err->esp_tls_stack_err,
                 (unsigned)err->esp_tls_cert_verify_flags);
        } else {
            LOGW("mqtt disconnected: error_type=%d", (int)err->error_type);
        }
    } else {
        LOGW("mqtt disconnected: no error details");
    }

    setState_(MQTTState::ErrorWait);
}

void MQTTModule::onMessage_(const char* topic,
                            size_t topicLen,
                            const char* payload,
                            size_t len,
                            size_t index,
                            size_t total)
{
    if (!rxQ_) return;
    if (!topic || !payload) {
        countRxDrop_();
        return;
    }
    if (index != 0 || len != total) {
        countRxDrop_();
        return;
    }

    if (topicLen >= sizeof(RxMsg{}.topic) || len >= sizeof(RxMsg{}.payload)) {
        countOversizeDrop_();
        return;
    }

    RxMsg msg{};
    memcpy(msg.topic, topic, topicLen);
    msg.topic[topicLen] = '\0';
    memcpy(msg.payload, payload, len);
    msg.payload[len] = '\0';

    if (xQueueSend(rxQ_, &msg, 0) != pdTRUE) {
        countRxDrop_();
        return;
    }

    const UBaseType_t queued = uxQueueMessagesWaiting(rxQ_);
    BufferUsageTracker::note(TrackedBufferId::MqttRxQueueStorage,
                             (size_t)queued * sizeof(RxMsg),
                             (size_t)Limits::Mqtt::Capacity::RxQueueLen * sizeof(RxMsg),
                             msg.topic,
                             nullptr);
}

void MQTTModule::mqttEventHandlerStatic_(void* handler_args,
                                         esp_event_base_t,
                                         int32_t event_id,
                                         void* event_data)
{
    MQTTModule* self = static_cast<MQTTModule*>(handler_args);
    if (!self || !event_data) return;

    esp_mqtt_event_handle_t ev = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            self->onConnect_(ev->session_present != 0);
            break;
        case MQTT_EVENT_ERROR:
            if (ev && ev->error_handle) {
                const esp_mqtt_error_codes_t* err = ev->error_handle;
                LOGW("mqtt event error: type=%d sock_errno=%d(%s) tls_esp=%d(%s) hint=%s tls_stack=%d refused=%d",
                     (int)err->error_type,
                     err->esp_transport_sock_errno,
                     socketErrName(err->esp_transport_sock_errno),
                     (int)err->esp_tls_last_esp_err,
                     esp_err_to_name(err->esp_tls_last_esp_err),
                     tlsEspHint(err->esp_tls_last_esp_err),
                     err->esp_tls_stack_err,
                     (int)err->connect_return_code);
            } else {
                LOGW("mqtt event error: no details");
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            self->onDisconnect_(ev ? ev->error_handle : nullptr);
            break;
        case MQTT_EVENT_DATA:
            self->onMessage_(ev->topic,
                             (size_t)ev->topic_len,
                             ev->data,
                             (size_t)ev->data_len,
                             (size_t)ev->current_data_offset,
                             (size_t)ev->total_data_len);
            break;
        default:
            break;
    }
}

void MQTTModule::formatTopic(char* out, size_t outLen, const char* suffix) const
{
    if (!out || outLen == 0 || !suffix) return;
    snprintf(out, outLen, "%s/%s/%s", cfgData_.baseTopic, deviceId_, suffix);
}
