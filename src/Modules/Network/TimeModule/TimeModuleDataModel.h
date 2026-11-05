#pragma once
/**
 * @file TimeModuleDataModel.h
 * @brief Time module runtime data model contribution.
 */

/** @brief Time runtime status. */
struct TimeRuntimeData {
    bool timeReady = false;
    uint8_t source = 0;
    char sourceText[16] = "none";
    uint8_t quality = 0;
    char qualityText[20] = "invalid";
    uint64_t lastNtpSyncUtc = 0;
    uint64_t lastRtcSyncUtc = 0;
};

// MODULE_DATA_MODEL: TimeRuntimeData time
