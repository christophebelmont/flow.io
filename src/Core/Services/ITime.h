#pragma once
/**
 * @file ITime.h
 * @brief Generic time synchronization service interface.
 */
#include <stddef.h>
#include <stdint.h>

/** @brief Time synchronization state. */
enum class TimeSyncState : uint8_t { Disabled, WaitingNetwork, Syncing, Synced, ErrorWait };

/** @brief Selected time reference source. */
enum class TimeSource : uint8_t {
    None = 0,
    Ntp = 1,
    InternalRtc = 2,
    NextionManual = 3
};

/** @brief Trust level of the currently exposed system time. */
enum class TimeQuality : uint8_t {
    Invalid = 0,
    RtcUntrusted = 1,
    Manual = 2,
    RtcTrusted = 3,
    NtpSynced = 4
};

/** @brief Complete UTC-oriented time state exposed by the Time service. */
struct TimeState {
    bool valid = false;
    TimeQuality quality = TimeQuality::Invalid;
    TimeSource source = TimeSource::None;
    uint64_t currentTimeUtc = 0;
    uint64_t lastNtpSyncUtc = 0;
    uint64_t lastRtcSyncUtc = 0;
    int64_t lastUpdateMonotonicMs = 0;
};

/** @brief Backward compatibility alias. */
using NTPState = TimeSyncState;

/** @brief Service interface for time synchronization and formatting. */
struct TimeService {
    TimeSyncState (*state)(void* ctx);
    bool (*isSynced)(void* ctx);
    uint64_t (*epoch)(void* ctx);
    bool (*formatLocalTime)(void* ctx, char* out, size_t len);
    void* ctx;
    bool (*setExternalEpoch)(void* ctx, uint64_t epochSec);
    bool (*isExternalRtc)(void* ctx);
    TimeSource (*source)(void* ctx);
    const char* (*sourceName)(void* ctx);
    TimeQuality (*quality)(void* ctx);
    const char* (*qualityName)(void* ctx);
    bool (*currentState)(void* ctx, TimeState* out);
    bool (*setManualEpoch)(void* ctx, uint64_t epochSec);
    bool (*isPlausible)(uint64_t epochSec);
    bool (*weekStartMonday)(void* ctx);
};
