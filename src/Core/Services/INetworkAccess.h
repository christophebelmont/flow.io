#pragma once
/**
 * @file INetworkAccess.h
 * @brief Network reachability service for web transport (STA/AP).
 */

#include <stddef.h>
#include <stdint.h>

enum class NetworkAccessMode : uint8_t {
    None = 0,
    Station = 1,
    AccessPoint = 2
};

struct NetworkAccessService {
    bool (*isWebReachable)(void* ctx);
    NetworkAccessMode (*mode)(void* ctx);
    bool (*getIP)(void* ctx, char* out, size_t len);
    bool (*notifyWifiConfigChanged)(void* ctx);
    bool (*notifyShutdownPending)(void* ctx);
    void* ctx;
};
