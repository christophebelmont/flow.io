/**
 * @file MQTTLifecycle.cpp
 * @brief Lifecycle, service surface and top-level state management for MQTTModule.
 */

#include "MQTTModule.h"

#include "Core/BufferUsageTracker.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/MqttTopics.h"
#include "Core/Runtime.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <initializer_list>
#include <new>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MQTTModule)
#include "Core/ModuleLog.h"

namespace {
uint32_t clampU32(uint32_t v, uint32_t minV, uint32_t maxV)
{
    if (v < minV) return minV;
    if (v > maxV) return maxV;
    return v;
}

uint32_t jitterMs(uint32_t baseMs, uint8_t pct)
{
    if (baseMs == 0U || pct == 0U) return baseMs;
    const uint32_t span = (baseMs * pct) / 100U;
    const uint32_t r = esp_random();
    const uint32_t delta = r % (2U * span + 1U);
    const int32_t signedDelta = (int32_t)delta - (int32_t)span;
    int32_t out = (int32_t)baseMs + signedDelta;
    if (out < 0) out = 0;
    return (uint32_t)out;
}

bool isAnyOf(const char* key, std::initializer_list<const char*> keys)
{
    if (!key || key[0] == '\0') return false;
    for (const char* candidate : keys) {
        if (candidate && strcmp(key, candidate) == 0) return true;
    }
    return false;
}

bool isMqttConnKey(const char* key)
{
    return isAnyOf(key, {
        NvsKeys::Mqtt::BaseTopic,
        NvsKeys::Mqtt::TopicDeviceId,
        NvsKeys::Mqtt::Host,
        NvsKeys::Mqtt::Port,
        NvsKeys::Mqtt::User,
        NvsKeys::Mqtt::Pass
    });
}

void makeDeviceId(char* out, size_t len)
{
    if (!out || len == 0) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

size_t sanitizeTopicSegment(const char* src, char* dst, size_t cap)
{
    if (!src || !dst || cap == 0U) return 0U;
    size_t w = 0U;
    for (size_t i = 0U; src[i] != '\0' && (w + 1U) < cap; ++i) {
        const char c = src[i];
        const bool safe =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            (c == '-') ||
            (c == '_') ||
            (c == '.');
        dst[w++] = safe ? c : '_';
    }
    dst[w] = '\0';
    return w;
}

static constexpr uint8_t kMqttCfgBranch = 1;
static constexpr uint32_t kDataChangedSlowWarnUs = 7000U;
static constexpr uint32_t kDataChangedTraceWarnUs = 30000U;
static constexpr uint32_t kDataChangedTraceMinIntervalMs = 250U;

static constexpr MqttConfigRouteProducer::Route kMqttCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Mqtt, kMqttCfgBranch}, "mqtt", "mqtt", (uint8_t)MqttPublishPriority::Normal, nullptr},
};
} // namespace

bool MQTTModule::enqueueSvc_(uint8_t producerId, uint16_t messageId, uint8_t prio, uint8_t flags)
{
    MqttPublishPriority p = MqttPublishPriority::Normal;
    if (prio == (uint8_t)MqttPublishPriority::Low) p = MqttPublishPriority::Low;
    else if (prio == (uint8_t)MqttPublishPriority::High) p = MqttPublishPriority::High;

    return enqueue(producerId, messageId, p, flags);
}

void MQTTModule::formatTopicSvc_(const char* suffix, char* out, size_t outLen) const
{
    formatTopic(out, outLen, suffix);
}

void MQTTModule::setState_(MQTTState s)
{
    state_ = s;
    stateTs_ = millis();
    if (dataStore_) {
        if (s != MQTTState::Connected) {
            setMqttRuntimeFullSnapshotPublished(*dataStore_, false);
        }
        setMqttReady(*dataStore_, s == MQTTState::Connected);
    }
}

void MQTTModule::onRuntimeInitialSnapshotCompleteStatic_(void* ctx)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    if (self) self->onRuntimeInitialSnapshotComplete_();
}

void MQTTModule::onRuntimeInitialSnapshotComplete_()
{
    if (dataStore_) {
        setMqttRuntimeFullSnapshotPublished(*dataStore_, true);
    }
}

bool MQTTModule::allocateScratchBuffers_()
{
    if (scratch_) return true;

    scratch_ = static_cast<ScratchBuffers*>(
#if defined(FLOW_PROFILE_WAVESHARE)
        heap_caps_calloc(1, sizeof(ScratchBuffers), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
        heap_caps_calloc(1, sizeof(ScratchBuffers), MALLOC_CAP_8BIT)
#endif
    );
    if (!scratch_) {
        LOGE("mqtt scratch alloc failed (%u bytes)", (unsigned)sizeof(ScratchBuffers));
        return false;
    }
    return true;
}

bool MQTTModule::allocateRxQueue_()
{
    if (rxQ_) return true;

    const size_t rxItemSize = sizeof(RxMsg);
    const size_t rxQueueStorageLen = ((size_t)Limits::Mqtt::Capacity::RxQueueLen) * rxItemSize;
    rxQueueStorage_ = static_cast<uint8_t*>(
#if defined(FLOW_PROFILE_WAVESHARE)
        heap_caps_malloc(rxQueueStorageLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
        heap_caps_malloc(rxQueueStorageLen, MALLOC_CAP_8BIT)
#endif
    );
    if (rxQueueStorage_) {
        rxQ_ = xQueueCreateStatic(Limits::Mqtt::Capacity::RxQueueLen,
                                  rxItemSize,
                                  rxQueueStorage_,
                                  &rxQueueStruct_);
    } else {
        rxQ_ = nullptr;
    }
    return rxQ_ != nullptr;
}

void MQTTModule::refreshTopicDeviceId_()
{
    if (cfgData_.topicDeviceId[0] == '\0') {
        makeDeviceId(deviceId_, sizeof(deviceId_));
        return;
    }

    char sanitized[sizeof(deviceId_)] = {0};
    const size_t n = sanitizeTopicSegment(cfgData_.topicDeviceId, sanitized, sizeof(sanitized));
    if (n == 0U) {
        makeDeviceId(deviceId_, sizeof(deviceId_));
        return;
    }
    snprintf(deviceId_, sizeof(deviceId_), "%s", sanitized);
}

void MQTTModule::buildTopics_()
{
    refreshTopicDeviceId_();
    snprintf(topicCmd_, sizeof(topicCmd_), "%s/%s/%s", cfgData_.baseTopic, deviceId_, MqttTopics::SuffixCmd);
    snprintf(topicStatus_, sizeof(topicStatus_), "%s/%s/%s", cfgData_.baseTopic, deviceId_, MqttTopics::SuffixStatus);
    snprintf(topicCfgSet_, sizeof(topicCfgSet_), "%s/%s/%s", cfgData_.baseTopic, deviceId_, MqttTopics::SuffixCfgSet);
}

void MQTTModule::onEventStatic_(const Event& e, void* user)
{
    MQTTModule* self = static_cast<MQTTModule*>(user);
    if (self) self->onEvent_(e);
}

void MQTTModule::onEvent_(const Event& e)
{
    if (e.id == EventId::DataChanged) {
        const uint32_t t0 = micros();
        const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
        if (!p) return;

        if (p->id == DATAKEY_NETWORK_READY) {
            if (!dataStore_) return;

            const bool oldReady = netReady_;
            const MQTTState oldState = state_;
            const bool ready = networkReady(*dataStore_);
            if (ready == netReady_) return;

            netReady_ = ready;
            netReadyTs_ = millis();

            if (netReady_) {
                if (state_ != MQTTState::Connected) setState_(MQTTState::WaitingNetwork);
            } else {
                stopClient_(true);
                setState_(MQTTState::WaitingNetwork);
            }
            const uint32_t dtUs = (uint32_t)(micros() - t0);
            uint16_t jobsUsed = 0U;
            uint16_t highCount = 0U;
            uint16_t normalCount = 0U;
            uint16_t lowCount = 0U;
            portENTER_CRITICAL(&jobsMux_);
            snapshotQueueStatsNoLock_(jobsUsed, highCount, normalCount, lowCount);
            portEXIT_CRITICAL(&jobsMux_);
            const bool slow = dtUs >= kDataChangedSlowWarnUs;
            if (slow) {
                LOGW("datachanged net key=%u dt=%lu us ready=%u->%u state=%u->%u jobs=%u/%u qh=%u/%u qn=%u/%u ql=%u/%u",
                     (unsigned)p->id,
                     (unsigned long)dtUs,
                     (unsigned)oldReady,
                     (unsigned)netReady_,
                     (unsigned)oldState,
                     (unsigned)state_,
                     (unsigned)jobsUsed,
                     (unsigned)MaxJobs,
                     (unsigned)highCount,
                     (unsigned)HighQueueCap,
                     (unsigned)normalCount,
                     (unsigned)NormalQueueCap,
                     (unsigned)lowCount,
                     (unsigned)LowQueueCap);
            } else {
                LOGI("datachanged net key=%u dt=%lu us ready=%u->%u state=%u->%u jobs=%u/%u qh=%u/%u qn=%u/%u ql=%u/%u",
                     (unsigned)p->id,
                     (unsigned long)dtUs,
                     (unsigned)oldReady,
                     (unsigned)netReady_,
                     (unsigned)oldState,
                     (unsigned)state_,
                     (unsigned)jobsUsed,
                     (unsigned)MaxJobs,
                     (unsigned)highCount,
                     (unsigned)HighQueueCap,
                     (unsigned)normalCount,
                     (unsigned)NormalQueueCap,
                     (unsigned)lowCount,
                     (unsigned)LowQueueCap);
            }
            return;
        }

        const RuntimeProducer::DataChangeStats stats = runtimeProducerCore_.onDataChanged(p->id);
        const uint32_t dtUs = (uint32_t)(micros() - t0);
        const bool slow = dtUs >= kDataChangedSlowWarnUs;
        const bool trace = dtUs >= kDataChangedTraceWarnUs ||
                           stats.enqueueFail > 0U ||
                           stats.matched > 8U;
        const uint32_t nowMs = millis();
        if (slow && trace &&
            (uint32_t)(nowMs - lastDataChangedTraceLogMs_) >= kDataChangedTraceMinIntervalMs) {
            lastDataChangedTraceLogMs_ = nowMs;
            uint16_t jobsUsed = 0U;
            uint16_t highCount = 0U;
            uint16_t normalCount = 0U;
            uint16_t lowCount = 0U;
            portENTER_CRITICAL(&jobsMux_);
            snapshotQueueStatsNoLock_(jobsUsed, highCount, normalCount, lowCount);
            portEXIT_CRITICAL(&jobsMux_);
            LOGW("datachanged slow key=%u dt=%lu us state=%u routes=%u checked=%u matched=%u enq_ok=%u enq_fail=%u already=%u enq_us=%lu jobs=%u/%u qh=%u/%u qn=%u/%u ql=%u/%u",
                 (unsigned)p->id,
                 (unsigned long)dtUs,
                 (unsigned)state_,
                 (unsigned)stats.routeCount,
                 (unsigned)stats.checked,
                 (unsigned)stats.matched,
                 (unsigned)stats.enqueueOk,
                 (unsigned)stats.enqueueFail,
                 (unsigned)stats.pendingAlready,
                 (unsigned long)stats.enqueueUs,
                 (unsigned)jobsUsed,
                 (unsigned)MaxJobs,
                 (unsigned)highCount,
                 (unsigned)HighQueueCap,
                 (unsigned)normalCount,
                 (unsigned)NormalQueueCap,
                 (unsigned)lowCount,
                 (unsigned)LowQueueCap);
        }
        return;
    }

    if (e.id == EventId::ConfigChanged) {
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (!p) return;

        const char* key = p->nvsKey;
        if (key && key[0] != '\0' && isMqttConnKey(key)) {
            clientConfigDirty_ = true;
            stopClient_(true);
            netReadyTs_ = millis();
            setState_(MQTTState::WaitingNetwork);
        }

        return;
    }

    if (e.id == EventId::AlarmRaised ||
        e.id == EventId::AlarmCleared ||
        e.id == EventId::AlarmReset ||
        e.id == EventId::AlarmSilenceChanged ||
        e.id == EventId::AlarmConditionChanged) {
        const AlarmPayload* p = (const AlarmPayload*)e.payload;
        if (p && e.len >= sizeof(AlarmPayload)) {
            (void)enqueue(ProducerIdAlarm,
                          (uint16_t)(AlarmMsgStateBase + (uint16_t)p->alarmId),
                          MqttPublishPriority::High,
                          0);
        } else {
            enqueueAlarmFullSync_();
            return;
        }

        (void)enqueue(ProducerIdAlarm, AlarmMsgMeta, MqttPublishPriority::Normal, 0);
        (void)enqueue(ProducerIdAlarm, AlarmMsgPack, MqttPublishPriority::Normal, 0);
        return;
    }
}

void MQTTModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Mqtt;
    constexpr uint8_t kCfgBranchId = kMqttCfgBranch;
    cfg.registerVar(hostVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(portVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(userVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(passVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(baseTopicVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(topicDeviceIdVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(deviceNameVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);

    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    timeSchedSvc_ = services.get<TimeSchedulerService>(ServiceId::TimeScheduler);
    alarmSvc_ = services.get<AlarmService>(ServiceId::Alarm);
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    (void)logHub_;

    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;

    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    rxDropCount_ = 0;
    parseFailCount_ = 0;
    handlerFailCount_ = 0;
    oversizeDropCount_ = 0;
    syncRxMetrics_();

    if (!services.add(ServiceId::Mqtt, &mqttSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Mqtt));
    }

    if (!cfgProducer_) {
        cfgProducer_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgProducer_) {
        cfgProducer_->configure(this,
                                ProducerIdConfig,
                                kMqttCfgRoutes,
                                (uint8_t)(sizeof(kMqttCfgRoutes) / sizeof(kMqttCfgRoutes[0])),
                                services);
    }
    runtimeProducerCore_.configure(&mqttSvc_,
                                   &MQTTModule::onRuntimeInitialSnapshotCompleteStatic_,
                                   this);

    ackProducerDesc_ = MqttPublishProducer{};
    ackProducerDesc_.producerId = ProducerIdAck;
    ackProducerDesc_.ctx = this;
    ackProducerDesc_.buildMessage = MQTTModule::buildAckStatic_;
    ackProducerDesc_.onMessagePublished = MQTTModule::onAckPublishedStatic_;
    ackProducerDesc_.onMessageDropped = MQTTModule::onAckDroppedStatic_;

    statusProducerDesc_ = MqttPublishProducer{};
    statusProducerDesc_.producerId = ProducerIdStatus;
    statusProducerDesc_.ctx = this;
    statusProducerDesc_.buildMessage = MQTTModule::buildStatusStatic_;

    runtimeProducerDesc_ = MqttPublishProducer{};
    runtimeProducerDesc_.producerId = ProducerIdRuntime;
    runtimeProducerDesc_.ctx = &runtimeProducerCore_;
    runtimeProducerDesc_.buildMessage = [](void* ctx, uint16_t messageId, MqttBuildContext& buildCtx) -> MqttBuildResult {
        RuntimeProducer* p = static_cast<RuntimeProducer*>(ctx);
        return p ? p->buildMessage(messageId, buildCtx) : MqttBuildResult::PermanentError;
    };
    runtimeProducerDesc_.onMessagePublished = [](void* ctx, uint16_t messageId) {
        RuntimeProducer* p = static_cast<RuntimeProducer*>(ctx);
        if (p) p->onMessagePublished(messageId);
    };
    runtimeProducerDesc_.onMessageDropped = [](void* ctx, uint16_t messageId) {
        RuntimeProducer* p = static_cast<RuntimeProducer*>(ctx);
        if (p) p->onMessageDropped(messageId);
    };

    alarmProducerDesc_ = MqttPublishProducer{};
    alarmProducerDesc_.producerId = ProducerIdAlarm;
    alarmProducerDesc_.ctx = this;
    alarmProducerDesc_.buildMessage = MQTTModule::buildAlarmStatic_;

    (void)registerProducer(&ackProducerDesc_);
    (void)registerProducer(&statusProducerDesc_);
    (void)registerProducer(&runtimeProducerDesc_);
    (void)registerProducer(&alarmProducerDesc_);

    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::ConfigChanged, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmRaised, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmCleared, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmReset, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmSilenceChanged, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmConditionChanged, &MQTTModule::onEventStatic_, this);
    }

    makeDeviceId(deviceId_, sizeof(deviceId_));
    buildTopics_();

#if defined(FLOW_PROFILE_MICRONOVA) || defined(FLOW_PROFILE_WAVESHARE)
    LOGI("mqtt heap buffers deferred until startup release");
#else
    (void)allocateScratchBuffers_();
    (void)allocateRxQueue_();
#endif

    netReady_ = dataStore_ ? networkReady(*dataStore_) : false;
    netReadyTs_ = millis();
    retryCount_ = 0;
    retryDelayMs_ = Limits::Mqtt::Backoff::MinMs;

    setState_(cfgData_.enabled ? MQTTState::WaitingNetwork : MQTTState::Disabled);
}

void MQTTModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgProducer_) {
        cfgProducer_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgProducer_) {
        cfgProducer_->configure(this,
                                ProducerIdConfig,
                                kMqttCfgRoutes,
                                (uint8_t)(sizeof(kMqttCfgRoutes) / sizeof(kMqttCfgRoutes[0])),
                                services);
    }
    runtimeProducerCore_.configure(&mqttSvc_);
    runtimeProducerCore_.rebuildRoutes();
}

void MQTTModule::onStart(ConfigStore&, ServiceRegistry&)
{
#if defined(FLOW_PROFILE_MICRONOVA) || defined(FLOW_PROFILE_WAVESHARE)
    (void)allocateScratchBuffers_();
    (void)allocateRxQueue_();
#endif
    runtimeProducerCore_.rebuildRoutes();
    LOGI("runtime routes ready count=%u", (unsigned)runtimeProducerCore_.routeCount());
}

void MQTTModule::loop()
{
    if (!cfgData_.enabled) {
        if (state_ != MQTTState::Disabled) {
            stopClient_(true);
            setState_(MQTTState::Disabled);
        }
        vTaskDelay(pdMS_TO_TICKS(Limits::Mqtt::Timing::DisabledDelayMs));
        return;
    }

    if (!scratch_ && !allocateScratchBuffers_()) {
        vTaskDelay(pdMS_TO_TICKS(Limits::Mqtt::Timing::DisabledDelayMs));
        return;
    }
    if (!rxQ_ && !allocateRxQueue_()) {
        vTaskDelay(pdMS_TO_TICKS(Limits::Mqtt::Timing::DisabledDelayMs));
        return;
    }

    const uint32_t nowMs = millis();
    updateAndReportQueueOccupancy_(nowMs);
    tickProducers_(nowMs);

    switch (state_) {
        case MQTTState::Disabled:
            setState_(MQTTState::WaitingNetwork);
            break;

        case MQTTState::WaitingNetwork:
            if (!netReady_) break;
            if ((millis() - netReadyTs_) >= Limits::Mqtt::Timing::NetWarmupMs) {
                connectMqtt_();
            }
            break;

        case MQTTState::Connecting:
            if ((millis() - stateTs_) > Limits::Mqtt::Timing::ConnectTimeoutMs) {
                stopClient_(true);
                setState_(MQTTState::ErrorWait);
            }
            break;

        case MQTTState::Connected: {
            if (rxQ_) {
                RxMsg msg{};
                while (xQueueReceive(rxQ_, &msg, 0) == pdTRUE) {
                    processRx_(msg);
                }
            }

            const uint32_t now = millis();
            for (uint8_t i = 0; i < runtimePublisherCount_; ++i) {
                RuntimePublisher& p = runtimePublishers_[i];
                if (!p.used || p.periodMs == 0U) continue;
                if ((int32_t)(now - p.nextDueMs) < 0) continue;

                p.nextDueMs = now + p.periodMs;
                (void)enqueue(ProducerIdStatus, p.messageId, MqttPublishPriority::Normal, 0);
            }

            processJobs_(now);
            break;
        }

        case MQTTState::ErrorWait:
            if (!netReady_) {
                setState_(MQTTState::WaitingNetwork);
                break;
            }

            if ((millis() - stateTs_) >= retryDelayMs_) {
                ++retryCount_;
                uint32_t next = retryDelayMs_;
                if (next < Limits::Mqtt::Backoff::Step1Ms) next = Limits::Mqtt::Backoff::Step1Ms;
                else if (next < Limits::Mqtt::Backoff::Step2Ms) next = Limits::Mqtt::Backoff::Step2Ms;
                else if (next < Limits::Mqtt::Backoff::Step3Ms) next = Limits::Mqtt::Backoff::Step3Ms;
                else if (next < Limits::Mqtt::Backoff::Step4Ms) next = Limits::Mqtt::Backoff::Step4Ms;
                else next = Limits::Mqtt::Backoff::MaxMs;

                next = clampU32(next, Limits::Mqtt::Backoff::MinMs, Limits::Mqtt::Backoff::MaxMs);
                retryDelayMs_ = jitterMs(next, Limits::Mqtt::Backoff::JitterPct);
                setState_(MQTTState::WaitingNetwork);
            }
            break;
    }

    vTaskDelay(pdMS_TO_TICKS(Limits::Mqtt::Timing::LoopDelayMs));
}
