#pragma once
/**
 * @file TimeRuntime.h
 * @brief Time runtime helpers and keys.
 */

#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/DataKeys.h"
#include "Core/Services/ITime.h"
#include <cstdio>
#include <cstring>

// RUNTIME_PUBLIC

// Data keys for time runtime values.
constexpr DataKey DATAKEY_TIME_READY = DataKeys::TimeReady;

static inline bool timeReady(const DataStore& ds)
{
    return ds.data().time.timeReady;
}

static inline void setTimeReady(DataStore& ds, bool ready)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.time.timeReady == ready) return;
    rt.time.timeReady = ready;
    ds.notifyChanged(DATAKEY_TIME_READY);
}

static inline TimeSource timeSource(const DataStore& ds)
{
    return (TimeSource)ds.data().time.source;
}

static inline const char* timeSourceText(const DataStore& ds)
{
    return ds.data().time.sourceText;
}

static inline TimeQuality timeQuality(const DataStore& ds)
{
    return (TimeQuality)ds.data().time.quality;
}

static inline const char* timeQualityText(const DataStore& ds)
{
    return ds.data().time.qualityText;
}

static inline uint64_t timeLastNtpSyncUtc(const DataStore& ds)
{
    return ds.data().time.lastNtpSyncUtc;
}

static inline uint64_t timeLastRtcSyncUtc(const DataStore& ds)
{
    return ds.data().time.lastRtcSyncUtc;
}

static inline void setTimeStatus(DataStore& ds,
                                 TimeSource source,
                                 const char* sourceText,
                                 TimeQuality quality,
                                 const char* qualityText,
                                 uint64_t lastNtpSyncUtc,
                                 uint64_t lastRtcSyncUtc)
{
    RuntimeData& rt = ds.dataMutable();
    const uint8_t sourceValue = (uint8_t)source;
    const uint8_t qualityValue = (uint8_t)quality;
    const char* safeSourceText = sourceText ? sourceText : "none";
    const char* safeQualityText = qualityText ? qualityText : "invalid";
    if (rt.time.source == sourceValue &&
        rt.time.quality == qualityValue &&
        rt.time.lastNtpSyncUtc == lastNtpSyncUtc &&
        rt.time.lastRtcSyncUtc == lastRtcSyncUtc &&
        strncmp(rt.time.sourceText, safeSourceText, sizeof(rt.time.sourceText)) == 0 &&
        strncmp(rt.time.qualityText, safeQualityText, sizeof(rt.time.qualityText)) == 0) {
        return;
    }
    rt.time.source = sourceValue;
    snprintf(rt.time.sourceText, sizeof(rt.time.sourceText), "%s", safeSourceText);
    rt.time.quality = qualityValue;
    snprintf(rt.time.qualityText, sizeof(rt.time.qualityText), "%s", safeQualityText);
    rt.time.lastNtpSyncUtc = lastNtpSyncUtc;
    rt.time.lastRtcSyncUtc = lastRtcSyncUtc;
    ds.notifyChanged(DATAKEY_TIME_READY);
}

static inline void setTimeSource(DataStore& ds, TimeSource source, const char* text)
{
    const TimeQuality quality = timeQuality(ds);
    setTimeStatus(ds,
                  source,
                  text,
                  quality,
                  timeQualityText(ds),
                  timeLastNtpSyncUtc(ds),
                  timeLastRtcSyncUtc(ds));
}
