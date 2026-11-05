#include "Modules/Network/MQTTModule/RuntimeProducer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <new>
#include <string.h>

namespace {
template <typename T>
T* allocPsramArray_(size_t count)
{
    void* mem = heap_caps_malloc(sizeof(T) * count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(T) * count, MALLOC_CAP_8BIT);
    if (!mem) return nullptr;

    T* out = static_cast<T*>(mem);
    for (size_t i = 0; i < count; ++i) {
        new (&out[i]) T();
    }
    return out;
}

bool* allocPsramBoolArray_(size_t count)
{
    void* mem = heap_caps_calloc(count, sizeof(bool), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_calloc(count, sizeof(bool), MALLOC_CAP_8BIT);
    return static_cast<bool*>(mem);
}
} // namespace

bool RuntimeProducer::ensureStorage_()
{
    if (!routes_) routes_ = allocPsramArray_<Route>(Limits::MaxRuntimeRoutes);
    if (!initialFullSnapshotDone_) initialFullSnapshotDone_ = allocPsramBoolArray_(Limits::MaxRuntimeRoutes);
    return routes_ && initialFullSnapshotDone_;
}

void RuntimeProducer::configure(const MqttService* mqttSvc,
                                InitialFullSnapshotCallback initialFullSnapshotCb,
                                void* initialFullSnapshotCtx)
{
    mqttSvc_ = mqttSvc;
    initialFullSnapshotCb_ = initialFullSnapshotCb;
    initialFullSnapshotCtx_ = initialFullSnapshotCtx;
    (void)ensureStorage_();
}

bool RuntimeProducer::registerProvider(const IRuntimeSnapshotProvider* provider)
{
    if (!ensureStorage_()) return false;
    if (!provider) return false;
    if (providerCount_ >= MaxProviders) return false;

    for (uint8_t i = 0; i < providerCount_; ++i) {
        if (providers_[i] == provider) return true;
    }

    providers_[providerCount_++] = provider;
    rebuildRoutes();
    return true;
}

void RuntimeProducer::rebuildRoutes()
{
    if (!ensureStorage_()) {
        routeCount_ = 0;
        return;
    }
    routeCount_ = 0;

    for (uint8_t p = 0; p < providerCount_; ++p) {
        const IRuntimeSnapshotProvider* provider = providers_[p];
        if (!provider) continue;

        const uint8_t count = provider->runtimeSnapshotCount();
        for (uint8_t i = 0; i < count; ++i) {
            if (routeCount_ >= Limits::MaxRuntimeRoutes) return;

            const char* suffix = provider->runtimeSnapshotSuffix(i);
            if (!suffix || suffix[0] == '\0') continue;

            Route& route = routes_[routeCount_++];
            route = Route{};
            route.provider = provider;
            route.snapshotIdx = i;
            route.routeClass = provider->runtimeSnapshotClass(i);
            route.pending = true;
            route.force = true;
            snprintf(route.suffix, sizeof(route.suffix), "%s", suffix);
        }
    }
}

void RuntimeProducer::markRoutePending_(uint8_t idx, bool force)
{
    if (idx >= routeCount_) return;
    Route& route = routes_[idx];
    route.pending = true;
    route.force = route.force || force;
}

bool RuntimeProducer::enqueueRoute_(uint8_t idx)
{
    if (!mqttSvc_ || !mqttSvc_->enqueue) return false;
    if (idx >= routeCount_) return false;

    const Route& route = routes_[idx];
    const MqttPublishPriority prio = (route.routeClass == RuntimeRouteClass::ActuatorImmediate)
        ? MqttPublishPriority::High
        : MqttPublishPriority::Normal;

    return mqttSvc_->enqueue(mqttSvc_->ctx, ProducerId, idx, (uint8_t)prio, 0);
}

void RuntimeProducer::completeInitialFullSnapshotRoute_(uint8_t idx)
{
    if (!initialFullSnapshotActive_) return;
    if (idx >= routeCount_) return;
    if (initialFullSnapshotDone_[idx]) return;

    initialFullSnapshotDone_[idx] = true;
    if (initialFullSnapshotRemaining_ > 0U) {
        --initialFullSnapshotRemaining_;
    }
    if (initialFullSnapshotRemaining_ == 0U) {
        notifyInitialFullSnapshotComplete_();
    }
}

void RuntimeProducer::notifyInitialFullSnapshotComplete_()
{
    initialFullSnapshotActive_ = false;
    if (initialFullSnapshotCb_) {
        initialFullSnapshotCb_(initialFullSnapshotCtx_);
    }
}

void RuntimeProducer::onConnected()
{
    if (!ensureStorage_()) return;
    memset(initialFullSnapshotDone_, 0, sizeof(bool) * Limits::MaxRuntimeRoutes);
    initialFullSnapshotRemaining_ = routeCount_;
    initialFullSnapshotActive_ = true;
    if (routeCount_ == 0U) {
        notifyInitialFullSnapshotComplete_();
        return;
    }

    for (uint8_t i = 0; i < routeCount_; ++i) {
        markRoutePending_(i, true);
        enqueueRoute_(i);
    }
}

RuntimeProducer::DataChangeStats RuntimeProducer::onDataChanged(DataKey key)
{
    DataChangeStats stats{};
    stats.routeCount = routeCount_;
    for (uint8_t i = 0; i < routeCount_; ++i) {
        Route& route = routes_[i];
        if (!route.provider) continue;
        ++stats.checked;
        if (!route.provider->runtimeSnapshotAffectsKey(route.snapshotIdx, key)) continue;
        ++stats.matched;
        if (route.pending || route.force) ++stats.pendingAlready;
        markRoutePending_(i, false);
        const uint32_t t0 = micros();
        const bool ok = enqueueRoute_(i);
        stats.enqueueUs += (uint32_t)(micros() - t0);
        if (ok) {
            ++stats.enqueueOk;
        } else {
            ++stats.enqueueFail;
        }
    }
    return stats;
}

MqttBuildResult RuntimeProducer::buildMessage(uint16_t messageId, MqttBuildContext& ctx)
{
    if (messageId >= routeCount_) return MqttBuildResult::NoLongerNeeded;

    Route& route = routes_[messageId];
    if (!route.provider) return MqttBuildResult::NoLongerNeeded;
    if (!route.pending && !route.force) return MqttBuildResult::NoLongerNeeded;
    if (!route.provider->runtimeSnapshotSuffix(route.snapshotIdx)) {
        route.pending = false;
        route.force = false;
        return MqttBuildResult::NoLongerNeeded;
    }

    const uint32_t nowMs = millis();
    if (!route.force && route.routeClass == RuntimeRouteClass::NumericThrottled) {
        if ((uint32_t)(nowMs - route.lastPublishMs) < NumericThrottleMs) {
            return MqttBuildResult::RetryLater;
        }
    }

    uint32_t ts = 0;
    if (!route.provider->buildRuntimeSnapshot(route.snapshotIdx, ctx.payload, ctx.payloadCapacity, ts)) {
        return MqttBuildResult::RetryLater;
    }

    const uint32_t effectiveTs = (ts == 0U) ? 1U : ts;
    if (!route.force && route.lastPublishedTs != 0U && effectiveTs <= route.lastPublishedTs) {
        route.pending = false;
        return MqttBuildResult::NoLongerNeeded;
    }

    if (!mqttSvc_ || !mqttSvc_->formatTopic) return MqttBuildResult::PermanentError;
    mqttSvc_->formatTopic(mqttSvc_->ctx, route.suffix, ctx.topic, ctx.topicCapacity);
    if (ctx.topic[0] == '\0') return MqttBuildResult::PermanentError;

    ctx.topicLen = (uint16_t)strnlen(ctx.topic, ctx.topicCapacity);
    ctx.payloadLen = (uint16_t)strnlen(ctx.payload, ctx.payloadCapacity);
    ctx.qos = 0;
    ctx.retain = false;

    route.lastBuiltTs = effectiveTs;
    return MqttBuildResult::Ready;
}

void RuntimeProducer::onMessagePublished(uint16_t messageId)
{
    if (messageId >= routeCount_) return;

    Route& route = routes_[messageId];
    route.lastPublishedTs = route.lastBuiltTs;
    route.lastPublishMs = millis();
    route.pending = false;
    route.force = false;
    completeInitialFullSnapshotRoute_((uint8_t)messageId);
}

void RuntimeProducer::onMessageDropped(uint16_t messageId)
{
    if (messageId >= routeCount_) return;
    routes_[messageId].pending = false;
    completeInitialFullSnapshotRoute_((uint8_t)messageId);
}
