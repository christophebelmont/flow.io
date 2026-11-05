#pragma once
/**
 * @file IOModuleDataModel.h
 * @brief IO runtime data model contribution.
 */

#include <stdint.h>

#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
constexpr uint8_t IO_MAX_ENDPOINTS = 45;
#else
constexpr uint8_t IO_MAX_ENDPOINTS = 40;
#endif

enum IOValueType : uint8_t {
    IO_VALUE_BOOL = 0,
    IO_VALUE_FLOAT = 1,
    IO_VALUE_INT32 = 2
};

struct IOEndpointRuntime {
    bool valid = false;
    uint8_t valueType = IO_VALUE_FLOAT;
    float floatValue = 0.0f;
    bool boolValue = false;
    int32_t intValue = 0;
    uint32_t timestampMs = 0;
};

struct IORuntimeData {
    IOEndpointRuntime endpoints[IO_MAX_ENDPOINTS];
};

// MODULE_DATA_MODEL: IORuntimeData io
