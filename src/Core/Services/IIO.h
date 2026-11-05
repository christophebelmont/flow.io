#pragma once
/**
 * @file IIO.h
 * @brief Unified I/O service interfaces.
 */
#include <stdint.h>
#include <stddef.h>

/** Numeric endpoint identifier shared across modules. */
typedef uint16_t IoId;
/** Monotonic sequence number for I/O cycles. */
typedef uint32_t IoSeq;

/** Invalid endpoint identifier sentinel. */
constexpr IoId IO_ID_INVALID = 0xFFFFu;
/** Reserved base for digital outputs. */
constexpr IoId IO_ID_DO_BASE = 0;
/** Reserved base for digital inputs. */
constexpr IoId IO_ID_DI_BASE = 64;
/** Reserved base for analog inputs. */
constexpr IoId IO_ID_AI_BASE = 192;
/** Hard upper bound used by static service implementations. */
constexpr uint8_t IO_SVC_MAX_ENDPOINTS = 40;
/** Max length for display names in metadata payloads. */
constexpr size_t IO_NAME_MAX_LEN = 24;
/** Max number of changed ids tracked per cycle. */
constexpr uint8_t IO_MAX_CHANGED_IDS = 40;

/** Result code for IOServiceV2 calls. */
enum IoStatus : uint8_t {
    IO_OK = 0,
    IO_ERR_INVALID_ARG = 1,
    IO_ERR_UNKNOWN_ID = 2,
    IO_ERR_TYPE_MISMATCH = 3,
    IO_ERR_READ_ONLY = 4,
    IO_ERR_NOT_READY = 5,
    IO_ERR_HW = 6,
    IO_ERR_DISABLED = 7
};

/** Runtime value type transported by I/O APIs. */
enum IoValueType : uint8_t {
    IO_VAL_BOOL = 0,
    IO_VAL_FLOAT = 1,
    IO_VAL_INT32 = 2
};

/** Logical I/O endpoint family. */
enum IoKind : uint8_t {
    IO_KIND_DIGITAL_IN = 0,
    IO_KIND_DIGITAL_OUT = 1,
    IO_KIND_ANALOG_IN = 2
};

/** Physical/backend origin of an endpoint. */
enum IoBackend : uint8_t {
    IO_BACKEND_GPIO = 0,
    IO_BACKEND_PCF8574 = 1,
    IO_BACKEND_ADS1115_INT = 2,
    IO_BACKEND_ADS1115_EXT_DIFF = 3,
    IO_BACKEND_DS18B20 = 4,
    IO_BACKEND_SHT40 = 5,
    IO_BACKEND_BMP280 = 6,
    IO_BACKEND_BME680 = 7,
    IO_BACKEND_INA226 = 8,
    IO_BACKEND_TCA9554 = 9,
    IO_BACKEND_MCP23017 = 10
};

/** Endpoint capability bitmask. */
enum IoCap : uint8_t {
    IO_CAP_R = 1,
    IO_CAP_W = 2
};

/** Typed runtime value snapshot used by generic readers. */
struct IoValue {
    uint8_t valid = 0;
    uint8_t reserved = 0;
    uint8_t type = IO_VAL_FLOAT;
    uint32_t tsMs = 0;
    IoSeq cycleSeq = 0;
    union {
        uint8_t b;
        float f;
        int32_t i32;
    } v{};
};

/** Static metadata describing one endpoint identity and capabilities. */
struct IoEndpointMeta {
    IoId id = IO_ID_INVALID;
    uint8_t kind = IO_KIND_DIGITAL_IN;
    uint8_t valueType = IO_VAL_BOOL;
    uint8_t backend = IO_BACKEND_GPIO;
    uint8_t channel = 0;
    /** Profile physical binding identifier; 0 means that the endpoint is unbound. */
    uint16_t bindingPort = 0;
    uint8_t capabilities = 0;
    char name[IO_NAME_MAX_LEN] = {0};
    int32_t precision = 0;
    float minValid = 0.0f;
    float maxValid = 0.0f;
};

/** Runtime availability of one configured I/O endpoint. */
enum IoRuntimeState : uint8_t {
    IO_RUNTIME_SLEEPING = 0,
    IO_RUNTIME_ACTIVE = 1,
    IO_RUNTIME_MANUALLY_DISABLED = 2,
    IO_RUNTIME_ERROR = 3
};

/** Cause associated with IoRuntimeStatus::state. */
enum IoRuntimeReason : uint8_t {
    IO_RUNTIME_REASON_NONE = 0,
    IO_RUNTIME_REASON_UNBOUND = 1,
    IO_RUNTIME_REASON_IO_MODULE_DISABLED = 2,
    IO_RUNTIME_REASON_DRIVER_DISABLED = 3,
    IO_RUNTIME_REASON_EXPANDER_DISABLED = 4,
    IO_RUNTIME_REASON_HARDWARE_NOT_DETECTED = 5,
    IO_RUNTIME_REASON_DRIVER_INIT_FAILED = 6,
    IO_RUNTIME_REASON_READ_FAILED = 7
};

/** Runtime status kept independently from endpoint hardware availability. */
struct IoRuntimeStatus {
    IoId id = IO_ID_INVALID;
    uint8_t state = IO_RUNTIME_SLEEPING;
    uint8_t reason = IO_RUNTIME_REASON_NONE;
    uint8_t expanderId = 0xFFU;
    uint8_t reserved = 0U;
};

constexpr const char* ioRuntimeStateName(uint8_t state)
{
    switch (state) {
        case IO_RUNTIME_SLEEPING: return "sleeping";
        case IO_RUNTIME_ACTIVE: return "active";
        case IO_RUNTIME_MANUALLY_DISABLED: return "manually_disabled";
        case IO_RUNTIME_ERROR: return "error";
        default: return "sleeping";
    }
}

constexpr const char* ioRuntimeReasonName(uint8_t reason)
{
    switch (reason) {
        case IO_RUNTIME_REASON_NONE: return "";
        case IO_RUNTIME_REASON_UNBOUND: return "unbound";
        case IO_RUNTIME_REASON_IO_MODULE_DISABLED: return "io_module_disabled";
        case IO_RUNTIME_REASON_DRIVER_DISABLED: return "driver_disabled";
        case IO_RUNTIME_REASON_EXPANDER_DISABLED: return "expander_disabled";
        case IO_RUNTIME_REASON_HARDWARE_NOT_DETECTED: return "hardware_not_detected";
        case IO_RUNTIME_REASON_DRIVER_INIT_FAILED: return "driver_init_failed";
        case IO_RUNTIME_REASON_READ_FAILED: return "read_failed";
        default: return "unknown";
    }
}

/** Per-cycle change summary exposed by IOServiceV2::lastCycle. */
struct IoCycleInfo {
    IoSeq seq = 0;
    uint32_t tsMs = 0;
    uint8_t changedCount = 0;
    IoId changedIds[IO_MAX_CHANGED_IDS] = {0};
};

/** Sensor validity reason bitmask. Disabled sensors are not considered faults. */
enum IoSensorInvalidReason : uint16_t {
    IO_SENSOR_INVALID_NONE = 0,
    IO_SENSOR_INVALID_DISABLED = 1U << 0,
    IO_SENSOR_INVALID_UNKNOWN_ID = 1U << 1,
    IO_SENSOR_INVALID_NOT_SENSOR = 1U << 2,
    IO_SENSOR_INVALID_NO_BINDING = 1U << 3,
    IO_SENSOR_INVALID_DRIVER_DISABLED = 1U << 4,
    IO_SENSOR_INVALID_NOT_READY = 1U << 5,
    IO_SENSOR_INVALID_NO_VALUE = 1U << 6,
    IO_SENSOR_INVALID_TYPE = 1U << 7
};

/** Status of a sensor-like IO endpoint. */
struct IoSensorStatus {
    IoId id = IO_ID_INVALID;
    uint8_t kind = IO_KIND_ANALOG_IN;
    uint8_t enabled = 0;
    uint8_t valid = 0;
    uint8_t reserved = 0;
    uint16_t invalidReasons = IO_SENSOR_INVALID_NONE;
    uint32_t tsMs = 0;
};

/**
 * @brief Unified static I/O service contract.
 *
 * Other modules must use numeric IoId access through this service.
 * Device names are metadata only for display/diagnostics.
 */
struct IOServiceV2 {
    /** Number of endpoints currently exposed by the service. */
    uint8_t (*count)(void* ctx);
    /** Resolve endpoint id by a compact index [0..count). */
    IoStatus (*idAt)(void* ctx, uint8_t index, IoId* outId);
    /** Fetch static metadata for a given endpoint id. */
    IoStatus (*meta)(void* ctx, IoId id, IoEndpointMeta* outMeta);
    /** Fetch runtime availability for a configured endpoint. */
    IoStatus (*runtimeStatus)(void* ctx, IoId id, IoRuntimeStatus* outStatus);
    /** Fetch runtime availability for one profile binding port. */
    IoStatus (*bindingPortStatus)(void* ctx, uint16_t bindingPort, IoRuntimeStatus* outStatus);
    /** Read the latest typed value for any endpoint kind. */
    IoStatus (*readValue)(void* ctx, IoId id, IoValue* outValue);

    /** Read the latest digital value (DI or DO). */
    IoStatus (*readDigital)(void* ctx, IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq);
    /** Write a digital output endpoint. */
    IoStatus (*writeDigital)(void* ctx, IoId id, uint8_t on, uint32_t tsMs);
    /** Read the latest analog value (AI). */
    IoStatus (*readAnalog)(void* ctx, IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq);

    /** Optional explicit tick hook for modules driving scheduled acquisition. */
    IoStatus (*tick)(void* ctx, uint32_t nowMs);
    /** Retrieve last completed cycle information. */
    IoStatus (*lastCycle)(void* ctx, IoCycleInfo* outCycle);
    /** Evaluate whether one sensor endpoint is enabled and currently valid. */
    IoStatus (*sensorStatus)(void* ctx, IoId id, IoSensorStatus* outStatus);
    /** List enabled sensor endpoints that are currently invalid. */
    IoStatus (*listInvalidSensors)(void* ctx, IoId* outIds, uint8_t maxIds, uint8_t* outCount);

    /** Opaque implementation context. */
    void* ctx;
};
