#pragma once
/**
 * @file MQTTRuntime.h
 * @brief MQTT runtime helpers and keys.
 */

#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/DataKeys.h"

// RUNTIME_PUBLIC

// Data keys for MQTT runtime values.
constexpr DataKey DATAKEY_MQTT_READY = DataKeys::MqttReady;
constexpr DataKey DATAKEY_MQTT_RX_DROP = DataKeys::MqttRxDrop;
constexpr DataKey DATAKEY_MQTT_PARSE_FAIL = DataKeys::MqttParseFail;
constexpr DataKey DATAKEY_MQTT_HANDLER_FAIL = DataKeys::MqttHandlerFail;
constexpr DataKey DATAKEY_MQTT_OVERSIZE_DROP = DataKeys::MqttOversizeDrop;
constexpr DataKey DATAKEY_MQTT_RUNTIME_FULL_SNAPSHOT_PUBLISHED = DataKeys::MqttRuntimeFullSnapshotPublished;

static inline bool mqttReady(const DataStore& ds)
{
    return ds.data().mqtt.mqttReady;
}

static inline bool mqttRuntimeFullSnapshotPublished(const DataStore& ds)
{
    return ds.data().mqtt.runtimeFullSnapshotPublished;
}

static inline uint32_t mqttRxDrop(const DataStore& ds)
{
    return ds.data().mqtt.rxDrop;
}

static inline uint32_t mqttParseFail(const DataStore& ds)
{
    return ds.data().mqtt.parseFail;
}

static inline uint32_t mqttHandlerFail(const DataStore& ds)
{
    return ds.data().mqtt.handlerFail;
}

static inline uint32_t mqttOversizeDrop(const DataStore& ds)
{
    return ds.data().mqtt.oversizeDrop;
}

static inline void setMqttReady(DataStore& ds, bool ready)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.mqttReady == ready) return;
    rt.mqtt.mqttReady = ready;
    ds.notifyChanged(DATAKEY_MQTT_READY);
}

static inline void setMqttRuntimeFullSnapshotPublished(DataStore& ds, bool published)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.runtimeFullSnapshotPublished == published) return;
    rt.mqtt.runtimeFullSnapshotPublished = published;
    ds.notifyChanged(DATAKEY_MQTT_RUNTIME_FULL_SNAPSHOT_PUBLISHED);
}

static inline void setMqttRxDrop(DataStore& ds, uint32_t v, bool notify = false)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.rxDrop == v) return;
    rt.mqtt.rxDrop = v;
    if (notify) ds.notifyChanged(DATAKEY_MQTT_RX_DROP);
}

static inline void setMqttParseFail(DataStore& ds, uint32_t v, bool notify = false)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.parseFail == v) return;
    rt.mqtt.parseFail = v;
    if (notify) ds.notifyChanged(DATAKEY_MQTT_PARSE_FAIL);
}

static inline void setMqttHandlerFail(DataStore& ds, uint32_t v, bool notify = false)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.handlerFail == v) return;
    rt.mqtt.handlerFail = v;
    if (notify) ds.notifyChanged(DATAKEY_MQTT_HANDLER_FAIL);
}

static inline void setMqttOversizeDrop(DataStore& ds, uint32_t v, bool notify = false)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.oversizeDrop == v) return;
    rt.mqtt.oversizeDrop = v;
    if (notify) ds.notifyChanged(DATAKEY_MQTT_OVERSIZE_DROP);
}
