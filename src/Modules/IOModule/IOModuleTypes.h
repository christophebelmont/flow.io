#pragma once
/**
 * @file IOModuleTypes.h
 * @brief Public POD types used to describe IO topology without runtime allocation.
 */

#include <stdint.h>

#include "Core/Services/IIO.h"
#include "Core/WokwiDefaultOverrides.h"

typedef uint16_t BindingPointId;
typedef BindingPointId PhysicalPortId;
typedef uint8_t IOExpanderId;
constexpr BindingPointId BINDING_POINT_NONE = 0u;
constexpr PhysicalPortId IO_PORT_INVALID = BINDING_POINT_NONE;
constexpr IOExpanderId IO_EXPANDER_INVALID = 0xFFu;
constexpr uint8_t IO_MAX_EXPANDERS = 4u;

struct IOModuleConfig {
    bool enabled = FLOW_WIRDEF_IO_EN;
    int32_t i2cSda = FLOW_WIRDEF_IO_SDA;
    int32_t i2cScl = FLOW_WIRDEF_IO_SCL;
    int32_t adsPollMs = FLOW_MODDEF_IO_ADS;
    int32_t dsPollMs = FLOW_MODDEF_IO_DS;
    int32_t digitalPollMs = FLOW_MODDEF_IO_DIN;
    uint8_t adsInternalAddr = FLOW_WIRDEF_IO_AIAD;
    uint8_t adsExternalAddr = FLOW_WIRDEF_IO_AEAD;
    int32_t adsGain = FLOW_MODDEF_IO_AGAI;
    int32_t adsRate = FLOW_MODDEF_IO_ARAT;
    bool sht40Enabled =
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
        true;
#else
        false;
#endif
    uint8_t sht40Address = 0x44;
    int32_t sht40PollMs = 2000;
    bool bmp280Enabled =
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
        true;
#else
        false;
#endif
    uint8_t bmp280Address = 0x76;
    int32_t bmp280PollMs = 1000;
    bool bme680Enabled = true;
    uint8_t bme680Address = 0x77;
    int32_t bme680PollMs = 2000;
    bool ina226Enabled =
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
        true;
#else
        false;
#endif
    uint8_t ina226Address = 0x40;
    int32_t ina226PollMs = 500;
    float ina226ShuntOhms = 0.1f;
    bool traceEnabled = FLOW_MODDEF_IO_TREN;
    int32_t tracePeriodMs = FLOW_MODDEF_IO_TRMS;
};

enum IOAnalogSource : uint8_t {
    IO_SRC_ADS_INTERNAL_SINGLE = 0,
    IO_SRC_ADS_EXTERNAL_DIFF = 1,
    IO_SRC_DS18_WATER = 2,
    IO_SRC_DS18_AIR = 3,
    IO_SRC_SHT40 = 4,
    IO_SRC_BMP280 = 5,
    IO_SRC_BME680 = 6,
    IO_SRC_INA226 = 7,
    IO_SRC_COUNT = 8
};

constexpr uint8_t IO_ANALOG_SOURCE_INVALID = 0xFFu;

enum IOBindingPortKind : uint8_t {
    IO_PORT_KIND_NONE = 0,
    IO_PORT_KIND_GPIO_INPUT = 1,
    IO_PORT_KIND_GPIO_OUTPUT = 2,
    IO_PORT_KIND_PCF8574_OUTPUT = 3,
    IO_PORT_KIND_ADS_INTERNAL_SINGLE = 4,
    IO_PORT_KIND_ADS_EXTERNAL_DIFF = 5,
    IO_PORT_KIND_DS18_WATER = 6,
    IO_PORT_KIND_DS18_AIR = 7,
    IO_PORT_KIND_INA226 = 8,
    IO_PORT_KIND_SHT40 = 9,
    IO_PORT_KIND_BMP280 = 10,
    IO_PORT_KIND_BME680 = 11,
    IO_PORT_KIND_TCA9554_OUTPUT = 12,
    IO_PORT_KIND_MCP23017_OUTPUT = 13,
    IO_PORT_KIND_MCP23017_INPUT = 14
};

enum IOExpanderKind : uint8_t {
    IO_EXPANDER_KIND_NONE = 0,
    IO_EXPANDER_KIND_PCF8574 = 1,
    IO_EXPANDER_KIND_TCA9554 = 2,
    IO_EXPANDER_KIND_MCP23017 = 3
};

struct IOExpanderSpec {
    IOExpanderId expanderId = IO_EXPANDER_INVALID;
    uint8_t kind = IO_EXPANDER_KIND_NONE;
    bool enabled = false;
    uint8_t address = 0;
    uint8_t maskDefault = 0;
    /** Fixed board-level output inversion; not user-configurable. */
    bool outputsInverted = false;
};

struct IOExpanderConfig {
    bool enabled = false;
    uint8_t address = 0;
    uint8_t maskDefault = 0;
};

struct IOBindingPortSpec {
    PhysicalPortId portId = IO_PORT_INVALID;
    uint8_t kind = IO_PORT_KIND_NONE;
    uint8_t channel = 0;
    IOExpanderId expanderId = IO_EXPANDER_INVALID;
    // Physical marking exposed by the board/profile (for example GPIO04, GPA0 or EXIO1).
    const char* boardLabel = nullptr;
};

typedef void (*IOAnalogValueCallback)(void* ctx, float value);
typedef void (*IODigitalValueCallback)(void* ctx, bool value);
typedef void (*IODigitalCounterValueCallback)(void* ctx, int32_t value);

enum IODigitalPullMode : uint8_t {
    IO_PULL_NONE = 0,
    IO_PULL_UP = 1,
    IO_PULL_DOWN = 2
};

enum IODigitalInputMode : uint8_t {
    IO_DIGITAL_INPUT_STATE = 0,
    IO_DIGITAL_INPUT_COUNTER = 1
};

enum IODigitalEdgeMode : uint8_t {
    IO_EDGE_FALLING = 0,
    IO_EDGE_RISING = 1,
    IO_EDGE_BOTH = 2
};

enum class IOOutputStartupPolicy : uint8_t {
    ApplyInitial = 0,
    PreserveHardwareState = 1
};

struct IOAnalogDefinition {
    char id[24] = {0};
    /** Required explicit AI id in [IO_ID_AI_BASE..IO_ID_AI_BASE+MAX_ANALOG_ENDPOINTS). */
    IoId ioId = IO_ID_INVALID;
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    float c0 = 1.0f;
    float c1 = 0.0f;
    int32_t precision = 1;
    IOAnalogValueCallback onValueChanged = nullptr;
    void* onValueCtx = nullptr;
};

struct IOAnalogSlotConfig {
    char name[24] = {0};
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    float c0 = 1.0f;
    float c1 = 0.0f;
    int32_t precision = 1;
};

struct IODigitalOutputDefinition {
    char id[24] = {0};
    /** Required explicit DO id in [IO_ID_DO_BASE..IO_ID_DO_BASE+MAX_DIGITAL_OUTPUTS). */
    IoId ioId = IO_ID_INVALID;
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    bool activeHigh = false;
    bool initialOn = false;
    IOOutputStartupPolicy startupPolicy = IOOutputStartupPolicy::ApplyInitial;
    bool retainOnWarmReboot = false;
    bool momentary = false;
    uint16_t pulseMs = 500;
};

struct IODigitalOutputSlotConfig {
    char name[24] = {0};
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    bool activeHigh = false;
    bool initialOn = false;
    IOOutputStartupPolicy startupPolicy = IOOutputStartupPolicy::ApplyInitial;
    bool retainOnWarmReboot = false;
    bool momentary = false;
    int32_t pulseMs = 500;
};

struct IODigitalInputSlotConfig {
    char name[24] = {0};
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    bool activeHigh = true;
    uint8_t pullMode = IO_PULL_NONE;
    uint8_t mode = IO_DIGITAL_INPUT_STATE;
    uint8_t edgeMode = IO_EDGE_RISING;
    int32_t counterDebounceUs = 0;
    float c0 = 1.0f;
    float counterTotal = 0.0f;
    int32_t precision = 0;
};

struct IODigitalInputDefinition {
    char id[24] = {0};
    /** Required explicit DI id in [IO_ID_DI_BASE..IO_ID_DI_BASE+MAX_DIGITAL_INPUTS). */
    IoId ioId = IO_ID_INVALID;
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    bool activeHigh = true;
    uint8_t pullMode = IO_PULL_NONE;
    uint8_t mode = IO_DIGITAL_INPUT_STATE;
    uint8_t edgeMode = IO_EDGE_RISING;
    uint32_t counterDebounceUs = 0;
    IODigitalValueCallback onValueChanged = nullptr;
    void* onValueCtx = nullptr;
    IODigitalCounterValueCallback onCounterChanged = nullptr;
    void* onCounterCtx = nullptr;
};
