#pragma once
/**
 * @file IFirmwareUpdate.h
 * @brief Firmware update service interface.
 */

#include <stddef.h>
#include <stdint.h>

enum class FirmwareUpdateTarget : uint8_t {
    Nextion = 2,
    Waveshare = 3,
    Spiffs = 4
};

enum class FirmwareManifestCheckState : uint8_t {
    Idle = 0,
    Queued,
    Downloading,
    Ready,
    Error
};

struct FirmwareManifestCheckSnapshot {
    uint32_t requestId = 0;
    FirmwareManifestCheckState state = FirmwareManifestCheckState::Idle;
    uint32_t updatedAtMs = 0;
    size_t payloadLen = 0;
    char manifestUrl[192] = {0};
    char message[120] = {0};
};

struct FirmwareUpdateService {
    bool (*start)(void* ctx, FirmwareUpdateTarget target, const char* url, char* errOut, size_t errOutLen);
    bool (*statusJson)(void* ctx, char* out, size_t outLen);
    bool (*isBusy)(void* ctx);
    bool (*configJson)(void* ctx, char* out, size_t outLen);
    bool (*startManifestCheck)(void* ctx, uint32_t* requestIdOut, char* errOut, size_t errOutLen);
    bool (*manifestCheckStatus)(void* ctx, uint32_t requestId, FirmwareManifestCheckSnapshot* out);
    bool (*copyManifestResult)(void* ctx,
                               uint32_t requestId,
                               char* out,
                               size_t outLen,
                               size_t* copiedLenOut);
    bool (*setConfig)(void* ctx,
                      const char* updateHost,
                      const char* updatePath,
                      char* errOut,
                      size_t errOutLen);
    void* ctx;
};
