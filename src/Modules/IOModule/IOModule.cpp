/**
 * @file IOModule.cpp
 * @brief Implementation file.
 */

#include "IOModule.h"
#include "IOConfigDescriptorStorage.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::IOModule)
#include "Core/ModuleLog.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/IOModule/IORuntime.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_rom_sys.h>
#include <esp_heap_caps.h>
#include <limits.h>
#include <new>
#include <stdlib.h>
#include <string.h>

IOModule::IOModule(const BoardSpec& board)
{
    applyBoardDefaults_(board);
}

void IOModule::applyBoardDefaults_(const BoardSpec& board)
{
    boardProfileName_ = board.name ? board.name : "unknown";
    const I2cBusSpec* ioBus = boardFindI2cBus(board, "io");
    if (!ioBus) return;
    boardDefaultI2cSda_ = ioBus->sdaPin;
    boardDefaultI2cScl_ = ioBus->sclPin;
    cfgData_.i2cSda = boardDefaultI2cSda_;
    cfgData_.i2cScl = boardDefaultI2cScl_;
}

void IOModule::logI2cConfigTrace_(const char* stage) const
{
    const char* sdaKeyActive = i2cSdaVar_.nvsKey ? i2cSdaVar_.nvsKey : "(null)";
    const char* sclKeyActive = i2cSclVar_.nvsKey ? i2cSclVar_.nvsKey : "(null)";

    Preferences prefs;
    bool opened = prefs.begin(NvsKeys::StorageNamespace, true);

    bool activeSdaExists = false;
    bool activeSclExists = false;
    bool legacySdaExists = false;
    bool legacySclExists = false;
    int activeSdaStored = 0;
    int activeSclStored = 0;
    int legacySdaStored = 0;
    int legacySclStored = 0;

    if (opened) {
        activeSdaExists = prefs.isKey(sdaKeyActive);
        activeSclExists = prefs.isKey(sclKeyActive);
        legacySdaExists = prefs.isKey(NvsKeys::Io::IO_SDA);
        legacySclExists = prefs.isKey(NvsKeys::Io::IO_SCL);
        activeSdaStored = prefs.getInt(sdaKeyActive, 0);
        activeSclStored = prefs.getInt(sclKeyActive, 0);
        legacySdaStored = prefs.getInt(NvsKeys::Io::IO_SDA, 0);
        legacySclStored = prefs.getInt(NvsKeys::Io::IO_SCL, 0);
        prefs.end();
    }

    const bool sdaValid = (cfgData_.i2cSda >= 0) && digitalPinIsValid((uint8_t)cfgData_.i2cSda);
    const bool sclValid = (cfgData_.i2cScl >= 0) && digitalPinIsValid((uint8_t)cfgData_.i2cScl);
    LOGI("io.i2c trace stage=%s board=%s defaults=(%ld,%ld) active_keys=(%s,%s) cfg=(%ld,%ld) valid=(%s,%s)",
         stage ? stage : "?",
         boardProfileName_ ? boardProfileName_ : "unknown",
         (long)boardDefaultI2cSda_,
         (long)boardDefaultI2cScl_,
         sdaKeyActive,
         sclKeyActive,
         (long)cfgData_.i2cSda,
         (long)cfgData_.i2cScl,
         sdaValid ? "true" : "false",
         sclValid ? "true" : "false");
    LOGI("io.i2c nvs ns=%s opened=%s active=(sda:%s/%d scl:%s/%d) legacy=(sda:%s/%d scl:%s/%d)",
         NvsKeys::StorageNamespace,
         opened ? "true" : "false",
         activeSdaExists ? "present" : "absent",
         activeSdaStored,
         activeSclExists ? "present" : "absent",
         activeSclStored,
         legacySdaExists ? "present" : "absent",
         legacySdaStored,
         legacySclExists ? "present" : "absent",
         legacySclStored);
}

namespace {
static constexpr uint8_t kIoCfgProducerId = 47;
static constexpr uint8_t kCfgBranchIo = 1;
static constexpr uint8_t kCfgBranchIoDebug = 2;
static constexpr uint8_t kCfgBranchIoA0 = 3;
static constexpr uint8_t kCfgBranchIoA1 = 4;
static constexpr uint8_t kCfgBranchIoA2 = 5;
static constexpr uint8_t kCfgBranchIoA3 = 6;
static constexpr uint8_t kCfgBranchIoA4 = 7;
static constexpr uint8_t kCfgBranchIoA5 = 8;
static constexpr uint8_t kCfgBranchIoA6 = 33;
static constexpr uint8_t kCfgBranchIoA7 = 34;
static constexpr uint8_t kCfgBranchIoA8 = 35;
static constexpr uint8_t kCfgBranchIoA9 = 36;
static constexpr uint8_t kCfgBranchIoA10 = 37;
static constexpr uint8_t kCfgBranchIoA11 = 38;
static constexpr uint8_t kCfgBranchIoA12 = 39;
static constexpr uint8_t kCfgBranchIoA13 = 40;
static constexpr uint8_t kCfgBranchIoA14 = 41;
static constexpr uint8_t kCfgBranchIoA15 = 42;
static constexpr uint8_t kCfgBranchIoA16 = 57;
static constexpr uint8_t kCfgBranchIoA17 = 58;
static constexpr uint8_t kCfgBranchIoA18 = 59;
static constexpr uint8_t kCfgBranchIoA19 = 60;
static constexpr uint8_t kCfgBranchIoA20 = 61;
static constexpr uint8_t kCfgBranchIoA21 = 62;
static constexpr uint8_t kCfgBranchIoA22 = 63;
static constexpr uint8_t kCfgBranchIoA23 = 64;
static constexpr uint8_t kCfgBranchIoA24 = 65;
static constexpr uint8_t kCfgBranchIoA25 = 66;
static constexpr uint8_t kCfgBranchIoA26 = 67;
static constexpr uint8_t kCfgBranchIoA27 = 68;
static constexpr uint8_t kCfgBranchIoA28 = 69;
static constexpr uint8_t kCfgBranchIoA29 = 70;
static constexpr uint8_t kCfgBranchIoA30 = 71;
static constexpr uint8_t kCfgBranchIoA31 = 72;
static constexpr uint8_t kCfgBranchIoD0 = 9;
static constexpr uint8_t kCfgBranchIoD1 = 10;
static constexpr uint8_t kCfgBranchIoD2 = 11;
static constexpr uint8_t kCfgBranchIoD3 = 12;
static constexpr uint8_t kCfgBranchIoD4 = 13;
static constexpr uint8_t kCfgBranchIoD5 = 14;
static constexpr uint8_t kCfgBranchIoD6 = 15;
static constexpr uint8_t kCfgBranchIoD7 = 16;
static constexpr uint8_t kCfgBranchIoD8 = 49;
static constexpr uint8_t kCfgBranchIoD9 = 50;
static constexpr uint8_t kCfgBranchIoD10 = 51;
static constexpr uint8_t kCfgBranchIoD11 = 52;
static constexpr uint8_t kCfgBranchIoD12 = 53;
static constexpr uint8_t kCfgBranchIoD13 = 54;
static constexpr uint8_t kCfgBranchIoD14 = 55;
static constexpr uint8_t kCfgBranchIoD15 = 56;

static constexpr uint8_t kCfgBranchIoI0 = 17;
static constexpr uint8_t kCfgBranchIoI1 = 18;
static constexpr uint8_t kCfgBranchIoI2 = 19;
static constexpr uint8_t kCfgBranchIoI3 = 20;
static constexpr uint8_t kCfgBranchIoI4 = 21;
static constexpr uint8_t kCfgBranchIoI5 = 44;
static constexpr uint8_t kCfgBranchIoI6 = 45;
static constexpr uint8_t kCfgBranchIoI7 = 46;
static constexpr uint8_t kCfgBranchIoI8 = 73;
static constexpr uint8_t kCfgBranchIoI9 = 74;
static constexpr uint8_t kCfgBranchIoI10 = 75;
static constexpr uint8_t kCfgBranchIoI11 = 76;
static constexpr uint8_t kCfgBranchIoI12 = 77;
static constexpr uint8_t kCfgBranchIoI13 = 78;
static constexpr uint8_t kCfgBranchIoI14 = 79;
static constexpr uint8_t kCfgBranchIoI15 = 80;
static constexpr uint8_t kCfgBranchIoExp0 = 81;
static constexpr uint8_t kCfgBranchIoExp1 = 82;
static constexpr uint8_t kCfgBranchIoExp2 = 83;
static constexpr uint8_t kCfgBranchIoExp3 = 84;
static constexpr uint8_t kCfgBranchIoBus = 22;
static constexpr uint8_t kCfgBranchIoDs18b20 = 23;
static constexpr uint8_t kCfgBranchIoGpio = 24;
static constexpr uint8_t kCfgBranchIoAds1115 = 25;
static constexpr uint8_t kCfgBranchIoAdsInt = 26;
static constexpr uint8_t kCfgBranchIoAdsExt = 27;
static constexpr uint8_t kCfgBranchIoSht40 = 29;
static constexpr uint8_t kCfgBranchIoBmp280 = 30;
static constexpr uint8_t kCfgBranchIoBme680 = 31;
static constexpr uint8_t kCfgBranchIoIna226 = 32;
static constexpr PhysicalPortId kLegacyDisconnectedBindingPort = 65535U;
static constexpr char kLegacyCounterRuntimeKeyFmt[] = "ioi%02urt";

static constexpr uint8_t analogCfgBranch_(uint8_t idx)
{
    return (idx == 0U) ? kCfgBranchIoA0 :
           (idx == 1U) ? kCfgBranchIoA1 :
           (idx == 2U) ? kCfgBranchIoA2 :
           (idx == 3U) ? kCfgBranchIoA3 :
           (idx == 4U) ? kCfgBranchIoA4 :
           (idx == 5U) ? kCfgBranchIoA5 :
           (idx == 6U) ? kCfgBranchIoA6 :
           (idx == 7U) ? kCfgBranchIoA7 :
           (idx == 8U) ? kCfgBranchIoA8 :
           (idx == 9U) ? kCfgBranchIoA9 :
           (idx == 10U) ? kCfgBranchIoA10 :
           (idx == 11U) ? kCfgBranchIoA11 :
           (idx == 12U) ? kCfgBranchIoA12 :
           (idx == 13U) ? kCfgBranchIoA13 :
           (idx == 14U) ? kCfgBranchIoA14 :
           (idx == 15U) ? kCfgBranchIoA15 :
           (idx == 16U) ? kCfgBranchIoA16 :
           (idx == 17U) ? kCfgBranchIoA17 :
           (idx == 18U) ? kCfgBranchIoA18 :
           (idx == 19U) ? kCfgBranchIoA19 :
           (idx == 20U) ? kCfgBranchIoA20 :
           (idx == 21U) ? kCfgBranchIoA21 :
           (idx == 22U) ? kCfgBranchIoA22 :
           (idx == 23U) ? kCfgBranchIoA23 :
           (idx == 24U) ? kCfgBranchIoA24 :
           (idx == 25U) ? kCfgBranchIoA25 :
           (idx == 26U) ? kCfgBranchIoA26 :
           (idx == 27U) ? kCfgBranchIoA27 :
           (idx == 28U) ? kCfgBranchIoA28 :
           (idx == 29U) ? kCfgBranchIoA29 :
           (idx == 30U) ? kCfgBranchIoA30 :
           (idx == 31U) ? kCfgBranchIoA31 :
                           ConfigBranchRef::UnknownLocalBranch;
}

static constexpr uint8_t digitalInputCfgBranch_(uint8_t idx)
{
    return (idx == 0U) ? kCfgBranchIoI0 :
           (idx == 1U) ? kCfgBranchIoI1 :
           (idx == 2U) ? kCfgBranchIoI2 :
           (idx == 3U) ? kCfgBranchIoI3 :
           (idx == 4U) ? kCfgBranchIoI4 :
           (idx == 5U) ? kCfgBranchIoI5 :
           (idx == 6U) ? kCfgBranchIoI6 :
           (idx == 7U) ? kCfgBranchIoI7 :
           (idx == 8U) ? kCfgBranchIoI8 :
           (idx == 9U) ? kCfgBranchIoI9 :
           (idx == 10U) ? kCfgBranchIoI10 :
           (idx == 11U) ? kCfgBranchIoI11 :
           (idx == 12U) ? kCfgBranchIoI12 :
           (idx == 13U) ? kCfgBranchIoI13 :
           (idx == 14U) ? kCfgBranchIoI14 :
           (idx == 15U) ? kCfgBranchIoI15 :
                            ConfigBranchRef::UnknownLocalBranch;
}

static constexpr uint8_t digitalOutputCfgBranch_(uint8_t idx)
{
    return (idx == 0U) ? kCfgBranchIoD0 :
           (idx == 1U) ? kCfgBranchIoD1 :
           (idx == 2U) ? kCfgBranchIoD2 :
           (idx == 3U) ? kCfgBranchIoD3 :
           (idx == 4U) ? kCfgBranchIoD4 :
           (idx == 5U) ? kCfgBranchIoD5 :
           (idx == 6U) ? kCfgBranchIoD6 :
           (idx == 7U) ? kCfgBranchIoD7 :
           (idx == 8U) ? kCfgBranchIoD8 :
           (idx == 9U) ? kCfgBranchIoD9 :
           (idx == 10U) ? kCfgBranchIoD10 :
           (idx == 11U) ? kCfgBranchIoD11 :
           (idx == 12U) ? kCfgBranchIoD12 :
           (idx == 13U) ? kCfgBranchIoD13 :
           (idx == 14U) ? kCfgBranchIoD14 :
           (idx == 15U) ? kCfgBranchIoD15 :
                            ConfigBranchRef::UnknownLocalBranch;
}

PhysicalPortId normalizeConfiguredBindingPort(PhysicalPortId port)
{
    return (port == kLegacyDisconnectedBindingPort) ? IO_PORT_INVALID : port;
}

static void formatDs18Address_(const uint8_t addr[8], char* out, size_t outLen)
{
    if (!addr || !out || outLen == 0U) return;
    int pos = snprintf(out, outLen, "%02X", addr[0]);
    for (uint8_t i = 1; i < 8U && pos > 0 && (size_t)pos < outLen; ++i) {
        pos += snprintf(out + pos, outLen - (size_t)pos, ":%02X", addr[i]);
    }
}

#define FLOW_IO_ANALOG_ROUTE_ENTRY(ROUTE_ID, BRANCH_ID, SLOT_STR) \
    {ROUTE_ID, {(uint8_t)ConfigModuleId::Io, BRANCH_ID}, "io/input/a" SLOT_STR, "io/input/a" SLOT_STR, (uint8_t)MqttPublishPriority::Normal, nullptr}
#define FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY(ROUTE_ID, BRANCH_ID, SLOT_STR) \
    {ROUTE_ID, {(uint8_t)ConfigModuleId::Io, BRANCH_ID}, "io/input/i" SLOT_STR, "io/input/i" SLOT_STR, (uint8_t)MqttPublishPriority::Normal, nullptr}
#define FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(ROUTE_ID, BRANCH_ID, SLOT_STR) \
    {ROUTE_ID, {(uint8_t)ConfigModuleId::Io, BRANCH_ID}, "io/output/d" SLOT_STR, "io/output/d" SLOT_STR, (uint8_t)MqttPublishPriority::Normal, nullptr}
static constexpr MqttConfigRouteProducer::Route kIoCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Io, kCfgBranchIo}, "io", "io", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {2, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoDebug}, "io/debug", "io/debug", (uint8_t)MqttPublishPriority::Normal, nullptr},
    FLOW_IO_ANALOG_ROUTE_ENTRY(3, kCfgBranchIoA0, "00"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(4, kCfgBranchIoA1, "01"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(5, kCfgBranchIoA2, "02"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(6, kCfgBranchIoA3, "03"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(7, kCfgBranchIoA4, "04"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(8, kCfgBranchIoA5, "05"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(9, kCfgBranchIoD0, "00"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(10, kCfgBranchIoD1, "01"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(11, kCfgBranchIoD2, "02"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(12, kCfgBranchIoD3, "03"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(13, kCfgBranchIoD4, "04"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(14, kCfgBranchIoD5, "05"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(15, kCfgBranchIoD6, "06"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(16, kCfgBranchIoD7, "07"),
    {17, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI0}, "io/input/i00", "io/input/i00", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {18, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI1}, "io/input/i01", "io/input/i01", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {19, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI2}, "io/input/i02", "io/input/i02", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {20, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI3}, "io/input/i03", "io/input/i03", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {21, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI4}, "io/input/i04", "io/input/i04", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {44, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI5}, "io/input/i05", "io/input/i05", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {45, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI6}, "io/input/i06", "io/input/i06", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {46, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI7}, "io/input/i07", "io/input/i07", (uint8_t)MqttPublishPriority::Normal, nullptr},
    FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY(73, kCfgBranchIoI8, "08"),
    FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY(74, kCfgBranchIoI9, "09"),
    FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY(75, kCfgBranchIoI10, "10"),
    FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY(76, kCfgBranchIoI11, "11"),
    FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY(77, kCfgBranchIoI12, "12"),
    FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY(78, kCfgBranchIoI13, "13"),
    FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY(79, kCfgBranchIoI14, "14"),
    FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY(80, kCfgBranchIoI15, "15"),
    {22, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoBus}, "io/drivers/bus", "io/drivers/bus", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {23, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoDs18b20}, "io/drivers/ds18b20", "io/drivers/ds18b20", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {24, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoGpio}, "io/drivers/gpio", "io/drivers/gpio", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {25, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoAds1115}, "io/drivers/ads1115", "io/drivers/ads1115", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {26, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoAdsInt}, "io/drivers/ads1115_int", "io/drivers/ads1115_int", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {27, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoAdsExt}, "io/drivers/ads1115_ext", "io/drivers/ads1115_ext", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {29, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoSht40}, "io/drivers/sht40", "io/drivers/sht40", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {30, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoBmp280}, "io/drivers/bmp280", "io/drivers/bmp280", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {31, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoBme680}, "io/drivers/bme680", "io/drivers/bme680", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {32, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoIna226}, "io/drivers/ina226", "io/drivers/ina226", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {81, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoExp0}, "io/drivers/expander00", "io/drivers/expander00", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {82, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoExp1}, "io/drivers/expander01", "io/drivers/expander01", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {83, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoExp2}, "io/drivers/expander02", "io/drivers/expander02", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {84, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoExp3}, "io/drivers/expander03", "io/drivers/expander03", (uint8_t)MqttPublishPriority::Normal, nullptr},
    FLOW_IO_ANALOG_ROUTE_ENTRY(33, kCfgBranchIoA6, "06"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(34, kCfgBranchIoA7, "07"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(35, kCfgBranchIoA8, "08"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(36, kCfgBranchIoA9, "09"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(37, kCfgBranchIoA10, "10"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(38, kCfgBranchIoA11, "11"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(39, kCfgBranchIoA12, "12"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(40, kCfgBranchIoA13, "13"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(41, kCfgBranchIoA14, "14"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(42, kCfgBranchIoA15, "15"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(49, kCfgBranchIoD8, "08"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(50, kCfgBranchIoD9, "09"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(51, kCfgBranchIoD10, "10"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(52, kCfgBranchIoD11, "11"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(53, kCfgBranchIoD12, "12"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(54, kCfgBranchIoD13, "13"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(55, kCfgBranchIoD14, "14"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(56, kCfgBranchIoD15, "15"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(57, kCfgBranchIoA16, "16"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(58, kCfgBranchIoA17, "17"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(59, kCfgBranchIoA18, "18"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(60, kCfgBranchIoA19, "19"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(61, kCfgBranchIoA20, "20"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(62, kCfgBranchIoA21, "21"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(63, kCfgBranchIoA22, "22"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(64, kCfgBranchIoA23, "23"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(65, kCfgBranchIoA24, "24"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(66, kCfgBranchIoA25, "25"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(67, kCfgBranchIoA26, "26"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(68, kCfgBranchIoA27, "27"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(69, kCfgBranchIoA28, "28"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(70, kCfgBranchIoA29, "29"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(71, kCfgBranchIoA30, "30"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(72, kCfgBranchIoA31, "31"),
};
#undef FLOW_IO_ANALOG_ROUTE_ENTRY
#undef FLOW_IO_DIGITAL_INPUT_ROUTE_ENTRY
#undef FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY
static_assert((sizeof(kIoCfgRoutes) / sizeof(kIoCfgRoutes[0])) <= MqttConfigRouteProducer::MaxRoutes,
              "IOModule config routes exceed MqttConfigRouteProducer capacity");
}

static bool hasDecimalSuffixLocal(const char* p)
{
    if (!p || *p == '\0') return false;
    while (*p) {
        if (*p < '0' || *p > '9') return false;
        ++p;
    }
    return true;
}

static bool isInputEndpointIdLocal(const char* id)
{
    if (!id || id[0] == '\0') return false;
    if ((id[0] == 'a' || id[0] == 'i') && hasDecimalSuffixLocal(id + 1)) return true;
    return false;
}

static bool isOutputEndpointIdLocal(const char* id)
{
    if (!id || id[0] == '\0') return false;
    return id[0] == 'd' && hasDecimalSuffixLocal(id + 1);
}

static const char* ioEdgeModeLabelLocal(uint8_t edgeMode)
{
    switch (edgeMode) {
        case IO_EDGE_FALLING: return "falling";
        case IO_EDGE_BOTH: return "both";
        case IO_EDGE_RISING:
        default:
            return "rising";
    }
}

static int32_t counterDebounceConfigFromUsLocal(uint32_t value)
{
    if (value > (uint32_t)INT32_MAX) return INT32_MAX;
    return (int32_t)value;
}

static uint32_t counterDebounceUsFromConfigLocal(int32_t value)
{
    return (value <= 0) ? 0U : (uint32_t)value;
}

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

void* allocPsramBytes_(size_t bytes)
{
    void* mem = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    return mem;
}

void IOModule::setOneWireBuses(OneWireBus* water, OneWireBus* air)
{
    oneWireWater_ = water;
    oneWireAir_ = air;
}

void IOModule::setBindingPorts(const IOBindingPortSpec* ports, uint8_t count)
{
    bindingPorts_ = ports;
    bindingPortCount_ = count;
}

void IOModule::setExpanders(const IOExpanderSpec* expanders, uint8_t count)
{
    expanders_ = expanders;
    expanderCount_ = count;
    for (uint8_t i = 0; i < IO_MAX_EXPANDERS; ++i) {
        expanderCfg_[i] = IOExpanderConfig{};
        runtimeExpanders_[i] = RuntimeExpander{};
    }
    if (!expanders_) return;
    for (uint8_t i = 0; i < count; ++i) {
        const IOExpanderSpec& spec = expanders[i];
        if (spec.expanderId >= IO_MAX_EXPANDERS) continue;
        expanderCfg_[spec.expanderId].enabled = spec.enabled;
        expanderCfg_[spec.expanderId].address = spec.address;
        expanderCfg_[spec.expanderId].maskDefault = spec.maskDefault;
        runtimeExpanders_[spec.expanderId].spec = &spec;
    }
}

bool IOModule::ensureConfigDescriptorStorage_()
{
    if (configDescriptors_) return true;

    const size_t internalBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void* mem = heap_caps_malloc(
        sizeof(IOConfigDescriptorStorage),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    bool psramBacked = (mem != nullptr);
#if !defined(BOARD_HAS_PSRAM)
    // Profiles without PSRAM retain a compatibility fallback. PSRAM-equipped
    // targets deliberately fail instead of silently consuming internal RAM.
    if (!mem) {
        mem = heap_caps_malloc(sizeof(IOConfigDescriptorStorage), MALLOC_CAP_8BIT);
    }
#endif
    if (!mem) {
        LOGE("I/O config descriptor allocation failed bytes=%u memory=psram",
             (unsigned)sizeof(IOConfigDescriptorStorage));
        return false;
    }

    configDescriptors_ = new (mem) IOConfigDescriptorStorage(analogCfg_, digitalInCfg_, digitalCfg_);
    const size_t internalAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psramAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    LOGI("I/O config descriptors ready slots=%u/%u/%u bytes=%u memory=%s heap_delta internal=%ld psram=%ld",
         (unsigned)ANALOG_CFG_SLOTS,
         (unsigned)DIGITAL_INPUT_CFG_SLOTS,
         (unsigned)DIGITAL_CFG_SLOTS,
         (unsigned)sizeof(IOConfigDescriptorStorage),
         psramBacked ? "psram" : "internal",
         (long)internalAfter - (long)internalBefore,
         (long)psramAfter - (long)psramBefore
    );
    return true;
}

bool IOModule::ensureScalableStorage_()
{
    if (analogSlots_ && digitalSlots_ && digitalSensorEndpointPool_ &&
        digitalActuatorEndpointPool_ && gpioDriverPool_) {
        return true;
    }

    if (!analogSlots_) analogSlots_ = allocPsramArray_<AnalogSlot>(MAX_ANALOG_ENDPOINTS);
    if (!digitalSlots_) digitalSlots_ = allocPsramArray_<DigitalSlot>(MAX_DIGITAL_SLOTS);
    if (!digitalSensorEndpointPool_) {
        digitalSensorEndpointPool_ = static_cast<uint8_t (*)[sizeof(DigitalSensorEndpoint)]>(
            allocPsramBytes_(MAX_DIGITAL_INPUTS * sizeof(DigitalSensorEndpoint))
        );
    }
    if (!digitalActuatorEndpointPool_) {
        digitalActuatorEndpointPool_ = static_cast<uint8_t (*)[sizeof(DigitalActuatorEndpoint)]>(
            allocPsramBytes_(MAX_DIGITAL_OUTPUTS * sizeof(DigitalActuatorEndpoint))
        );
    }
    if (!gpioDriverPool_) {
        gpioDriverPool_ = static_cast<uint8_t (*)[sizeof(GpioDriver)]>(
            allocPsramBytes_(MAX_DIGITAL_SLOTS * sizeof(GpioDriver))
        );
    }

    const bool ok = analogSlots_ && digitalSlots_ && digitalSensorEndpointPool_ &&
                    digitalActuatorEndpointPool_ && gpioDriverPool_;
    if (ok) {
        LOGI("I/O scalable storage ready analog=%u digital_slots=%u digital_in=%u digital_out=%u",
             (unsigned)MAX_ANALOG_ENDPOINTS,
             (unsigned)MAX_DIGITAL_SLOTS,
             (unsigned)MAX_DIGITAL_INPUTS,
             (unsigned)MAX_DIGITAL_OUTPUTS);
    } else {
        LOGE("I/O scalable storage allocation failed");
    }
    return ok;
}

bool IOModule::ensureDigitalCounterConfigState_()
{
    if (digitalCounterLastConfigTotals_) return true;
    digitalCounterLastConfigTotals_ = static_cast<float*>(
        heap_caps_calloc(MAX_DIGITAL_INPUTS, sizeof(float), MALLOC_CAP_8BIT)
    );
    return digitalCounterLastConfigTotals_ != nullptr;
}

bool IOModule::ensureLastCycleState_()
{
    if (lastCycle_) return true;
    lastCycle_ = static_cast<IoCycleInfo*>(
        heap_caps_calloc(1, sizeof(IoCycleInfo), MALLOC_CAP_8BIT)
    );
    return lastCycle_ != nullptr;
}

bool IOModule::ensureAnalogPrecisionState_()
{
    if (analogPrecisionLast_) return true;
    analogPrecisionLast_ = static_cast<int32_t*>(
        heap_caps_calloc(ANALOG_CFG_SLOTS, sizeof(int32_t), MALLOC_CAP_8BIT)
    );
    return analogPrecisionLast_ != nullptr;
}

bool IOModule::defineAnalogInput(const IOAnalogDefinition& def)
{
    if (!ensureScalableStorage_()) return false;
    if (def.id[0] == '\0') return false;
    if (def.ioId == IO_ID_INVALID) return false;
    if (def.ioId < IO_ID_AI_BASE || def.ioId >= IO_ID_AI_MAX) return false;

    const uint8_t analogIdx = (uint8_t)(def.ioId - IO_ID_AI_BASE);
    if (analogSlots_[analogIdx].used) return false;

    AnalogSlot& slot = analogSlots_[analogIdx];
    slot.used = true;
    slot.ioId = def.ioId;
    slot.def = def;
    slot.def.ioId = slot.ioId;

    if (analogIdx < ANALOG_CFG_SLOTS) {
        strncpy(analogCfg_[analogIdx].name, def.id, sizeof(analogCfg_[analogIdx].name) - 1);
        analogCfg_[analogIdx].name[sizeof(analogCfg_[analogIdx].name) - 1] = '\0';
        analogCfg_[analogIdx].bindingPort = def.bindingPort;
        analogCfg_[analogIdx].c0 = def.c0;
        analogCfg_[analogIdx].c1 = def.c1;
        analogCfg_[analogIdx].precision = def.precision;
    }

    return true;
}

bool IOModule::applyAnalogInputDefaults(const IOAnalogDefinition& def)
{
    if (!ensureScalableStorage_()) return false;
    if (def.id[0] == '\0') return false;
    if (def.ioId == IO_ID_INVALID) return false;
    if (def.ioId < IO_ID_AI_BASE || def.ioId >= IO_ID_AI_MAX) return false;

    const uint8_t analogIdx = (uint8_t)(def.ioId - IO_ID_AI_BASE);
    AnalogSlot& slot = analogSlots_[analogIdx];
    if (!slot.used) return false;

    slot.def = def;
    slot.def.ioId = slot.ioId;

    if (analogIdx < ANALOG_CFG_SLOTS) {
        strncpy(analogCfg_[analogIdx].name, def.id, sizeof(analogCfg_[analogIdx].name) - 1);
        analogCfg_[analogIdx].name[sizeof(analogCfg_[analogIdx].name) - 1] = '\0';
        analogCfg_[analogIdx].bindingPort = def.bindingPort;
        analogCfg_[analogIdx].c0 = def.c0;
        analogCfg_[analogIdx].c1 = def.c1;
        analogCfg_[analogIdx].precision = def.precision;
    }

    return true;
}

bool IOModule::digitalLogicalUsed_(uint8_t kind, uint8_t logicalIdx) const
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;
        if (s.kind != kind) continue;
        if (s.logicalIdx != logicalIdx) continue;
        return true;
    }
    return false;
}

bool IOModule::findDigitalSlotByLogical_(uint8_t kind, uint8_t logicalIdx, uint8_t& slotIdxOut) const
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;
        if (s.kind != kind) continue;
        if (s.logicalIdx != logicalIdx) continue;
        slotIdxOut = i;
        return true;
    }
    return false;
}

bool IOModule::findDigitalSlotByIoId_(IoId id, uint8_t& slotIdxOut) const
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;
        if (s.ioId != id) continue;
        slotIdxOut = i;
        return true;
    }
    return false;
}

ConfigVariable<float,0>* IOModule::counterTotalVar_(uint8_t logicalIdx)
{
    if (logicalIdx >= DIGITAL_INPUT_CFG_SLOTS) return nullptr;
    if (!configDescriptors_ && !ensureConfigDescriptorStorage_()) return nullptr;
    return &configDescriptors_->digitalInputs[logicalIdx].counterTotalVar;
}

float* IOModule::counterConfigTotalState_(uint8_t logicalIdx)
{
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return nullptr;
    if (!digitalCounterLastConfigTotals_ && !ensureDigitalCounterConfigState_()) return nullptr;
    return &digitalCounterLastConfigTotals_[logicalIdx];
}

void IOModule::eraseLegacyCounterPersistedTotal_(uint8_t logicalIdx)
{
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return;

    char key[16];
    snprintf(key, sizeof(key), kLegacyCounterRuntimeKeyFmt, (unsigned)logicalIdx);
    if (cfgSvc_ && cfgSvc_->eraseKeyAsync) {
        (void)cfgSvc_->eraseKeyAsync(cfgSvc_->ctx, key);
    }
}

void IOModule::beginIoCycle_(uint32_t nowMs)
{
    if (!ensureLastCycleState_()) return;
    ++lastCycle_->seq;
    lastCycle_->tsMs = nowMs;
    lastCycle_->changedCount = 0;
}

void IOModule::markIoCycleChanged_(IoId id)
{
    if (id == IO_ID_INVALID) return;
    if (!ensureLastCycleState_()) return;

    for (uint8_t i = 0; i < lastCycle_->changedCount; ++i) {
        if (lastCycle_->changedIds[i] == id) return;
    }

    if (lastCycle_->changedCount >= IO_MAX_CHANGED_IDS) return;
    lastCycle_->changedIds[lastCycle_->changedCount++] = id;
}

bool IOModule::defineDigitalInput(const IODigitalInputDefinition& def)
{
    if (!ensureScalableStorage_()) return false;
    if (def.id[0] == '\0') return false;
    if (def.ioId == IO_ID_INVALID) return false;
    if (def.ioId < IO_ID_DI_BASE || def.ioId >= IO_ID_DI_MAX) return false;

    const uint8_t logicalIdx = (uint8_t)(def.ioId - IO_ID_DI_BASE);
    if (digitalLogicalUsed_(DIGITAL_SLOT_INPUT, logicalIdx)) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& s = digitalSlots_[i];
        if (s.used) continue;
        s.used = true;
        s.ioId = def.ioId;
        s.kind = DIGITAL_SLOT_INPUT;
        s.logicalIdx = logicalIdx;
        s.inDef = def;
        s.inDef.ioId = s.ioId;
        s.owner = this;
        if (logicalIdx < MAX_DIGITAL_INPUTS) {
            strncpy(digitalInCfg_[logicalIdx].name, def.id, sizeof(digitalInCfg_[logicalIdx].name) - 1);
            digitalInCfg_[logicalIdx].name[sizeof(digitalInCfg_[logicalIdx].name) - 1] = '\0';
            digitalInCfg_[logicalIdx].bindingPort = def.bindingPort;
            digitalInCfg_[logicalIdx].activeHigh = def.activeHigh;
            digitalInCfg_[logicalIdx].pullMode = def.pullMode;
            digitalInCfg_[logicalIdx].mode = def.mode;
            digitalInCfg_[logicalIdx].edgeMode = def.edgeMode;
            digitalInCfg_[logicalIdx].counterDebounceUs = counterDebounceConfigFromUsLocal(def.counterDebounceUs);
        }
        return true;
    }

    return false;
}

bool IOModule::defineDigitalOutput(const IODigitalOutputDefinition& def)
{
    if (!ensureScalableStorage_()) return false;
    if (def.id[0] == '\0') return false;
    if (def.ioId == IO_ID_INVALID) return false;
    if (def.ioId < IO_ID_DO_BASE || def.ioId >= IO_ID_DO_MAX) return false;

    const uint8_t logicalIdx = (uint8_t)(def.ioId - IO_ID_DO_BASE);
    if (digitalLogicalUsed_(DIGITAL_SLOT_OUTPUT, logicalIdx)) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& s = digitalSlots_[i];
        if (s.used) continue;
        s.used = true;
        s.ioId = def.ioId;
        s.kind = DIGITAL_SLOT_OUTPUT;
        s.logicalIdx = logicalIdx;
        s.outDef = def;
        s.outDef.ioId = s.ioId;
        s.owner = this;

        if (logicalIdx < DIGITAL_CFG_SLOTS) {
            const uint8_t cfgIdx = logicalIdx;
            strncpy(digitalCfg_[cfgIdx].name, def.id, sizeof(digitalCfg_[cfgIdx].name) - 1);
            digitalCfg_[cfgIdx].name[sizeof(digitalCfg_[cfgIdx].name) - 1] = '\0';
            digitalCfg_[cfgIdx].bindingPort = def.bindingPort;
            digitalCfg_[cfgIdx].activeHigh = def.activeHigh;
            digitalCfg_[cfgIdx].initialOn = def.initialOn;
            digitalCfg_[cfgIdx].startupPolicy = def.startupPolicy;
            digitalCfg_[cfgIdx].retainOnWarmReboot = def.retainOnWarmReboot;
            digitalCfg_[cfgIdx].momentary = def.momentary;
            digitalCfg_[cfgIdx].pulseMs = (int32_t)def.pulseMs;
        }
        return true;
    }

    return false;
}

const char* IOModule::analogSlotName(uint8_t idx) const
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return nullptr;
    if (!analogSlots_[idx].used) return nullptr;
    if (analogSlots_[idx].def.id[0] == '\0') return nullptr;
    return analogSlots_[idx].def.id;
}

bool IOModule::analogSlotUsed(uint8_t idx) const
{
    return idx < MAX_ANALOG_ENDPOINTS && analogSlots_[idx].used;
}

bool IOModule::analogSlotPublished(uint8_t idx) const
{
    return analogSlotPublished_(idx);
}

bool IOModule::digitalInputSlotUsed(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    return logicalIdx < MAX_DIGITAL_INPUTS && findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logicalIdx, slotIdx);
}

bool IOModule::digitalInputSlotPublished(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return false;
    if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logicalIdx, slotIdx)) return false;

    const DigitalSlot& s = digitalSlots_[slotIdx];
    return s.used && s.kind == DIGITAL_SLOT_INPUT && s.endpoint;
}

uint8_t IOModule::digitalInputValueType(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return IO_VAL_BOOL;
    if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logicalIdx, slotIdx)) return IO_VAL_BOOL;
    const DigitalSlot& s = digitalSlots_[slotIdx];
    return (s.inDef.mode == IO_DIGITAL_INPUT_COUNTER) ? IO_VAL_FLOAT : IO_VAL_BOOL;
}

int32_t IOModule::digitalInputPrecision(uint8_t logicalIdx) const
{
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return 0;
    return sanitizeAnalogPrecision_(digitalInCfg_[logicalIdx].precision);
}

bool IOModule::digitalOutputSlotUsed(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    return logicalIdx < MAX_DIGITAL_OUTPUTS && findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logicalIdx, slotIdx);
}

bool IOModule::digitalOutputSlotWritable(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    if (logicalIdx >= MAX_DIGITAL_OUTPUTS) return false;
    if (!findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logicalIdx, slotIdx)) return false;

    const DigitalSlot& s = digitalSlots_[slotIdx];
    return s.used && s.kind == DIGITAL_SLOT_OUTPUT && s.provider.isBound();
}

int32_t IOModule::analogPrecision(uint8_t idx) const
{
    if (idx >= ANALOG_CFG_SLOTS) return 0;
    return sanitizeAnalogPrecision_(analogCfg_[idx].precision);
}

uint32_t IOModule::takeAnalogConfigDirtyMask()
{
    const uint32_t mask = analogConfigDirtyMask_;
    analogConfigDirtyMask_ = 0;
    return mask;
}

const char* IOModule::endpointLabel(const char* endpointId) const
{
    if (!endpointId || endpointId[0] == '\0') return nullptr;
    if (endpointId[0] == 'a' && hasDecimalSuffixLocal(endpointId + 1)) {
        uint8_t idx = (uint8_t)atoi(endpointId + 1);
        if (idx < ANALOG_CFG_SLOTS && analogCfg_[idx].name[0] != '\0') return analogCfg_[idx].name;
    }
    if (endpointId[0] == 'i' && hasDecimalSuffixLocal(endpointId + 1)) {
        uint8_t idx = (uint8_t)atoi(endpointId + 1);
        uint8_t slotIdx = 0xFF;
        if (findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, idx, slotIdx)) {
            const DigitalSlot& s = digitalSlots_[slotIdx];
            if (idx < MAX_DIGITAL_INPUTS && digitalInCfg_[idx].name[0] != '\0') return digitalInCfg_[idx].name;
            if (s.inDef.id[0] != '\0') return s.inDef.id;
        }
    }
    if (endpointId[0] == 'd' && hasDecimalSuffixLocal(endpointId + 1)) {
        uint8_t idx = (uint8_t)atoi(endpointId + 1);
        if (idx < DIGITAL_CFG_SLOTS && digitalCfg_[idx].name[0] != '\0') return digitalCfg_[idx].name;
    }
    return nullptr;
}

bool IOModule::buildInputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const
{
    return buildGroupSnapshot_(out, len, true, maxTsOut);
}

bool IOModule::buildOutputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const
{
    return buildGroupSnapshot_(out, len, false, maxTsOut);
}

bool IOModule::writeAnalogProviderRuntimeValue_(RuntimeUiId runtimeId,
                                                uint8_t source,
                                                uint8_t channel,
                                                IRuntimeUiWriter& writer) const
{
    const IOAnalogProvider* provider = analogProviderForSource_(source);
    if (!provider || !provider->isBound()) {
        return writer.writeUnavailable(runtimeId);
    }

    IOAnalogSample sample{};
    if (!provider->readSample(channel, sample)) {
        return writer.writeUnavailable(runtimeId);
    }
    return writer.writeF32(runtimeId, sample.value);
}

bool IOModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);
    uint8_t runtimeIndex = 0xFF;

    switch (valueId) {
        case RuntimeUiWaterCounter: {
            IoValue value{};
            const IoStatus st = ioReadValue_(
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                ioIdFromSlot(digitalInputSlot(12)),
#else
                ioIdFromSlot(digitalInputSlot(3)),
#endif
                &value
            );
            if (st != IO_OK || !value.valid) {
                return writer.writeUnavailable(runtimeId);
            }
            if (value.type == IO_VAL_FLOAT) return writer.writeF32(runtimeId, value.v.f);
            if (value.type == IO_VAL_INT32) return writer.writeI32(runtimeId, value.v.i32);
            return writer.writeUnavailable(runtimeId);
        }
        case RuntimeUiPsi:
            runtimeIndex = 2;
            break;
        case RuntimeUiBmp280Temp:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BMP280, 0U, writer);
        case RuntimeUiBme680Temp:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BME680, 0U, writer);
        case RuntimeUiBmp280Pressure:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BMP280, 1U, writer);
        case RuntimeUiSht40Temperature:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_SHT40, 0U, writer);
        case RuntimeUiSht40Humidity:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_SHT40, 1U, writer);
        case RuntimeUiBme680Humidity:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BME680, 1U, writer);
        case RuntimeUiBme680Pressure:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BME680, 2U, writer);
        case RuntimeUiBme680Gaz:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BME680, 3U, writer);
        case RuntimeUiWaterTemp:
            runtimeIndex = 4;
            break;
        case RuntimeUiAirTemp:
            runtimeIndex = 5;
            break;
        case RuntimeUiPh:
            runtimeIndex = 1;
            break;
        case RuntimeUiOrp:
            runtimeIndex = 0;
            break;
        default:
            return false;
    }

    if (!dataStore_) return writer.writeUnavailable(runtimeId);

    float value = 0.0f;
    if (!ioEndpointFloat(*dataStore_, runtimeIndex, value)) {
        return writer.writeUnavailable(runtimeId);
    }
    return writer.writeF32(runtimeId, value);
}

uint8_t IOModule::runtimeSnapshotCount() const
{
    if (!cfgData_.enabled) return 0;

    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (analogRuntimeRoutePublished_(i)) ++count;
    }
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        if (digitalRuntimeRoutePublished_(i)) ++count;
    }
    return count;
}

bool IOModule::runtimeSnapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& routeTypeOut, uint8_t& slotIdxOut) const
{
    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;
    static constexpr uint8_t ROUTE_DIGITAL_OUTPUT = 2;

    if (!cfgData_.enabled) return false;

    uint8_t seen = 0;
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogRuntimeRoutePublished_(i)) continue;
        if (seen == snapshotIdx) {
            routeTypeOut = ROUTE_ANALOG;
            slotIdxOut = i;
            return true;
        }
        ++seen;
    }
    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logical, slotIdx)) continue;
        if (!digitalRuntimeRoutePublished_(slotIdx)) continue;
        if (seen == snapshotIdx) {
            routeTypeOut = ROUTE_DIGITAL_INPUT;
            slotIdxOut = slotIdx;
            return true;
        }
        ++seen;
    }
    for (uint8_t logical = 0; logical < MAX_DIGITAL_OUTPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logical, slotIdx)) continue;
        if (!digitalRuntimeRoutePublished_(slotIdx)) continue;
        if (seen == snapshotIdx) {
            routeTypeOut = ROUTE_DIGITAL_OUTPUT;
            slotIdxOut = slotIdx;
            return true;
        }
        ++seen;
    }
    return false;
}

bool IOModule::buildEndpointSnapshot_(IOEndpoint* ep, char* out, size_t len, uint32_t& maxTsOut, bool invalidAsUndefined) const
{
    if (!ep || !out || len == 0) return false;
    if ((ep->capabilities() & IO_CAP_READ) == 0) return false;

    IOEndpointValue v{};
    bool ok = ep->read(v);
    if (!ok) v.valid = false;

    const char* id = ep->id();
    const char* label = endpointLabel(id);
    int wrote = snprintf(out, len, "{\"id\":\"%s\",\"name\":\"%s\",\"available\":%s,\"value\":",
                         (id && id[0] != '\0') ? id : "",
                         (label && label[0] != '\0') ? label : ((id && id[0] != '\0') ? id : ""),
                         v.valid ? "true" : "false");
    if (wrote < 0 || (size_t)wrote >= len) return false;
    size_t used = (size_t)wrote;

    if (!v.valid) {
        (void)invalidAsUndefined;
        wrote = snprintf(out + used, len - used, "null");
    } else if (v.valueType == IO_EP_VALUE_BOOL) {
        wrote = snprintf(out + used, len - used, "%s", v.v.b ? "true" : "false");
    } else if (v.valueType == IO_EP_VALUE_FLOAT) {
        wrote = snprintf(out + used, len - used, "%.3f", (double)v.v.f);
    } else if (v.valueType == IO_EP_VALUE_INT32) {
        wrote = snprintf(out + used, len - used, "%ld", (long)v.v.i);
    } else {
        wrote = snprintf(out + used, len - used, "null");
    }
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
    used += (size_t)wrote;

    wrote = snprintf(out + used, len - used, ",\"ts\":%lu}", (unsigned long)millis());
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;

    // Ensure one initial publish even if endpoint timestamp has not been set yet.
    maxTsOut = (v.timestampMs == 0U) ? 1U : v.timestampMs;
    return true;
}

const char* IOModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) return nullptr;

    static char suffix[24];
    if (routeType == ROUTE_ANALOG) {
        snprintf(suffix, sizeof(suffix), "rt/io/input/a%02u", (unsigned)slotIdx);
    } else {
        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (routeType == ROUTE_DIGITAL_INPUT) {
            snprintf(suffix, sizeof(suffix), "rt/io/input/i%02u", (unsigned)s.logicalIdx);
        } else {
            snprintf(suffix, sizeof(suffix), "rt/io/output/d%02u", (unsigned)s.logicalIdx);
        }
    }
    return suffix;
}

RuntimeRouteClass IOModule::runtimeSnapshotClass(uint8_t idx) const
{
    static constexpr uint8_t ROUTE_DIGITAL_OUTPUT = 2;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) {
        return RuntimeRouteClass::NumericThrottled;
    }
    (void)slotIdx;
    return (routeType == ROUTE_DIGITAL_OUTPUT)
        ? RuntimeRouteClass::ActuatorImmediate
        : RuntimeRouteClass::NumericThrottled;
}

bool IOModule::runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const
{
    if (key < DATAKEY_IO_BASE || key >= (DataKey)(DATAKEY_IO_BASE + IO_MAX_ENDPOINTS)) return false;

    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;
    static constexpr uint8_t ROUTE_DIGITAL_OUTPUT = 2;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) return false;

    IOEndpoint* ep = nullptr;
    if (routeType == ROUTE_ANALOG) {
        ep = static_cast<IOEndpoint*>(analogSlots_[slotIdx].endpoint);
    } else if (routeType == ROUTE_DIGITAL_INPUT || routeType == ROUTE_DIGITAL_OUTPUT) {
        ep = digitalSlots_[slotIdx].endpoint;
    } else {
        return false;
    }
    if (!ep || !ep->id()) return false;

    uint8_t endpointIdx = 0;
    if (!endpointIndexFromId_(ep->id(), endpointIdx)) return false;
    return key == (DataKey)(DATAKEY_IO_BASE + endpointIdx);
}

bool IOModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) return false;

    IOEndpoint* ep = nullptr;
    if (routeType == ROUTE_ANALOG) {
        ep = static_cast<IOEndpoint*>(analogSlots_[slotIdx].endpoint);
        return buildEndpointSnapshot_(ep, out, len, maxTsOut, analogSlotUsesUndefinedInvalidValue_(slotIdx));
    }
    ep = digitalSlots_[slotIdx].endpoint;
    return buildEndpointSnapshot_(ep, out, len, maxTsOut);
}

bool IOModule::buildGroupSnapshot_(char* out, size_t len, bool inputGroup, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;

    size_t used = 0;
    int wrote = snprintf(out, len, "{");
    if (wrote < 0 || (size_t)wrote >= len) return false;
    used += (size_t)wrote;

    bool first = true;
    uint32_t maxTs = 0;
    for (uint8_t i = 0; i < registry_.count(); ++i) {
        IOEndpoint* ep = registry_.at(i);
        if (!ep) continue;
        if ((ep->capabilities() & IO_CAP_READ) == 0) continue;

        const char* id = ep->id();
        if (!id || id[0] == '\0') continue;
        if (inputGroup && !isInputEndpointIdLocal(id)) continue;
        if (!inputGroup && !isOutputEndpointIdLocal(id)) continue;
        if (inputGroup && id[0] == 'a' && hasDecimalSuffixLocal(id + 1)) {
            const uint8_t analogIdx = (uint8_t)atoi(id + 1);
            if (!analogSlotPublished_(analogIdx)) continue;
        }

        IOEndpointValue v{};
        bool ok = ep->read(v);
        if (!ok) v.valid = false;

        const char* label = endpointLabel(id);
        wrote = snprintf(out + used, len - used, "%s\"%s\":{\"name\":\"%s\",\"available\":%s,\"value\":",
                         first ? "" : ",",
                         id,
                         (label && label[0] != '\0') ? label : id,
                         v.valid ? "true" : "false");
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;
        first = false;

        if (!v.valid) {
            wrote = snprintf(out + used, len - used, "null");
        } else if (v.valueType == IO_EP_VALUE_BOOL) {
            wrote = snprintf(out + used, len - used, "%s", v.v.b ? "true" : "false");
        } else if (v.valueType == IO_EP_VALUE_FLOAT) {
            wrote = snprintf(out + used, len - used, "%.3f", (double)v.v.f);
        } else if (v.valueType == IO_EP_VALUE_INT32) {
            wrote = snprintf(out + used, len - used, "%ld", (long)v.v.i);
        } else {
            wrote = snprintf(out + used, len - used, "null");
        }
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;

        wrote = snprintf(out + used, len - used, "}");
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;

        if (v.timestampMs > maxTs) maxTs = v.timestampMs;
    }

    wrote = snprintf(out + used, len - used, ",\"ts\":%lu}", (unsigned long)millis());
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;

    maxTsOut = maxTs;
    return true;
}

bool IOModule::tickFastAds_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    self->analogProviders_[IO_SRC_ADS_INTERNAL_SINGLE].tick(nowMs);
    self->analogProviders_[IO_SRC_ADS_EXTERNAL_DIFF].tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        uint8_t src = self->analogSlots_[i].source;
        if (src == IO_SRC_ADS_INTERNAL_SINGLE || src == IO_SRC_ADS_EXTERNAL_DIFF) {
            self->processAnalogDefinition_(i, nowMs);
        }
    }
    return true;
}

bool IOModule::tickSlowDs_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    self->analogProviders_[IO_SRC_DS18_WATER].tick(nowMs);
    self->analogProviders_[IO_SRC_DS18_AIR].tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        uint8_t src = self->analogSlots_[i].source;
        if (src == IO_SRC_DS18_WATER || src == IO_SRC_DS18_AIR) {
            self->processAnalogDefinition_(i, nowMs);
        }
    }
    return true;
}

bool IOModule::tickI2cAnalogs_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    self->analogProviders_[IO_SRC_SHT40].tick(nowMs);
    self->analogProviders_[IO_SRC_BMP280].tick(nowMs);
    self->analogProviders_[IO_SRC_BME680].tick(nowMs);
    self->analogProviders_[IO_SRC_INA226].tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        const uint8_t src = self->analogSlots_[i].source;
        if (src == IO_SRC_SHT40 || src == IO_SRC_BMP280 || src == IO_SRC_BME680 || src == IO_SRC_INA226) {
            self->processAnalogDefinition_(i, nowMs);
        }
    }
    return true;
}

bool IOModule::tickDigitalInputs_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        if (!self->digitalSlots_[i].used) continue;
        if (self->digitalSlots_[i].kind != DIGITAL_SLOT_INPUT) continue;
        (void)self->processDigitalInputDefinition_(i, nowMs);
    }
    self->pollPulseOutputs_(nowMs);
    return true;
}

const IOAnalogProvider* IOModule::analogProviderForSource_(uint8_t source) const
{
    // Kernel-side routing stays on compact source ids; runtime setup binds one provider per physical device.
    return (source < IO_SRC_COUNT) ? &analogProviders_[source] : nullptr;
}

bool IOModule::resolveConfiguredAnalogSource_(uint8_t idx, uint8_t& sourceOut) const
{
    if (idx >= ANALOG_CFG_SLOTS) return false;
    if (!analogSlots_[idx].used) return false;

    uint8_t channel = 0U;
    uint8_t backend = IO_BACKEND_GPIO;
    uint8_t source = IO_ANALOG_SOURCE_INVALID;
    if (!resolveAnalogBinding_(analogCfg_[idx].bindingPort, source, channel, backend)) return false;

    sourceOut = source;
    return true;
}

bool IOModule::analogSourceRequiresDriverEnable_(uint8_t source) const
{
    return source == IO_SRC_SHT40 ||
           source == IO_SRC_BMP280 ||
           source == IO_SRC_BME680 ||
           source == IO_SRC_INA226;
}

bool IOModule::analogSourceDriverEnabled_(uint8_t source) const
{
    switch (source) {
        case IO_SRC_SHT40:
            return cfgData_.sht40Enabled;
        case IO_SRC_BMP280:
            return cfgData_.bmp280Enabled;
        case IO_SRC_BME680:
            return cfgData_.bme680Enabled;
        case IO_SRC_INA226:
            return cfgData_.ina226Enabled;
        default:
            return true;
    }
}

bool IOModule::analogSlotPublished_(uint8_t idx) const
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return false;
    return cfgData_.enabled && analogSlots_[idx].used && analogSlots_[idx].endpoint;
}

bool IOModule::analogRuntimeRoutePublished_(uint8_t idx) const
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return false;
    if (!cfgData_.enabled || !analogSlots_[idx].used) return false;

    uint8_t source = IO_ANALOG_SOURCE_INVALID;
    if (!resolveConfiguredAnalogSource_(idx, source)) return false;

    if (!analogSourceRequiresDriverEnable_(source)) return true;
    return analogSourceDriverEnabled_(source);
}

bool IOModule::digitalRuntimeRoutePublished_(uint8_t slotIdx) const
{
    if (slotIdx >= MAX_DIGITAL_SLOTS) return false;
    const DigitalSlot& slot = digitalSlots_[slotIdx];
    if (!cfgData_.enabled || !slot.used) return false;

    if (slot.kind == DIGITAL_SLOT_INPUT) {
        if (slot.logicalIdx >= MAX_DIGITAL_INPUTS) return false;
        return digitalInCfg_[slot.logicalIdx].bindingPort != IO_PORT_INVALID;
    }
    if (slot.kind == DIGITAL_SLOT_OUTPUT) {
        if (slot.logicalIdx >= DIGITAL_CFG_SLOTS) return false;
        return digitalCfg_[slot.logicalIdx].bindingPort != IO_PORT_INVALID;
    }
    return false;
}

bool IOModule::analogSlotUsesUndefinedInvalidValue_(uint8_t idx) const
{
    uint8_t source = IO_ANALOG_SOURCE_INVALID;
    if (!resolveConfiguredAnalogSource_(idx, source)) return false;
    return analogSourceRequiresDriverEnable_(source) && analogSourceDriverEnabled_(source);
}

void IOModule::invalidateAnalogSlot_(AnalogSlot& slot, uint32_t nowMs)
{
    if (!slot.endpoint) return;
    if (!slot.lastRoundedValid) return;

    slot.endpoint->update(slot.lastRounded, false, nowMs);
    slot.lastRoundedValid = false;

    if (dataStore_) {
        uint8_t rtIdx = 0;
        if (endpointIndexFromId_(slot.def.id, rtIdx)) {
            (void)setIoEndpointInvalid(*dataStore_, rtIdx, IO_VALUE_FLOAT, nowMs);
        }
    }
    markIoCycleChanged_(slot.ioId);
}

bool IOModule::processAnalogDefinition_(uint8_t idx, uint32_t nowMs)
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return false;
    AnalogSlot& slot = analogSlots_[idx];
    if (!slot.used || !slot.endpoint) return false;

    const IOAnalogProvider* provider = analogProviderForSource_(slot.source);
    if (!provider || !provider->isBound()) {
        invalidateAnalogSlot_(slot, nowMs);
        return false;
    }

    IOAnalogSample sample{};
    const uint8_t readChannel =
        (slot.source == IO_SRC_DS18_WATER || slot.source == IO_SRC_DS18_AIR) ? 0U : slot.channel;
    if (!provider->readSample(readChannel, sample)) {
        invalidateAnalogSlot_(slot, nowMs);
        return false;
    }
    float raw = sample.value;
    int16_t rawBinary = sample.raw;
    uint32_t sampleSeq = sample.seq;
    bool hasSampleSeq = sample.hasSeq;

    // Providers expose an optional sequence so multi-channel sensors only update endpoints on fresh acquisitions.
    if (hasSampleSeq) {
        if (slot.lastSampleSeqValid && sampleSeq == slot.lastSampleSeq) return false;
        slot.lastSampleSeq = sampleSeq;
        slot.lastSampleSeqValid = true;
    }

    float filtered = slot.median.update(raw);
    float calibrated = (slot.def.c0 * filtered) + slot.def.c1;
    float rounded = ioRoundToPrecision(calibrated, slot.def.precision);

    // Trace pH/ORP/PSI calculation chain with configurable periodic ticker.
    bool isAdsSource = (slot.source == IO_SRC_ADS_INTERNAL_SINGLE) ||
                       (slot.source == IO_SRC_ADS_EXTERNAL_DIFF);
    if (cfgData_.traceEnabled && isAdsSource && idx < 3) {
        uint32_t periodMs =
            (cfgData_.tracePeriodMs > 0) ? (uint32_t)cfgData_.tracePeriodMs : Limits::IoTracePeriodMs;
        uint32_t& lastMs = analogCalcLogLastMs_[idx];
        if (lastMs == 0U || (uint32_t)(nowMs - lastMs) >= periodMs) {
            const char* sensor = (idx == 0) ? "ORP" : ((idx == 1) ? "pH" : "PSI");
            const char sourceMark = (slot.source == IO_SRC_ADS_INTERNAL_SINGLE) ? 'I' : 'E';
            LOGD("Calc %c %-3s raw_bin=%7d raw_V=%10.6f median_V=%10.6f coeff=%9.3f rounded=%9.3f",
                 sourceMark,
                 sensor,
                 (int)rawBinary,
                 (double)raw,
                 (double)filtered,
                 (double)calibrated,
                 (double)rounded);
            lastMs = nowMs;
        }
    }

    slot.endpoint->update(rounded, true, nowMs);

    if (!slot.lastRoundedValid || rounded != slot.lastRounded) {
        slot.lastRounded = rounded;
        slot.lastRoundedValid = true;
        if (dataStore_) {
            uint8_t rtIdx = 0;
            if (endpointIndexFromId_(slot.def.id, rtIdx)) {
                (void)setIoEndpointFloat(*dataStore_, rtIdx, rounded, nowMs);
            }
        }
        markIoCycleChanged_(slot.ioId);
        if (slot.def.onValueChanged) {
            slot.def.onValueChanged(slot.def.onValueCtx, rounded);
        }
    }

    return true;
}

bool IOModule::processDigitalInputDefinition_(uint8_t slotIdx, uint32_t nowMs)
{
    if (slotIdx >= MAX_DIGITAL_SLOTS) return false;
    DigitalSlot& slot = digitalSlots_[slotIdx];
    if (!slot.used || slot.kind != DIGITAL_SLOT_INPUT || !slot.endpoint) return false;
    if (slot.endpoint->type() != IO_EP_DIGITAL_SENSOR) return false;

    DigitalSensorEndpoint* inputEp = static_cast<DigitalSensorEndpoint*>(slot.endpoint);

    if (slot.inDef.mode == IO_DIGITAL_INPUT_COUNTER) {
        if (!slot.provider.isBound()) return false;
        IDigitalCounterDriver* counterDriver = static_cast<IDigitalCounterDriver*>(slot.provider.ctx);
        if (!counterDriver) return false;

        const IODigitalInputSlotConfig* cfg = (slot.logicalIdx < MAX_DIGITAL_INPUTS) ? &digitalInCfg_[slot.logicalIdx] : nullptr;
        const float c0 = cfg ? cfg->c0 : 1.0f;
        const int32_t precision = sanitizeAnalogPrecision_(cfg ? cfg->precision : 0);

        int32_t rawCount = 0;
        if (!counterDriver->readCount(rawCount)) {
            if (slot.lastValid) {
                const float invalidValue = ioRoundToPrecision(slot.counterScaledTotal, precision);
                inputEp->updateFloat(invalidValue, false, nowMs);
                slot.lastValid = false;
            }
            return false;
        }

        float* lastConfigTotal = counterConfigTotalState_(slot.logicalIdx);
        if (cfg && lastConfigTotal && *lastConfigTotal != cfg->counterTotal) {
            slot.counterScaledTotal = cfg->counterTotal;
            slot.counterLastPersistedTotal = cfg->counterTotal;
            *lastConfigTotal = cfg->counterTotal;
            slot.counterLastRawCount = rawCount;
            slot.counterLastFlushedRawCount = rawCount;
            slot.counterLastPersistMs = nowMs;
        }

        const int32_t delta = rawCount - slot.counterLastRawCount;
        if (delta > 0) {
            slot.counterScaledTotal += ((float)delta * c0);
            slot.counterLastRawCount = rawCount;
            if (cfgData_.traceEnabled) {
                const float tracedScaledValue = ioRoundToPrecision(slot.counterScaledTotal, precision);
                LOGI("Counter pulse i%02u io=%u raw=%ld delta=%ld total=%.3f",
                     (unsigned)slot.logicalIdx,
                     (unsigned)slot.ioId,
                     (long)rawCount,
                     (long)delta,
                     (double)tracedScaledValue);
            }
        } else if (delta < 0) {
            if (cfgData_.traceEnabled) {
                LOGW("Counter raw reset i%02u io=%u raw=%ld prev_raw=%ld",
                     (unsigned)slot.logicalIdx,
                     (unsigned)slot.ioId,
                     (long)rawCount,
                     (long)slot.counterLastRawCount);
            }
            slot.counterLastRawCount = rawCount;
            slot.counterLastFlushedRawCount = rawCount;
        }
        (void)persistCounterTotalIfNeeded_(slot, rawCount, nowMs);

        const float scaledValue = ioRoundToPrecision(slot.counterScaledTotal, precision);

        IOEndpointValue prev{};
        const bool hasPrev = inputEp->read(prev) && prev.valid && prev.valueType == IO_EP_VALUE_FLOAT;
        const bool changed = (!slot.lastValid) || (delta != 0) || !hasPrev || (prev.v.f != scaledValue);
        if (changed) {
            inputEp->updateFloat(scaledValue, true, nowMs);
            slot.lastValid = true;
            if (dataStore_) {
                uint8_t rtIdx = 0;
                if (endpointIndexFromId_(slot.endpointId, rtIdx)) {
                    (void)setIoEndpointFloat(*dataStore_, rtIdx, scaledValue, nowMs);
                }
            }
            markIoCycleChanged_(slot.ioId);
        }
        return true;
    }

    if (!slot.provider.isBound()) return false;

    bool on = false;
    if (!slot.provider.read(on)) {
        // Transition to invalid only once; avoid timestamp churn while input remains unreadable.
        if (slot.lastValid) {
            inputEp->update(false, false, nowMs);
            slot.lastValid = false;
        }
        return false;
    }

    const bool changed = (!slot.lastValid) || (slot.lastValue != on);
    if (changed) {
        inputEp->update(on, true, nowMs);
        slot.lastValue = on;
        slot.lastValid = true;
        if (dataStore_) {
            uint8_t rtIdx = 0;
            if (endpointIndexFromId_(slot.endpointId, rtIdx)) {
                (void)setIoEndpointBool(*dataStore_, rtIdx, on, nowMs);
            }
        }
        markIoCycleChanged_(slot.ioId);
        if (slot.inDef.onValueChanged) {
            slot.inDef.onValueChanged(slot.inDef.onValueCtx, on);
        }
    }

    return true;
}

void IOModule::traceDigitalCounters_(uint32_t nowMs)
{
    if (!cfgData_.traceEnabled || !runtimeReady_) return;
    if (counterTraceLastMs_ != 0U && (uint32_t)(nowMs - counterTraceLastMs_) < 1000U) return;
    counterTraceLastMs_ = nowMs;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& slot = digitalSlots_[i];
        if (!slot.used || slot.kind != DIGITAL_SLOT_INPUT) continue;
        if (slot.inDef.mode != IO_DIGITAL_INPUT_COUNTER) continue;
        if (!slot.provider.isBound()) continue;

        IDigitalCounterDriver* counterDriver = static_cast<IDigitalCounterDriver*>(slot.provider.ctx);
        if (!counterDriver) continue;

        IODigitalCounterDebugStats stats{};
        if (!counterDriver->readDebugStats(stats)) continue;
        LOGD("Counter dbg i%02u pin=%u accepted=%ld raw_hw=%lu polls=%lu dropped_db=%lu active_high=%u edge_mode=%u",
             (unsigned)slot.logicalIdx,
             (unsigned)stats.pin,
             (long)stats.pulseCount,
             (unsigned long)stats.irqCalls,
             (unsigned long)stats.transitions,
             (unsigned long)stats.ignoredDebounce,
             (unsigned)stats.activeHigh,
             (unsigned)stats.edgeMode);

    }
}

int32_t IOModule::sanitizeAnalogPrecision_(int32_t precision) const
{
    if (precision < 0) return 0;
    if (precision > 6) return 6;
    return precision;
}

void IOModule::forceAnalogSnapshotPublish_(uint8_t analogIdx, uint32_t nowMs)
{
    if (analogIdx >= MAX_ANALOG_ENDPOINTS) return;
    AnalogSlot& slot = analogSlots_[analogIdx];
    if (!slot.used || !slot.endpoint) return;

    IOEndpointValue v{};
    if (!slot.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_FLOAT) return;

    float republished = ioRoundToPrecision(v.v.f, slot.def.precision);
    slot.endpoint->update(republished, true, nowMs);
    if (dataStore_) {
        (void)setIoEndpointFloat(*dataStore_, analogIdx, republished, nowMs);
    }
}

void IOModule::refreshAnalogConfigState_()
{
    if (!ensureAnalogPrecisionState_()) return;

    // `c0/c1` live in the logical slot, not in the shared provider. Keeping them
    // synced here makes the next acquired sample use the new calibration without reboot.
    if (runtimeReady_) {
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            if (i >= MAX_ANALOG_ENDPOINTS) continue;
            if (!analogSlots_[i].used) continue;
            analogSlots_[i].def.c0 = analogCfg_[i].c0;
            analogSlots_[i].def.c1 = analogCfg_[i].c1;
        }
    }

    if (!analogPrecisionLastInit_) {
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            int32_t p = sanitizeAnalogPrecision_(analogCfg_[i].precision);
            analogPrecisionLast_[i] = p;
        }
        analogPrecisionLastInit_ = true;
        return;
    }

    bool changed = false;
    uint32_t changedMask = 0;
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        int32_t p = sanitizeAnalogPrecision_(analogCfg_[i].precision);
        if (analogPrecisionLast_[i] == p) continue;
        analogPrecisionLast_[i] = p;
        if (runtimeReady_ && i < MAX_ANALOG_ENDPOINTS && analogSlots_[i].used) {
            analogSlots_[i].def.precision = p;
        }
        changedMask |= (uint32_t)(1u << i);
        changed = true;
    }

    if (changed) {
        LOGI("Input precision changed -> publish runtime snapshot");
        const uint32_t nowMs = millis();
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            if ((changedMask & (uint32_t)(1u << i)) == 0) continue;
            forceAnalogSnapshotPublish_(i, nowMs);
        }
        analogConfigDirtyMask_ |= changedMask;
    }
}

uint8_t IOModule::ioCount_() const
{
    uint8_t count = 0;
    for (uint8_t logical = 0; logical < MAX_DIGITAL_OUTPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logical, slotIdx)) ++count;
    }
    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logical, slotIdx)) ++count;
    }
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (analogSlots_[i].used) ++count;
    }
    return count;
}

IoStatus IOModule::ioIdAt_(uint8_t index, IoId* outId) const
{
    if (!outId) return IO_ERR_INVALID_ARG;
    uint8_t seen = 0;

    for (uint8_t logical = 0; logical < MAX_DIGITAL_OUTPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logical, slotIdx)) continue;
        if (seen == index) {
            *outId = digitalSlots_[slotIdx].ioId;
            return IO_OK;
        }
        ++seen;
    }

    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logical, slotIdx)) continue;
        if (seen == index) {
            *outId = digitalSlots_[slotIdx].ioId;
            return IO_OK;
        }
        ++seen;
    }

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogSlots_[i].used) continue;
        if (seen == index) {
            *outId = analogSlots_[i].ioId;
            return IO_OK;
        }
        ++seen;
    }

    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioMeta_(IoId id, IoEndpointMeta* outMeta) const
{
    if (!outMeta) return IO_ERR_INVALID_ARG;
    *outMeta = IoEndpointMeta{};
    outMeta->id = id;

    uint8_t slotIdx = 0xFF;
    if (findDigitalSlotByIoId_(id, slotIdx)) {
        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (!s.used) return IO_ERR_UNKNOWN_ID;

        outMeta->kind = (s.kind == DIGITAL_SLOT_OUTPUT) ? IO_KIND_DIGITAL_OUT : IO_KIND_DIGITAL_IN;
        outMeta->valueType = (s.kind == DIGITAL_SLOT_OUTPUT)
            ? IO_VAL_BOOL
            : ((s.inDef.mode == IO_DIGITAL_INPUT_COUNTER) ? IO_VAL_FLOAT : IO_VAL_BOOL);
        outMeta->backend = s.backend;
        outMeta->channel = s.channel;
        outMeta->bindingPort = (s.kind == DIGITAL_SLOT_OUTPUT) ? s.outDef.bindingPort : s.inDef.bindingPort;
        if (s.kind == DIGITAL_SLOT_OUTPUT && s.logicalIdx < DIGITAL_CFG_SLOTS) {
            outMeta->bindingPort = digitalCfg_[s.logicalIdx].bindingPort;
        } else if (s.kind == DIGITAL_SLOT_INPUT && s.logicalIdx < DIGITAL_INPUT_CFG_SLOTS) {
            outMeta->bindingPort = digitalInCfg_[s.logicalIdx].bindingPort;
        }

        if (s.kind == DIGITAL_SLOT_INPUT) {
            uint8_t pin = 0U;
            uint8_t backend = IO_BACKEND_GPIO;
            uint8_t channel = 0U;
            IOExpanderId expanderId = IO_EXPANDER_INVALID;
            if (resolveDigitalInputBinding_(outMeta->bindingPort, pin, backend, channel, expanderId)) {
                outMeta->backend = backend;
                outMeta->channel = channel;
            }
        } else {
            uint8_t pin = 0U;
            uint8_t backend = IO_BACKEND_GPIO;
            uint8_t channel = 0U;
            IOExpanderId expanderId = IO_EXPANDER_INVALID;
            bool usesPcfOut = false;
            bool usesTcaOut = false;
            bool usesMcpOut = false;
            if (resolveDigitalOutputBinding_(outMeta->bindingPort,
                                             pin,
                                             backend,
                                             channel,
                                             expanderId,
                                             usesPcfOut,
                                             usesTcaOut,
                                             usesMcpOut)) {
                outMeta->backend = backend;
                outMeta->channel = channel;
            }
        }
        outMeta->capabilities = s.endpoint ? IO_CAP_R : 0;
        if (s.kind == DIGITAL_SLOT_OUTPUT && s.provider.isBound()) {
            outMeta->capabilities |= IO_CAP_W;
        }
        if (s.kind == DIGITAL_SLOT_INPUT && s.inDef.mode == IO_DIGITAL_INPUT_COUNTER && s.logicalIdx < MAX_DIGITAL_INPUTS) {
            outMeta->precision = sanitizeAnalogPrecision_(digitalInCfg_[s.logicalIdx].precision);
        }

        const char* name = nullptr;
        if (s.kind == DIGITAL_SLOT_OUTPUT && s.logicalIdx < DIGITAL_CFG_SLOTS) {
            name = digitalCfg_[s.logicalIdx].name;
        } else if (s.kind == DIGITAL_SLOT_INPUT) {
            if (s.logicalIdx < MAX_DIGITAL_INPUTS && digitalInCfg_[s.logicalIdx].name[0] != '\0') {
                name = digitalInCfg_[s.logicalIdx].name;
            } else {
                name = s.inDef.id;
            }
        }
        if (!name || name[0] == '\0') name = s.endpointId;
        if (!name) name = "";
        strncpy(outMeta->name, name, sizeof(outMeta->name) - 1);
        outMeta->name[sizeof(outMeta->name) - 1] = '\0';
        return IO_OK;
    }

    if (id >= IO_ID_AI_BASE && id < IO_ID_AI_MAX) {
        const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
        const AnalogSlot& s = analogSlots_[analogIdx];
        if (!s.used) return IO_ERR_UNKNOWN_ID;

        outMeta->kind = IO_KIND_ANALOG_IN;
        outMeta->valueType = IO_VAL_FLOAT;
        outMeta->capabilities = s.endpoint ? IO_CAP_R : 0;
        outMeta->channel = s.channel;
        outMeta->backend = s.backend;
        outMeta->bindingPort = (analogIdx < ANALOG_CFG_SLOTS)
            ? analogCfg_[analogIdx].bindingPort
            : s.def.bindingPort;
        uint8_t source = IO_ANALOG_SOURCE_INVALID;
        uint8_t channel = 0U;
        uint8_t backend = IO_BACKEND_GPIO;
        if (resolveAnalogBinding_(outMeta->bindingPort, source, channel, backend)) {
            outMeta->channel = channel;
            outMeta->backend = backend;
        }
        outMeta->precision = s.def.precision;
        outMeta->minValid = 0.0f;
        outMeta->maxValid = 0.0f;

        const char* name = (analogIdx < ANALOG_CFG_SLOTS) ? analogCfg_[analogIdx].name : nullptr;
        if (!name || name[0] == '\0') name = s.def.id;
        if (!name) name = "";
        strncpy(outMeta->name, name, sizeof(outMeta->name) - 1);
        outMeta->name[sizeof(outMeta->name) - 1] = '\0';
        return IO_OK;
    }

    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioRuntimeStatus_(IoId id, IoRuntimeStatus* outStatus) const
{
    if (!outStatus) return IO_ERR_INVALID_ARG;
    *outStatus = IoRuntimeStatus{};
    outStatus->id = id;

    IoEndpointMeta meta{};
    const IoStatus metaStatus = ioMeta_(id, &meta);
    if (metaStatus != IO_OK) return metaStatus;

    if (!cfgData_.enabled) {
        outStatus->state = IO_RUNTIME_MANUALLY_DISABLED;
        outStatus->reason = IO_RUNTIME_REASON_IO_MODULE_DISABLED;
        return IO_OK;
    }

    if (meta.bindingPort == IO_PORT_INVALID) {
        outStatus->state = IO_RUNTIME_SLEEPING;
        outStatus->reason = IO_RUNTIME_REASON_UNBOUND;
        return IO_OK;
    }

    const IOBindingPortSpec* port = bindingPortSpec_(meta.bindingPort);
    if (!port) {
        outStatus->state = IO_RUNTIME_SLEEPING;
        outStatus->reason = IO_RUNTIME_REASON_UNBOUND;
        return IO_OK;
    }

    const bool usesExpander =
        port->kind == IO_PORT_KIND_PCF8574_OUTPUT ||
        port->kind == IO_PORT_KIND_TCA9554_OUTPUT ||
        port->kind == IO_PORT_KIND_MCP23017_INPUT ||
        port->kind == IO_PORT_KIND_MCP23017_OUTPUT;
    if (usesExpander) {
        outStatus->expanderId = port->expanderId;
        if (!expanderEnabled_(port->expanderId)) {
            outStatus->state = IO_RUNTIME_MANUALLY_DISABLED;
            outStatus->reason = IO_RUNTIME_REASON_EXPANDER_DISABLED;
            return IO_OK;
        }
    }

    if (meta.kind == IO_KIND_ANALOG_IN) {
        const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
        uint8_t source = IO_ANALOG_SOURCE_INVALID;
        if (!resolveConfiguredAnalogSource_(analogIdx, source)) {
            outStatus->state = IO_RUNTIME_SLEEPING;
            outStatus->reason = IO_RUNTIME_REASON_UNBOUND;
            return IO_OK;
        }
        if (analogSourceRequiresDriverEnable_(source) && !analogSourceDriverEnabled_(source)) {
            outStatus->state = IO_RUNTIME_MANUALLY_DISABLED;
            outStatus->reason = IO_RUNTIME_REASON_DRIVER_DISABLED;
            return IO_OK;
        }
        if (analogSlots_[analogIdx].endpoint) {
            outStatus->state = IO_RUNTIME_ACTIVE;
            return IO_OK;
        }
    } else {
        uint8_t slotIdx = 0xFFU;
        if (findDigitalSlotByIoId_(id, slotIdx) && digitalSlots_[slotIdx].endpoint) {
            outStatus->state = IO_RUNTIME_ACTIVE;
            return IO_OK;
        }
    }

    outStatus->state = IO_RUNTIME_ERROR;
    if (usesExpander && port->expanderId < IO_MAX_EXPANDERS &&
        runtimeExpanders_[port->expanderId].beginAttempted &&
        !runtimeExpanders_[port->expanderId].beginOk) {
        outStatus->reason = IO_RUNTIME_REASON_HARDWARE_NOT_DETECTED;
    } else {
        outStatus->reason = IO_RUNTIME_REASON_DRIVER_INIT_FAILED;
    }
    return IO_OK;
}

IoStatus IOModule::ioBindingPortStatus_(PhysicalPortId portId, IoRuntimeStatus* outStatus) const
{
    if (!outStatus) return IO_ERR_INVALID_ARG;
    *outStatus = IoRuntimeStatus{};

    const IOBindingPortSpec* port = bindingPortSpec_(portId);
    if (!port) return IO_ERR_UNKNOWN_ID;

    if (!cfgData_.enabled) {
        outStatus->state = IO_RUNTIME_MANUALLY_DISABLED;
        outStatus->reason = IO_RUNTIME_REASON_IO_MODULE_DISABLED;
        return IO_OK;
    }

    const bool usesExpander =
        port->kind == IO_PORT_KIND_PCF8574_OUTPUT ||
        port->kind == IO_PORT_KIND_TCA9554_OUTPUT ||
        port->kind == IO_PORT_KIND_MCP23017_INPUT ||
        port->kind == IO_PORT_KIND_MCP23017_OUTPUT;
    if (!usesExpander) {
        outStatus->state = IO_RUNTIME_SLEEPING;
        return IO_OK;
    }

    outStatus->expanderId = port->expanderId;
    if (!expanderEnabled_(port->expanderId)) {
        outStatus->state = IO_RUNTIME_MANUALLY_DISABLED;
        outStatus->reason = IO_RUNTIME_REASON_EXPANDER_DISABLED;
        return IO_OK;
    }
    if (port->expanderId < IO_MAX_EXPANDERS &&
        runtimeExpanders_[port->expanderId].beginAttempted &&
        !runtimeExpanders_[port->expanderId].beginOk) {
        outStatus->state = IO_RUNTIME_ERROR;
        outStatus->reason = IO_RUNTIME_REASON_HARDWARE_NOT_DETECTED;
        return IO_OK;
    }

    outStatus->state = IO_RUNTIME_SLEEPING;
    return IO_OK;
}

IoStatus IOModule::ioReadValue_(IoId id, IoValue* outValue) const
{
    if (!outValue) return IO_ERR_INVALID_ARG;
    *outValue = IoValue{};

    IoRuntimeStatus runtime{};
    const IoStatus runtimeResult = ioRuntimeStatus_(id, &runtime);
    if (runtimeResult != IO_OK) return runtimeResult;
    if (runtime.state == IO_RUNTIME_MANUALLY_DISABLED) return IO_ERR_DISABLED;
    if (runtime.state == IO_RUNTIME_ERROR) return IO_ERR_HW;
    if (runtime.state != IO_RUNTIME_ACTIVE) return IO_ERR_NOT_READY;

    uint8_t slotIdx = 0xFF;
    if (findDigitalSlotByIoId_(id, slotIdx)) {
        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

        IOEndpointValue v{};
        if (!s.endpoint->read(v) || !v.valid) return IO_ERR_NOT_READY;

        outValue->valid = 1U;
        outValue->tsMs = v.timestampMs;
        outValue->cycleSeq = lastCycle_ ? lastCycle_->seq : 0U;
        if (v.valueType == IO_EP_VALUE_BOOL) {
            outValue->type = IO_VAL_BOOL;
            outValue->v.b = v.v.b ? 1U : 0U;
            return IO_OK;
        }
        if (v.valueType == IO_EP_VALUE_INT32) {
            outValue->type = IO_VAL_INT32;
            outValue->v.i32 = v.v.i;
            return IO_OK;
        }
        if (v.valueType == IO_EP_VALUE_FLOAT) {
            outValue->type = IO_VAL_FLOAT;
            outValue->v.f = v.v.f;
            return IO_OK;
        }
        return IO_ERR_TYPE_MISMATCH;
    }

    if (id >= IO_ID_AI_BASE && id < IO_ID_AI_MAX) {
        const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
        const AnalogSlot& s = analogSlots_[analogIdx];
        if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

        IOEndpointValue v{};
        if (!s.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_FLOAT) return IO_ERR_NOT_READY;

        outValue->valid = 1U;
        outValue->type = IO_VAL_FLOAT;
        outValue->tsMs = v.timestampMs;
        outValue->cycleSeq = lastCycle_ ? lastCycle_->seq : 0U;
        outValue->v.f = v.v.f;
        return IO_OK;
    }

    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioReadDigital_(IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq) const
{
    if (!outOn) return IO_ERR_INVALID_ARG;

    IoRuntimeStatus runtime{};
    const IoStatus runtimeResult = ioRuntimeStatus_(id, &runtime);
    if (runtimeResult != IO_OK) return runtimeResult;
    if (runtime.state == IO_RUNTIME_MANUALLY_DISABLED) return IO_ERR_DISABLED;
    if (runtime.state == IO_RUNTIME_ERROR) return IO_ERR_HW;
    if (runtime.state != IO_RUNTIME_ACTIVE) return IO_ERR_NOT_READY;

    uint8_t slotIdx = 0xFF;
    if (!findDigitalSlotByIoId_(id, slotIdx)) return IO_ERR_UNKNOWN_ID;
    const DigitalSlot& s = digitalSlots_[slotIdx];
    if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

    IOEndpointValue v{};
    if (!s.endpoint->read(v) || !v.valid) return IO_ERR_NOT_READY;
    if (v.valueType != IO_EP_VALUE_BOOL) return IO_ERR_TYPE_MISMATCH;

    *outOn = v.v.b ? 1U : 0U;
    if (outTsMs) *outTsMs = v.timestampMs;
    if (outSeq) *outSeq = lastCycle_ ? lastCycle_->seq : 0U;
    return IO_OK;
}

IoStatus IOModule::ioWriteDigital_(IoId id, uint8_t on, uint32_t tsMs)
{
    IoRuntimeStatus runtime{};
    const IoStatus runtimeResult = ioRuntimeStatus_(id, &runtime);
    if (runtimeResult != IO_OK) return runtimeResult;
    if (runtime.state == IO_RUNTIME_MANUALLY_DISABLED) return IO_ERR_DISABLED;
    if (runtime.state == IO_RUNTIME_ERROR) return IO_ERR_HW;
    if (runtime.state != IO_RUNTIME_ACTIVE) return IO_ERR_NOT_READY;

    uint8_t slotIdx = 0xFF;
    if (!findDigitalSlotByIoId_(id, slotIdx)) return IO_ERR_UNKNOWN_ID;
    DigitalSlot& s = digitalSlots_[slotIdx];
    if (!s.used) return IO_ERR_UNKNOWN_ID;
    if (s.kind != DIGITAL_SLOT_OUTPUT) return IO_ERR_READ_ONLY;
    if (!s.endpoint) return IO_ERR_NOT_READY;

    IOEndpointValue in{};
    in.timestampMs = (tsMs == 0) ? millis() : tsMs;
    in.valueType = IO_EP_VALUE_BOOL;
    in.v.b = (on != 0U);
    in.valid = true;
    if (!s.endpoint->write(in)) return IO_ERR_HW;

    if (dataStore_) {
        uint8_t rtIdx = 0;
        if (endpointIndexFromId_(s.endpointId, rtIdx)) {
            (void)setIoEndpointBool(*dataStore_, rtIdx, in.v.b, in.timestampMs);
        }
    }

    markIoCycleChanged_(s.ioId);
    return IO_OK;
}

IoStatus IOModule::ioReadAnalog_(IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq) const
{
    if (!outValue) return IO_ERR_INVALID_ARG;
    if (id < IO_ID_AI_BASE || id >= IO_ID_AI_MAX) return IO_ERR_UNKNOWN_ID;

    IoRuntimeStatus runtime{};
    const IoStatus runtimeResult = ioRuntimeStatus_(id, &runtime);
    if (runtimeResult != IO_OK) return runtimeResult;
    if (runtime.state == IO_RUNTIME_MANUALLY_DISABLED) return IO_ERR_DISABLED;
    if (runtime.state == IO_RUNTIME_ERROR) return IO_ERR_HW;
    if (runtime.state != IO_RUNTIME_ACTIVE) return IO_ERR_NOT_READY;

    const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
    const AnalogSlot& s = analogSlots_[analogIdx];
    if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

    IOEndpointValue v{};
    if (!s.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_FLOAT) return IO_ERR_NOT_READY;

    *outValue = v.v.f;
    if (outTsMs) *outTsMs = v.timestampMs;
    if (outSeq) *outSeq = lastCycle_ ? lastCycle_->seq : 0U;
    return IO_OK;
}

IoStatus IOModule::ioTick_(uint32_t nowMs)
{
    refreshAnalogConfigState_();

    if (!cfgData_.enabled) return IO_ERR_NOT_READY;
    if (!runtimeReady_) return IO_ERR_NOT_READY;

    beginIoCycle_(nowMs);
    scheduler_.tick(nowMs);
    traceDigitalCounters_(nowMs);
    return IO_OK;
}

IoStatus IOModule::ioLastCycle_(IoCycleInfo* outCycle) const
{
    if (!outCycle) return IO_ERR_INVALID_ARG;
    *outCycle = lastCycle_ ? *lastCycle_ : IoCycleInfo{};
    return IO_OK;
}

IoStatus IOModule::ioSensorStatus_(IoId id, IoSensorStatus* outStatus) const
{
    if (!outStatus) return IO_ERR_INVALID_ARG;
    *outStatus = IoSensorStatus{};
    outStatus->id = id;

    if (!cfgData_.enabled) {
        outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED;
        return IO_OK;
    }

    IoRuntimeStatus runtime{};
    const IoStatus runtimeResult = ioRuntimeStatus_(id, &runtime);
    if (runtimeResult == IO_OK && runtime.state == IO_RUNTIME_MANUALLY_DISABLED) {
        outStatus->enabled = 0U;
        outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED | IO_SENSOR_INVALID_DRIVER_DISABLED;
        return IO_OK;
    }

    if (id >= IO_ID_AI_BASE && id < IO_ID_AI_MAX) {
        const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
        outStatus->kind = IO_KIND_ANALOG_IN;

        if (!analogSlots_[analogIdx].used) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_UNKNOWN_ID;
            return IO_ERR_UNKNOWN_ID;
        }

        if (analogIdx < ANALOG_CFG_SLOTS && analogCfg_[analogIdx].bindingPort == IO_PORT_INVALID) {
            outStatus->enabled = 0U;
            outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED | IO_SENSOR_INVALID_NO_BINDING;
            return IO_OK;
        }

        if (!analogSlotPublished_(analogIdx)) {
            outStatus->enabled = 0U;
            outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED;

            if (analogIdx < ANALOG_CFG_SLOTS) {
                if (analogCfg_[analogIdx].bindingPort == IO_PORT_INVALID) {
                    outStatus->invalidReasons |= IO_SENSOR_INVALID_NO_BINDING;
                } else {
                    uint8_t source = IO_ANALOG_SOURCE_INVALID;
                    if (!resolveConfiguredAnalogSource_(analogIdx, source)) {
                        outStatus->invalidReasons |= IO_SENSOR_INVALID_NO_BINDING;
                    } else if (analogSourceRequiresDriverEnable_(source) && !analogSourceDriverEnabled_(source)) {
                        outStatus->invalidReasons |= IO_SENSOR_INVALID_DRIVER_DISABLED;
                    }
                }
            }

            return IO_OK;
        }

        outStatus->enabled = 1U;
        if (!analogSlots_[analogIdx].endpoint) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_READY;
            return IO_OK;
        }

        IOEndpointValue v{};
        if (!analogSlots_[analogIdx].endpoint->read(v)) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_READY;
            return IO_OK;
        }
        if (!v.valid) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NO_VALUE;
            outStatus->tsMs = v.timestampMs;
            return IO_OK;
        }
        if (v.valueType != IO_EP_VALUE_FLOAT) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_TYPE;
            outStatus->tsMs = v.timestampMs;
            return IO_OK;
        }

        outStatus->valid = 1U;
        outStatus->invalidReasons = IO_SENSOR_INVALID_NONE;
        outStatus->tsMs = v.timestampMs;
        return IO_OK;
    }

    if (id >= IO_ID_DI_BASE && id < IO_ID_DI_MAX) {
        const uint8_t logicalIdx = (uint8_t)(id - IO_ID_DI_BASE);
        outStatus->kind = IO_KIND_DIGITAL_IN;

        if (logicalIdx < DIGITAL_INPUT_CFG_SLOTS &&
            digitalInCfg_[logicalIdx].bindingPort == IO_PORT_INVALID) {
            outStatus->enabled = 0U;
            outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED | IO_SENSOR_INVALID_NO_BINDING;
            return IO_OK;
        }

        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByIoId_(id, slotIdx)) {
            outStatus->enabled = 0U;
            outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED;
            outStatus->invalidReasons |= IO_SENSOR_INVALID_UNKNOWN_ID;
            return IO_OK;
        }

        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (!s.used || s.kind != DIGITAL_SLOT_INPUT) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_SENSOR;
            return IO_ERR_TYPE_MISMATCH;
        }

        outStatus->enabled = 1U;
        if (!s.endpoint) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_READY;
            return IO_OK;
        }

        IOEndpointValue v{};
        if (!s.endpoint->read(v)) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_READY;
            return IO_OK;
        }
        if (!v.valid) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NO_VALUE;
            outStatus->tsMs = v.timestampMs;
            return IO_OK;
        }

        outStatus->valid = 1U;
        outStatus->invalidReasons = IO_SENSOR_INVALID_NONE;
        outStatus->tsMs = v.timestampMs;
        return IO_OK;
    }

    if (id >= IO_ID_DO_BASE && id < IO_ID_DO_MAX) {
        outStatus->kind = IO_KIND_DIGITAL_OUT;
        outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_SENSOR;
        return IO_ERR_TYPE_MISMATCH;
    }

    outStatus->invalidReasons = IO_SENSOR_INVALID_UNKNOWN_ID;
    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioListInvalidSensors_(IoId* outIds, uint8_t maxIds, uint8_t* outCount) const
{
    if (!outCount) return IO_ERR_INVALID_ARG;
    *outCount = 0U;

    uint8_t written = 0U;
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        IoSensorStatus st{};
        if (ioSensorStatus_((IoId)(IO_ID_AI_BASE + i), &st) != IO_OK) continue;
        if (!st.enabled || st.valid) continue;
        if (outIds && written < maxIds) outIds[written++] = st.id;
        if (*outCount < 0xFFU) ++(*outCount);
    }

    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        IoSensorStatus st{};
        if (ioSensorStatus_((IoId)(IO_ID_DI_BASE + logical), &st) != IO_OK) continue;
        if (!st.enabled || st.valid) continue;
        if (outIds && written < maxIds) outIds[written++] = st.id;
        if (*outCount < 0xFFU) ++(*outCount);
    }

    return IO_OK;
}

const IOBindingPortSpec* IOModule::bindingPortSpec_(PhysicalPortId portId) const
{
    if (portId == IO_PORT_INVALID || !bindingPorts_ || bindingPortCount_ == 0) return nullptr;
    for (uint8_t i = 0; i < bindingPortCount_; ++i) {
        if (bindingPorts_[i].portId == portId) return &bindingPorts_[i];
    }
    return nullptr;
}

const IOExpanderSpec* IOModule::expanderSpec_(IOExpanderId expanderId) const
{
    if (expanderId == IO_EXPANDER_INVALID || expanderId >= IO_MAX_EXPANDERS) return nullptr;
    if (runtimeExpanders_[expanderId].spec) return runtimeExpanders_[expanderId].spec;
    if (!expanders_) return nullptr;
    for (uint8_t i = 0; i < expanderCount_; ++i) {
        if (expanders_[i].expanderId == expanderId) return &expanders_[i];
    }
    return nullptr;
}

IOExpanderConfig* IOModule::expanderConfig_(IOExpanderId expanderId)
{
    if (expanderId == IO_EXPANDER_INVALID || expanderId >= IO_MAX_EXPANDERS) return nullptr;
    return &expanderCfg_[expanderId];
}

const IOExpanderConfig* IOModule::expanderConfig_(IOExpanderId expanderId) const
{
    if (expanderId == IO_EXPANDER_INVALID || expanderId >= IO_MAX_EXPANDERS) return nullptr;
    return &expanderCfg_[expanderId];
}

bool IOModule::expanderEnabled_(IOExpanderId expanderId) const
{
    const IOExpanderConfig* cfg = expanderConfig_(expanderId);
    return cfg ? cfg->enabled : false;
}

bool IOModule::expanderUsable_(IOExpanderId expanderId) const
{
    if (expanderId == IO_EXPANDER_INVALID || expanderId >= IO_MAX_EXPANDERS) return false;
    return runtimeExpanders_[expanderId].configValid && expanderEnabled_(expanderId);
}

uint8_t IOModule::expanderAddress_(IOExpanderId expanderId) const
{
    const IOExpanderConfig* cfg = expanderConfig_(expanderId);
    return cfg ? cfg->address : 0;
}

uint8_t IOModule::expanderMaskDefault_(IOExpanderId expanderId) const
{
    const IOExpanderConfig* cfg = expanderConfig_(expanderId);
    return cfg ? cfg->maskDefault : 0;
}

bool IOModule::expanderOutputsInverted_(IOExpanderId expanderId) const
{
    const IOExpanderSpec* spec = expanderSpec_(expanderId);
    return spec ? spec->outputsInverted : false;
}

bool IOModule::validateExpanderTopology_()
{
    bool topologyValid = true;
    bool expanderIdSeen[IO_MAX_EXPANDERS] = {false};

    for (uint8_t i = 0; i < IO_MAX_EXPANDERS; ++i) {
        runtimeExpanders_[i].configValid = false;
    }

    if (expanderCount_ > 0U && !expanders_) {
        LOGE("IO expander topology is missing");
        return false;
    }

    for (uint8_t i = 0; i < expanderCount_; ++i) {
        const IOExpanderSpec& spec = expanders_[i];
        if (spec.expanderId >= IO_MAX_EXPANDERS) {
            LOGE("Invalid IO expander id=%u", (unsigned)spec.expanderId);
            topologyValid = false;
            continue;
        }
        if (expanderIdSeen[spec.expanderId]) {
            LOGE("Duplicate IO expander id=%u", (unsigned)spec.expanderId);
            runtimeExpanders_[spec.expanderId].configValid = false;
            topologyValid = false;
            continue;
        }
        expanderIdSeen[spec.expanderId] = true;
        runtimeExpanders_[spec.expanderId].spec = &spec;
        runtimeExpanders_[spec.expanderId].configValid =
            spec.kind == IO_EXPANDER_KIND_PCF8574 ||
            spec.kind == IO_EXPANDER_KIND_TCA9554 ||
            spec.kind == IO_EXPANDER_KIND_MCP23017;
        if (!runtimeExpanders_[spec.expanderId].configValid) {
            LOGE("Invalid IO expander kind=%u for id=%u",
                 (unsigned)spec.kind,
                 (unsigned)spec.expanderId);
            topologyValid = false;
        }
    }

    for (uint8_t i = 0; i < bindingPortCount_; ++i) {
        const IOBindingPortSpec& port = bindingPorts_[i];
        if (port.portId == IO_PORT_INVALID) {
            LOGE("Invalid binding port at index=%u", (unsigned)i);
            topologyValid = false;
            continue;
        }

        for (uint8_t previous = 0; previous < i; ++previous) {
            const IOBindingPortSpec& other = bindingPorts_[previous];
            if (other.portId == port.portId) {
                LOGE("Duplicate binding port id=%u", (unsigned)port.portId);
                topologyValid = false;
            }
        }

        uint8_t requiredKind = IO_EXPANDER_KIND_NONE;
        uint8_t maxChannel = 0;
        if (port.kind == IO_PORT_KIND_PCF8574_OUTPUT) {
            requiredKind = IO_EXPANDER_KIND_PCF8574;
            maxChannel = 7U;
        } else if (port.kind == IO_PORT_KIND_TCA9554_OUTPUT) {
            requiredKind = IO_EXPANDER_KIND_TCA9554;
            maxChannel = 7U;
        } else if (port.kind == IO_PORT_KIND_MCP23017_INPUT ||
                   port.kind == IO_PORT_KIND_MCP23017_OUTPUT) {
            requiredKind = IO_EXPANDER_KIND_MCP23017;
            maxChannel = 15U;
        }
        if (requiredKind == IO_EXPANDER_KIND_NONE) continue;

        const IOExpanderSpec* expander = expanderSpec_(port.expanderId);
        if (!expander || expander->kind != requiredKind || port.channel > maxChannel) {
            LOGE("Invalid binding port=%u kind=%u channel=%u expander=%u",
                 (unsigned)port.portId,
                 (unsigned)port.kind,
                 (unsigned)port.channel,
                 (unsigned)port.expanderId);
            topologyValid = false;
            continue;
        }

        for (uint8_t previous = 0; previous < i; ++previous) {
            const IOBindingPortSpec& other = bindingPorts_[previous];
            if (other.expanderId == port.expanderId &&
                other.channel == port.channel &&
                (other.kind == IO_PORT_KIND_PCF8574_OUTPUT ||
                 other.kind == IO_PORT_KIND_TCA9554_OUTPUT ||
                 other.kind == IO_PORT_KIND_MCP23017_INPUT ||
                 other.kind == IO_PORT_KIND_MCP23017_OUTPUT)) {
                LOGE("Duplicate expander resource expander=%u channel=%u ports=%u/%u",
                     (unsigned)port.expanderId,
                     (unsigned)port.channel,
                     (unsigned)other.portId,
                     (unsigned)port.portId);
                topologyValid = false;
            }
        }
    }

    for (uint8_t i = 0; i < IO_MAX_EXPANDERS; ++i) {
        if (!runtimeExpanders_[i].configValid || !expanderEnabled_(i)) continue;
        const uint8_t address = expanderAddress_(i);
        if (address < 0x08U || address > 0x77U) {
            LOGE("Invalid I2C address expander=%u addr=0x%02X", (unsigned)i, address);
            runtimeExpanders_[i].configValid = false;
            continue;
        }
        for (uint8_t previous = 0; previous < i; ++previous) {
            const IOExpanderSpec* previousSpec = expanderSpec_(previous);
            if (!previousSpec || !expanderEnabled_(previous)) continue;
            if (expanderAddress_(previous) != address) continue;
            LOGE("I2C address collision expanders=%u/%u addr=0x%02X",
                 (unsigned)previous,
                 (unsigned)i,
                 address);
            runtimeExpanders_[previous].configValid = false;
            runtimeExpanders_[i].configValid = false;
        }
    }

    return topologyValid;
}

bool IOModule::resolveAnalogBinding_(PhysicalPortId portId, uint8_t& sourceOut, uint8_t& channelOut, uint8_t& backendOut) const
{
    const IOBindingPortSpec* spec = bindingPortSpec_(portId);
    if (!spec) return false;

    // `sourceOut` identifies the shared physical provider, while `channelOut` selects the logical measurement.
    switch (spec->kind) {
        case IO_PORT_KIND_ADS_INTERNAL_SINGLE:
            sourceOut = IO_SRC_ADS_INTERNAL_SINGLE;
            channelOut = spec->channel;
            backendOut = IO_BACKEND_ADS1115_INT;
            return true;
        case IO_PORT_KIND_ADS_EXTERNAL_DIFF:
            sourceOut = IO_SRC_ADS_EXTERNAL_DIFF;
            channelOut = spec->channel;
            backendOut = IO_BACKEND_ADS1115_EXT_DIFF;
            return true;
        case IO_PORT_KIND_DS18_WATER:
            sourceOut = IO_SRC_DS18_WATER;
            channelOut = spec->channel;
            backendOut = IO_BACKEND_DS18B20;
            return true;
        case IO_PORT_KIND_DS18_AIR:
            sourceOut = IO_SRC_DS18_AIR;
            channelOut = spec->channel;
            backendOut = IO_BACKEND_DS18B20;
            return true;
        case IO_PORT_KIND_SHT40:
            sourceOut = IO_SRC_SHT40;
            channelOut = spec->channel;
            backendOut = IO_BACKEND_SHT40;
            return channelOut <= 1U;
        case IO_PORT_KIND_BMP280:
            sourceOut = IO_SRC_BMP280;
            channelOut = spec->channel;
            backendOut = IO_BACKEND_BMP280;
            return channelOut <= 1U;
        case IO_PORT_KIND_BME680:
            sourceOut = IO_SRC_BME680;
            channelOut = spec->channel;
            backendOut = IO_BACKEND_BME680;
            return channelOut <= 3U;
        case IO_PORT_KIND_INA226:
            sourceOut = IO_SRC_INA226;
            channelOut = spec->channel;
            backendOut = IO_BACKEND_INA226;
            return channelOut <= 4U;
        default:
            return false;
    }
}

bool IOModule::resolveDigitalInputBinding_(PhysicalPortId portId,
                                           uint8_t& pinOut,
                                           uint8_t& backendOut,
                                           uint8_t& channelOut,
                                           IOExpanderId& expanderOut) const
{
    const IOBindingPortSpec* spec = bindingPortSpec_(portId);
    if (!spec) return false;

    if (spec->kind == IO_PORT_KIND_GPIO_INPUT) {
        pinOut = spec->channel;
        backendOut = IO_BACKEND_GPIO;
        channelOut = spec->channel;
        expanderOut = IO_EXPANDER_INVALID;
        return true;
    }
    if (spec->kind == IO_PORT_KIND_MCP23017_INPUT) {
        if (spec->channel > 15U) return false;
        pinOut = 0U;
        backendOut = IO_BACKEND_MCP23017;
        channelOut = spec->channel;
        expanderOut = spec->expanderId;
        return true;
    }
    return false;
}

bool IOModule::resolveDigitalOutputBinding_(PhysicalPortId portId,
                                            uint8_t& pinOut,
                                            uint8_t& backendOut,
                                            uint8_t& channelOut,
                                            IOExpanderId& expanderOut,
                                            bool& usesPcfOut,
                                            bool& usesTcaOut,
                                            bool& usesMcpOut) const
{
    const IOBindingPortSpec* spec = bindingPortSpec_(portId);
    if (!spec) return false;

    if (spec->kind == IO_PORT_KIND_GPIO_OUTPUT) {
        pinOut = spec->channel;
        backendOut = IO_BACKEND_GPIO;
        channelOut = spec->channel;
        expanderOut = IO_EXPANDER_INVALID;
        usesPcfOut = false;
        usesTcaOut = false;
        usesMcpOut = false;
        return true;
    }
    if (spec->kind == IO_PORT_KIND_PCF8574_OUTPUT) {
        pinOut = 0U;
        backendOut = IO_BACKEND_PCF8574;
        channelOut = spec->channel;
        expanderOut = spec->expanderId;
        usesPcfOut = true;
        usesTcaOut = false;
        usesMcpOut = false;
        return true;
    }
    if (spec->kind == IO_PORT_KIND_TCA9554_OUTPUT) {
        pinOut = 0U;
        backendOut = IO_BACKEND_TCA9554;
        channelOut = spec->channel;
        expanderOut = spec->expanderId;
        usesPcfOut = false;
        usesTcaOut = true;
        usesMcpOut = false;
        return true;
    }
    if (spec->kind == IO_PORT_KIND_MCP23017_OUTPUT) {
        if (spec->channel > 15U) return false;
        pinOut = 0U;
        backendOut = IO_BACKEND_MCP23017;
        channelOut = spec->channel;
        expanderOut = spec->expanderId;
        usesPcfOut = false;
        usesTcaOut = false;
        usesMcpOut = true;
        return true;
    }
    return false;
}

bool IOModule::resolveDsBusAddress_(OneWireBus* bus, const char* runtimeKey, uint8_t outAddr[8])
{
    if (!bus || !runtimeKey || !outAddr) return false;

    bus->begin();
    const uint8_t count = bus->deviceCount();

    size_t len = 0U;
    const bool readOk = cfgSvc_ && cfgSvc_->readRuntimeBlob
        ? cfgSvc_->readRuntimeBlob(cfgSvc_->ctx, runtimeKey, outAddr, 8U, &len)
        : (cfgStore_ && cfgStore_->readRuntimeBlob(runtimeKey, outAddr, 8U, &len));
    if (readOk && len == 8U) {
        char cached[24]{};
        formatDs18Address_(outAddr, cached, sizeof(cached));
        if (bus->hasAddress(outAddr)) {
            LOGI("DS18B20 resolved from cache key=%s GPIO=%d count=%u rom=%s",
                 runtimeKey,
                 bus->pin(),
                 (unsigned)count,
                 cached);
            return true;
        }
        LOGW("Cached DS18B20 address for %s not found on current bus GPIO=%d count=%u rom=%s; rescanning",
             runtimeKey,
             bus->pin(),
             (unsigned)count,
             cached);
    }

    if (count != 1U) {
        LOGW("DS18B20 scan unresolved key=%s GPIO=%d count=%u expected=1",
             runtimeKey,
             bus->pin(),
             (unsigned)count);
        for (uint8_t i = 0; i < count; ++i) {
            uint8_t found[8]{};
            if (!bus->getAddress(i, found)) continue;
            char rom[24]{};
            formatDs18Address_(found, rom, sizeof(rom));
            LOGW("DS18B20 scan key=%s GPIO=%d index=%u rom=%s",
                 runtimeKey,
                 bus->pin(),
                 (unsigned)i,
                 rom);
        }
        return false;
    }
    if (!bus->getAddress(0, outAddr)) {
        LOGW("DS18B20 scan failed to read address key=%s GPIO=%d count=%u",
             runtimeKey,
             bus->pin(),
             (unsigned)count);
        return false;
    }

    char resolved[24]{};
    formatDs18Address_(outAddr, resolved, sizeof(resolved));
    LOGI("DS18B20 resolved by scan key=%s GPIO=%d rom=%s", runtimeKey, bus->pin(), resolved);

    if (cfgSvc_ && cfgSvc_->writeRuntimeBlobAsync) {
        (void)cfgSvc_->writeRuntimeBlobAsync(cfgSvc_->ctx, runtimeKey, outAddr, 8U);
    }
    return true;
}

bool IOModule::persistCounterTotalIfNeeded_(DigitalSlot& slot, int32_t rawCount, uint32_t nowMs)
{
    static constexpr int32_t kCounterPersistPulseDelta = 32;
    static constexpr uint32_t kCounterPersistPeriodMs = 180000U;

    if (slot.kind != DIGITAL_SLOT_INPUT || slot.inDef.mode != IO_DIGITAL_INPUT_COUNTER) return false;
    if (slot.counterScaledTotal == slot.counterLastPersistedTotal) return false;

    bool shouldPersist = false;
    if (rawCount >= slot.counterLastFlushedRawCount &&
        (rawCount - slot.counterLastFlushedRawCount) >= kCounterPersistPulseDelta) {
        shouldPersist = true;
    }
    if (!shouldPersist &&
        slot.counterLastPersistMs != 0U &&
        (uint32_t)(nowMs - slot.counterLastPersistMs) >= kCounterPersistPeriodMs) {
        shouldPersist = true;
    }
    if (!shouldPersist) return false;

    ConfigVariable<float,0>* totalVar = counterTotalVar_(slot.logicalIdx);
    if (!totalVar) return false;

    if (cfgSvc_ && cfgSvc_->persistFloatAsync) {
        if (!totalVar->value || !totalVar->nvsKey) return false;
        if (!cfgSvc_->persistFloatAsync(cfgSvc_->ctx,
                                        totalVar->nvsKey,
                                        slot.counterScaledTotal,
                                        totalVar->moduleName,
                                        totalVar->moduleId,
                                        totalVar->localBranchId)) {
            return false;
        }
        *(totalVar->value) = slot.counterScaledTotal;
        totalVar->notify();
    } else {
        return false;
    }
    if (float* lastConfigTotal = counterConfigTotalState_(slot.logicalIdx)) {
        *lastConfigTotal = slot.counterScaledTotal;
    }
    slot.counterLastPersistedTotal = slot.counterScaledTotal;
    slot.counterLastFlushedRawCount = rawCount;
    slot.counterLastPersistMs = nowMs;
    return true;
}

bool IOModule::configureRuntime_()
{
    if (runtimeReady_) return true;
    if (!cfgData_.enabled) return false;
    if (!validateExpanderTopology_()) {
        LOGE("I/O topology validation failed");
        return false;
    }

    bool needAnalogSource[IO_SRC_COUNT] = {false};

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogSlots_[i].used) continue;
        analogSlots_[i].ioId = (IoId)(IO_ID_AI_BASE + i);
        analogSlots_[i].source = IO_ANALOG_SOURCE_INVALID;
        analogSlots_[i].channel = 0U;
        analogSlots_[i].backend = IO_BACKEND_GPIO;
        analogSlots_[i].lastSampleSeqValid = false;
        analogSlots_[i].lastSampleSeq = 0;
        analogSlots_[i].lastRoundedValid = false;
        analogSlots_[i].lastRounded = 0.0f;

        if (i < ANALOG_CFG_SLOTS) {
            snprintf(analogSlots_[i].def.id, sizeof(analogSlots_[i].def.id), "a%02u", (unsigned)i);
            analogSlots_[i].def.bindingPort = analogCfg_[i].bindingPort;
            analogSlots_[i].def.c0 = analogCfg_[i].c0;
            analogSlots_[i].def.c1 = analogCfg_[i].c1;
            analogSlots_[i].def.precision = analogCfg_[i].precision;

            uint8_t source = IO_ANALOG_SOURCE_INVALID;
            uint8_t channel = 0U;
            uint8_t backend = IO_BACKEND_GPIO;
            if (resolveAnalogBinding_(analogSlots_[i].def.bindingPort, source, channel, backend)) {
                analogSlots_[i].source = source;
                analogSlots_[i].channel = channel;
                analogSlots_[i].backend = backend;
            } else if (analogSlots_[i].def.bindingPort != IO_PORT_INVALID) {
                LOGW("Analog %s unresolved binding_port=%u",
                     analogSlots_[i].def.id,
                     (unsigned)analogSlots_[i].def.bindingPort);
            }

            if (i < 3 && analogSlots_[i].source != IO_ANALOG_SOURCE_INVALID) {
                LOGI("Analog map %s binding_port=%u source=%u channel=%u",
                     analogSlots_[i].def.id,
                     (unsigned)analogSlots_[i].def.bindingPort,
                     (unsigned)analogSlots_[i].source,
                     (unsigned)analogSlots_[i].channel);
            }
        }

        if (analogSlots_[i].source < IO_SRC_COUNT) {
            needAnalogSource[analogSlots_[i].source] = true;
        } else {
            continue;
        }

        analogSlots_[i].endpoint = allocAnalogEndpoint_(analogSlots_[i].def.id);
        if (!analogSlots_[i].endpoint) continue;
        if (!registry_.add(analogSlots_[i].endpoint)) {
            LOGE("I/O registry full while adding analog endpoint id=%s count=%u capacity=%u",
                 analogSlots_[i].def.id,
                 (unsigned)registry_.count(),
                 (unsigned)IO_REGISTRY_MAX_ENDPOINTS);
            return false;
        }
    }

    bool needPcfOutput = false;
    bool needTcaOutput = false;
    bool needMcpOutput = false;
    bool needMcpInput = false;
    bool tcaPreserveStartup[IO_MAX_EXPANDERS] = {false};
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;

        PhysicalPortId bindingPort = IO_PORT_INVALID;
        IOOutputStartupPolicy startupPolicy = IOOutputStartupPolicy::ApplyInitial;
        if (s.kind == DIGITAL_SLOT_INPUT) {
            bindingPort = s.inDef.bindingPort;
            if (s.logicalIdx < DIGITAL_INPUT_CFG_SLOTS) {
                bindingPort = digitalInCfg_[s.logicalIdx].bindingPort;
            }
        } else if (s.kind == DIGITAL_SLOT_OUTPUT) {
            bindingPort = s.outDef.bindingPort;
            startupPolicy = s.outDef.startupPolicy;
            if (s.logicalIdx < DIGITAL_CFG_SLOTS) {
                bindingPort = digitalCfg_[s.logicalIdx].bindingPort;
                startupPolicy = digitalCfg_[s.logicalIdx].startupPolicy;
            }
        }

        const IOBindingPortSpec* spec = bindingPortSpec_(bindingPort);
        if (spec && spec->kind == IO_PORT_KIND_MCP23017_INPUT) {
            needMcpInput = true;
        }
        if (s.kind != DIGITAL_SLOT_OUTPUT) continue;
        if (spec && spec->kind == IO_PORT_KIND_PCF8574_OUTPUT) {
            needPcfOutput = true;
        }
        if (spec && spec->kind == IO_PORT_KIND_TCA9554_OUTPUT) {
            needTcaOutput = true;
            if (startupPolicy == IOOutputStartupPolicy::PreserveHardwareState &&
                spec->expanderId < IO_MAX_EXPANDERS) {
                tcaPreserveStartup[spec->expanderId] = true;
            }
        }
        if (spec && spec->kind == IO_PORT_KIND_MCP23017_OUTPUT) {
            needMcpOutput = true;
        }
    }

    const bool needI2c =
        needAnalogSource[IO_SRC_ADS_INTERNAL_SINGLE] ||
        needAnalogSource[IO_SRC_ADS_EXTERNAL_DIFF] ||
        needAnalogSource[IO_SRC_SHT40] ||
        needAnalogSource[IO_SRC_BMP280] ||
        needAnalogSource[IO_SRC_BME680] ||
        needAnalogSource[IO_SRC_INA226] ||
        cfgData_.sht40Enabled ||
        cfgData_.bmp280Enabled ||
        cfgData_.bme680Enabled ||
        cfgData_.ina226Enabled ||
        needPcfOutput ||
        needTcaOutput ||
        needMcpOutput ||
        needMcpInput;

    if (needI2c) {
        if (!i2cBus_) {
            LOGE("shared I2C bus service unavailable");
            return false;
        }
        // Concrete bus/driver assembly is centralized here so the rest of the module can stay on kernel types.
        i2cBus_->begin(cfgData_.i2cSda, cfgData_.i2cScl);
        if (!i2cBus_->beginOk()) {
            LOGW("i2c.begin failed sda=%d scl=%d freq=%lu",
                 i2cBus_->beginSda(),
                 i2cBus_->beginScl(),
                 (unsigned long)i2cBus_->beginFrequencyHz());
        }
        const bool ads48Present = i2cBus_->probe(0x48);
        const bool ads49Present = i2cBus_->probe(0x49);
        LOGI("ADS1115 probe 0x48: %s", ads48Present ? "found" : "not found");
        LOGI("ADS1115 probe 0x49: %s", ads49Present ? "found" : "not found");
    }

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        if (!digitalSlots_[i].used) continue;
        DigitalSlot& s = digitalSlots_[i];
        s.owner = this;
        s.ioId = (s.kind == DIGITAL_SLOT_OUTPUT)
                   ? (IoId)(IO_ID_DO_BASE + s.logicalIdx)
                   : (IoId)(IO_ID_DI_BASE + s.logicalIdx);

        if (s.kind == DIGITAL_SLOT_INPUT) {
            const uint8_t cfgIdx = s.logicalIdx;
            if (cfgIdx < MAX_DIGITAL_INPUTS) {
                if (digitalInCfg_[cfgIdx].name[0] != '\0') {
                    strncpy(s.inDef.id, digitalInCfg_[cfgIdx].name, sizeof(s.inDef.id) - 1);
                    s.inDef.id[sizeof(s.inDef.id) - 1] = '\0';
                }
                s.inDef.bindingPort = digitalInCfg_[cfgIdx].bindingPort;
                s.inDef.activeHigh = digitalInCfg_[cfgIdx].activeHigh;
                uint8_t pull = digitalInCfg_[cfgIdx].pullMode;
                if (pull > IO_PULL_DOWN) pull = IO_PULL_NONE;
                s.inDef.pullMode = pull;
                s.inDef.mode = digitalInCfg_[cfgIdx].mode;
                s.inDef.edgeMode = digitalInCfg_[cfgIdx].edgeMode;
                s.inDef.counterDebounceUs = counterDebounceUsFromConfigLocal(digitalInCfg_[cfgIdx].counterDebounceUs);
            }

            snprintf(s.endpointId, sizeof(s.endpointId), "i%02u", (unsigned)s.logicalIdx);
            uint8_t pin = 0U;
            uint8_t backend = IO_BACKEND_GPIO;
            uint8_t channel = 0U;
            IOExpanderId expanderId = IO_EXPANDER_INVALID;
            if (!resolveDigitalInputBinding_(s.inDef.bindingPort, pin, backend, channel, expanderId)) {
                if (s.inDef.bindingPort != IO_PORT_INVALID) {
                    LOGW("Digital input %s unresolved binding_port=%u",
                         s.endpointId,
                         (unsigned)s.inDef.bindingPort);
                }
                continue;
            }
            s.backend = backend;
            s.channel = channel;
            s.expanderId = expanderId;
            if (backend == IO_BACKEND_MCP23017) {
                if (s.inDef.pullMode == IO_PULL_DOWN) {
                    LOGW("Digital input %s MCP23017 pull-down unsupported; using pull-none", s.endpointId);
                    s.inDef.pullMode = IO_PULL_NONE;
                }
                if (s.inDef.mode == IO_DIGITAL_INPUT_COUNTER) {
                    LOGW("Digital input %s MCP23017 counter mode unsupported; using state mode", s.endpointId);
                    s.inDef.mode = IO_DIGITAL_INPUT_STATE;
                }
            }

            IDigitalPinDriver* driver = nullptr;
            IDigitalCounterDriver* counterDriver = nullptr;
            if (backend == IO_BACKEND_MCP23017) {
                Mcp23017Driver* mcp = beginMcpExpander_(s.expanderId);
                if (!mcp) {
                    LOGW("Digital input %s requires MCP23017 expander=%u but it is unavailable",
                         s.endpointId,
                         (unsigned)s.expanderId);
                    continue;
                }
                driver = allocMcpBitDriver_(s.endpointId, mcp, channel, s.inDef.activeHigh, false, s.inDef.pullMode);
            } else {
                counterDriver = allocGpioDriver_(
                    s.endpointId,
                    pin,
                    false,
                    s.inDef.activeHigh,
                    s.inDef.pullMode,
                    s.inDef.mode == IO_DIGITAL_INPUT_COUNTER,
                    s.inDef.edgeMode,
                    s.inDef.counterDebounceUs
                );
                driver = counterDriver;
            }
            if (!driver) {
                LOGW("Digital input %s driver alloc failed pin=%u binding_port=%u mode=%u debounce_us=%lu",
                     s.endpointId,
                     (unsigned)pin,
                     (unsigned)s.inDef.bindingPort,
                     (unsigned)s.inDef.mode,
                     (unsigned long)s.inDef.counterDebounceUs);
                continue;
            }

            s.provider = makeDigitalProvider(driver);
            if (!s.provider.begin()) {
                LOGW("Digital input %s driver begin failed id=%s pin=%u binding_port=%u mode=%u debounce_us=%lu",
                     s.endpointId,
                     driver->id() ? driver->id() : "?",
                     (unsigned)pin,
                     (unsigned)s.inDef.bindingPort,
                     (unsigned)s.inDef.mode,
                     (unsigned long)s.inDef.counterDebounceUs);
                continue;
            }

            const uint8_t valueType = (s.inDef.mode == IO_DIGITAL_INPUT_COUNTER) ? IO_EP_VALUE_FLOAT : IO_EP_VALUE_BOOL;
            s.endpoint = allocDigitalSensorEndpoint_(s.endpointId, valueType);
            if (!s.endpoint) continue;
            if (s.inDef.mode == IO_DIGITAL_INPUT_COUNTER) {
                eraseLegacyCounterPersistedTotal_(s.logicalIdx);
                int32_t initialRawCount = 0;
                if (counterDriver) {
                    (void)counterDriver->readCount(initialRawCount);
                }

                const IODigitalInputSlotConfig* cfg = (s.logicalIdx < MAX_DIGITAL_INPUTS) ? &digitalInCfg_[s.logicalIdx] : nullptr;
                const float c0 = cfg ? cfg->c0 : 1.0f;
                const int32_t precision = sanitizeAnalogPrecision_(cfg ? cfg->precision : 0);
                const float configTotal = cfg ? cfg->counterTotal : 0.0f;

                if (float* lastConfigTotal = counterConfigTotalState_(s.logicalIdx)) {
                    *lastConfigTotal = configTotal;
                }
                s.counterScaledTotal = configTotal;
                s.counterScaledTotal += ((float)initialRawCount * c0);
                s.counterLastPersistedTotal = configTotal;
                s.counterLastRawCount = initialRawCount;
                s.counterLastFlushedRawCount = initialRawCount;
                s.counterLastPersistMs = millis();
                s.lastValid = false;
                const float scaledValue = ioRoundToPrecision(s.counterScaledTotal, precision);
                static_cast<DigitalSensorEndpoint*>(s.endpoint)->updateFloat(scaledValue, true, millis());
            }
            if (!registry_.add(s.endpoint)) {
                LOGE("I/O registry full while adding digital input id=%s count=%u capacity=%u",
                     s.endpointId,
                     (unsigned)registry_.count(),
                     (unsigned)IO_REGISTRY_MAX_ENDPOINTS);
                return false;
            }
            (void)processDigitalInputDefinition_(i, millis());
            continue;
        }

        const uint8_t cfgIdx = s.logicalIdx;
        if (cfgIdx < DIGITAL_CFG_SLOTS) {
            snprintf(s.outDef.id, sizeof(s.outDef.id), "d%02u", (unsigned)cfgIdx);
            s.outDef.bindingPort = digitalCfg_[cfgIdx].bindingPort;
            s.outDef.activeHigh = digitalCfg_[cfgIdx].activeHigh;
            s.outDef.initialOn = digitalCfg_[cfgIdx].initialOn;
            s.outDef.startupPolicy = digitalCfg_[cfgIdx].startupPolicy;
            s.outDef.retainOnWarmReboot = digitalCfg_[cfgIdx].retainOnWarmReboot;
            s.outDef.momentary = digitalCfg_[cfgIdx].momentary;
            int32_t p = digitalCfg_[cfgIdx].pulseMs;
            if (p <= 0) p = 500;
            if (p > 60000) p = 60000;
            s.outDef.pulseMs = (uint16_t)p;
        } else {
            snprintf(s.outDef.id, sizeof(s.outDef.id), "d%02u", (unsigned)s.logicalIdx);
        }

        strncpy(s.endpointId, s.outDef.id, sizeof(s.endpointId) - 1);
        s.endpointId[sizeof(s.endpointId) - 1] = '\0';

        uint8_t pin = 0U;
        uint8_t backend = IO_BACKEND_GPIO;
        uint8_t channel = 0U;
        IOExpanderId expanderId = IO_EXPANDER_INVALID;
        bool usesPcfOut = false;
        bool usesTcaOut = false;
        bool usesMcpOut = false;
        bool bindingAlreadyUsed = false;
        if (s.outDef.bindingPort != IO_PORT_INVALID) {
            for (uint8_t previous = 0; previous < i; ++previous) {
                const DigitalSlot& other = digitalSlots_[previous];
                if (!other.used || other.kind != DIGITAL_SLOT_OUTPUT) continue;
                PhysicalPortId otherBinding = other.outDef.bindingPort;
                if (other.logicalIdx < DIGITAL_CFG_SLOTS) {
                    otherBinding = digitalCfg_[other.logicalIdx].bindingPort;
                }
                if (otherBinding == s.outDef.bindingPort) {
                    bindingAlreadyUsed = true;
                    LOGE("Digital outputs %s/%s share binding_port=%u; %s disabled",
                         other.endpointId[0] ? other.endpointId : other.outDef.id,
                         s.outDef.id,
                         (unsigned)s.outDef.bindingPort,
                         s.outDef.id);
                    break;
                }
            }
        }
        if (bindingAlreadyUsed) continue;

        if (!resolveDigitalOutputBinding_(s.outDef.bindingPort, pin, backend, channel, expanderId, usesPcfOut, usesTcaOut, usesMcpOut)) {
            if (s.outDef.bindingPort != IO_PORT_INVALID) {
                LOGW("Digital output %s unresolved binding_port=%u",
                     s.endpointId,
                     (unsigned)s.outDef.bindingPort);
            }
            continue;
        }
        s.backend = backend;
        s.channel = channel;
        s.expanderId = expanderId;
        if (s.outDef.retainOnWarmReboot && !usesTcaOut) {
            LOGW("Digital output %s retain_on_warm_reboot ignored: backend is not TCA9554", s.endpointId);
        }

        IDigitalPinDriver* driver = nullptr;
        const bool effectiveActiveHigh = (usesPcfOut || usesTcaOut || usesMcpOut)
            ? (s.outDef.activeHigh != expanderOutputsInverted_(s.expanderId))
            : s.outDef.activeHigh;
        if (usesPcfOut) {
            IMaskOutputDriver* pcf = beginMaskExpander_(s.expanderId, IO_EXPANDER_KIND_PCF8574, false);
            if (!pcf) {
                LOGW("Digital output %s requires PCF8574 expander=%u but it is unavailable",
                     s.endpointId,
                     (unsigned)s.expanderId);
                continue;
            }
            driver = allocPcfBitDriver_(s.outDef.id, static_cast<Pcf8574Driver*>(pcf), channel, effectiveActiveHigh);
        } else if (usesTcaOut) {
            IMaskOutputDriver* tca = beginMaskExpander_(s.expanderId,
                                                        IO_EXPANDER_KIND_TCA9554,
                                                        s.expanderId < IO_MAX_EXPANDERS && tcaPreserveStartup[s.expanderId]);
            if (!tca) {
                LOGW("Digital output %s requires TCA9554 expander=%u but it is unavailable",
                     s.endpointId,
                     (unsigned)s.expanderId);
                continue;
            }
            driver = allocTcaBitDriver_(s.outDef.id, static_cast<Tca9554Driver*>(tca), channel, effectiveActiveHigh);
        } else if (usesMcpOut) {
            Mcp23017Driver* mcp = beginMcpExpander_(s.expanderId);
            if (!mcp) {
                LOGW("Digital output %s requires MCP23017 expander=%u but it is unavailable",
                     s.endpointId,
                     (unsigned)s.expanderId);
                continue;
            }
            driver = allocMcpBitDriver_(s.outDef.id, mcp, channel, effectiveActiveHigh, true);
        } else {
            driver = allocGpioDriver_(s.outDef.id, pin, true, effectiveActiveHigh);
        }
        if (!driver) continue;

        s.provider = makeDigitalProvider(driver);
        if (!s.provider.begin()) continue;
        s.pulseArmed = false;
        s.pulseDeadlineMs = 0;

        s.endpoint = static_cast<IOEndpoint*>(allocDigitalActuatorEndpoint_(
            s.outDef.id,
            &IOModule::writeDigitalOut_,
            &s
        ));
        if (!s.endpoint) continue;
        if (!registry_.add(s.endpoint)) {
            LOGE("I/O registry full while adding digital output id=%s count=%u capacity=%u",
                 s.endpointId,
                 (unsigned)registry_.count(),
                 (unsigned)IO_REGISTRY_MAX_ENDPOINTS);
            return false;
        }

        bool actualOn = s.outDef.initialOn;
        const bool preserveStartup =
            s.outDef.startupPolicy == IOOutputStartupPolicy::PreserveHardwareState;
        if (preserveStartup) {
            if (!s.provider.read(actualOn)) {
                LOGW("Digital output %s startup state adoption failed", s.endpointId);
            }
        } else {
            (void)s.provider.write(s.outDef.initialOn);
            actualOn = s.outDef.initialOn;
        }

        const uint32_t nowMs = millis();
        static_cast<DigitalActuatorEndpoint*>(s.endpoint)->adoptValue(actualOn, nowMs);
        if (dataStore_) {
            uint8_t rtIdx = 0;
            if (endpointIndexFromId_(s.endpointId, rtIdx)) {
                (void)setIoEndpointBool(*dataStore_, rtIdx, actualOn, nowMs);
            }
        }
        markIoCycleChanged_(s.ioId);
    }

    Ads1115DriverConfig adsInternalCfg{};
    adsInternalCfg.address = cfgData_.adsInternalAddr;
    adsInternalCfg.gain = (uint8_t)cfgData_.adsGain;
    adsInternalCfg.dataRate = (uint8_t)cfgData_.adsRate;
    adsInternalCfg.pollMs = (cfgData_.adsPollMs < 20) ? 20 : (uint32_t)cfgData_.adsPollMs;
    adsInternalCfg.differentialPairs = false;

    Ads1115DriverConfig adsExternalCfg = adsInternalCfg;
    adsExternalCfg.address = cfgData_.adsExternalAddr;
    adsExternalCfg.differentialPairs = true;

    if (needAnalogSource[IO_SRC_SHT40] || cfgData_.sht40Enabled) {
        const bool present = i2cBus_->probe(cfgData_.sht40Address);
        LOGI("SHT40 probe 0x%02X: %s", cfgData_.sht40Address, present ? "found" : "not found");
    }

    if (needAnalogSource[IO_SRC_BMP280] || cfgData_.bmp280Enabled) {
        const bool present = i2cBus_->probe(cfgData_.bmp280Address);
        LOGI("BMP280 probe 0x%02X: %s", cfgData_.bmp280Address, present ? "found" : "not found");
    }

    if (needAnalogSource[IO_SRC_BME680] || cfgData_.bme680Enabled) {
        const bool present = i2cBus_->probe(cfgData_.bme680Address);
        LOGI("BME680 probe 0x%02X: %s", cfgData_.bme680Address, present ? "found" : "not found");
    }

    if (needAnalogSource[IO_SRC_INA226] || cfgData_.ina226Enabled) {
        const bool present = i2cBus_->probe(cfgData_.ina226Address);
        LOGI("INA226 probe 0x%02X: %s", cfgData_.ina226Address, present ? "found" : "not found");
    }

    if (needPcfOutput || needTcaOutput || needMcpInput || needMcpOutput) {
        for (uint8_t expIdx = 0; expIdx < IO_MAX_EXPANDERS; ++expIdx) {
            const IOExpanderSpec* spec = expanderSpec_(expIdx);
            if (!spec || !expanderUsable_(spec->expanderId)) continue;
            const uint8_t address = expanderAddress_(spec->expanderId);
            const bool present = i2cBus_->probe(address);
            const char* name = (spec->kind == IO_EXPANDER_KIND_TCA9554) ? "TCA9554" :
                               (spec->kind == IO_EXPANDER_KIND_PCF8574) ? "PCF8574" :
                               (spec->kind == IO_EXPANDER_KIND_MCP23017) ? "MCP23017" : "expander";
            LOGI("%s probe expander=%u addr=0x%02X: %s",
                 name,
                 (unsigned)spec->expanderId,
                 address,
                 present ? "found" : "not found");
        }
    }

    if (needAnalogSource[IO_SRC_ADS_INTERNAL_SINGLE]) {
        IAnalogSourceDriver* driver = allocAdsDriver_("ads_internal", i2cBus_, adsInternalCfg);
        if (!driver) {
            LOGW("ADS internal pool exhausted");
        } else
        if (!makeAnalogProvider(driver).begin()) {
            LOGW("ADS internal not detected at 0x%02X", cfgData_.adsInternalAddr);
        } else {
            analogProviders_[IO_SRC_ADS_INTERNAL_SINGLE] = makeAnalogProvider(driver);
            if (cfgData_.adsInternalAddr == 0x49) {
                LOGI("ADS1115 found at 0x49 (internal)");
            }
        }
    }

    if (needAnalogSource[IO_SRC_ADS_EXTERNAL_DIFF]) {
        IAnalogSourceDriver* driver = allocAdsDriver_("ads_external", i2cBus_, adsExternalCfg);
        if (!driver) {
            LOGW("ADS external pool exhausted");
        } else
        if (!makeAnalogProvider(driver).begin()) {
            LOGW("ADS external not detected at 0x%02X", cfgData_.adsExternalAddr);
        } else {
            analogProviders_[IO_SRC_ADS_EXTERNAL_DIFF] = makeAnalogProvider(driver);
            if (cfgData_.adsExternalAddr == 0x49) {
                LOGI("ADS1115 found at 0x49 (external)");
            }
        }
    }

    Ds18b20DriverConfig dsCfg{};
    dsCfg.pollMs = (cfgData_.dsPollMs < 750) ? 750 : (uint32_t)cfgData_.dsPollMs;
    dsCfg.conversionWaitMs = 750;

    if (needAnalogSource[IO_SRC_DS18_WATER] && oneWireWater_) {
        oneWireWaterAddrValid_ = resolveDsBusAddress_(oneWireWater_, NvsKeys::Io::DsRomWater, oneWireWaterAddr_);
        if (oneWireWaterAddrValid_) {
            IAnalogSourceDriver* driver = allocDsDriver_("ds18_water", oneWireWater_, oneWireWaterAddr_, dsCfg);
            if (driver) {
                analogProviders_[IO_SRC_DS18_WATER] = makeAnalogProvider(driver);
                (void)analogProviders_[IO_SRC_DS18_WATER].begin();
            } else {
                LOGW("DS18 water pool exhausted");
            }
        } else {
            LOGW("No resolvable DS18B20 found on water OneWire bus GPIO=%d", oneWireWater_->pin());
        }
    }

    if (needAnalogSource[IO_SRC_DS18_AIR] && oneWireAir_) {
        oneWireAirAddrValid_ = resolveDsBusAddress_(oneWireAir_, NvsKeys::Io::DsRomAir, oneWireAirAddr_);
        if (oneWireAirAddrValid_) {
            IAnalogSourceDriver* driver = allocDsDriver_("ds18_air", oneWireAir_, oneWireAirAddr_, dsCfg);
            if (driver) {
                analogProviders_[IO_SRC_DS18_AIR] = makeAnalogProvider(driver);
                (void)analogProviders_[IO_SRC_DS18_AIR].begin();
            } else {
                LOGW("DS18 air pool exhausted");
            }
        } else {
            LOGW("No resolvable DS18B20 found on air OneWire bus GPIO=%d", oneWireAir_->pin());
        }
    }

    if (needAnalogSource[IO_SRC_SHT40]) {
        if (!cfgData_.sht40Enabled) {
            LOGW("SHT40 required by analog slots but disabled");
        } else {
            Sht40DriverConfig shtCfg{};
            shtCfg.address = cfgData_.sht40Address;
            shtCfg.pollMs = (cfgData_.sht40PollMs < 250) ? 250U : (uint32_t)cfgData_.sht40PollMs;

            IAnalogSourceDriver* driver = allocSht40Driver_("sht40", i2cBus_, shtCfg);
            if (!driver) {
                LOGW("SHT40 pool exhausted");
            } else {
                IOAnalogProvider provider = makeAnalogProvider(driver);
                if (provider.begin()) {
                    analogProviders_[IO_SRC_SHT40] = provider;
                }
            }
        }
    }

    if (needAnalogSource[IO_SRC_BMP280]) {
        if (!cfgData_.bmp280Enabled) {
            LOGW("BMP280 required by analog slots but disabled");
        } else {
            Bmp280DriverConfig bmpCfg{};
            bmpCfg.address = cfgData_.bmp280Address;
            bmpCfg.pollMs = (cfgData_.bmp280PollMs < 100) ? 100U : (uint32_t)cfgData_.bmp280PollMs;

            IAnalogSourceDriver* driver = allocBmp280Driver_("bmp280", i2cBus_, bmpCfg);
            if (!driver) {
                LOGW("BMP280 pool exhausted");
            } else {
                IOAnalogProvider provider = makeAnalogProvider(driver);
                if (provider.begin()) {
                    analogProviders_[IO_SRC_BMP280] = provider;
                }
            }
        }
    }

    if (needAnalogSource[IO_SRC_BME680]) {
        if (!cfgData_.bme680Enabled) {
            LOGW("BME680 required by analog slots but disabled");
        } else {
            Bme680DriverConfig bmeCfg{};
            bmeCfg.address = cfgData_.bme680Address;
            bmeCfg.pollMs = (cfgData_.bme680PollMs < 250) ? 250U : (uint32_t)cfgData_.bme680PollMs;

            IAnalogSourceDriver* driver = allocBme680Driver_("bme680", i2cBus_, bmeCfg);
            if (!driver) {
                LOGW("BME680 pool exhausted");
            } else {
                IOAnalogProvider provider = makeAnalogProvider(driver);
                if (provider.begin()) {
                    analogProviders_[IO_SRC_BME680] = provider;
                }
            }
        }
    }

    if (needAnalogSource[IO_SRC_INA226]) {
        if (!cfgData_.ina226Enabled) {
            LOGW("INA226 required by analog slots but disabled");
        } else {
            Ina226DriverConfig inaCfg{};
            inaCfg.address = cfgData_.ina226Address;
            inaCfg.pollMs = (cfgData_.ina226PollMs < 100) ? 100U : (uint32_t)cfgData_.ina226PollMs;
            inaCfg.shuntOhms = (cfgData_.ina226ShuntOhms > 0.0f) ? cfgData_.ina226ShuntOhms : 0.1f;

            IAnalogSourceDriver* driver = allocIna226Driver_("ina226", i2cBus_, inaCfg);
            if (!driver) {
                LOGW("INA226 pool exhausted");
            } else {
                IOAnalogProvider provider = makeAnalogProvider(driver);
                if (provider.begin()) {
                    analogProviders_[IO_SRC_INA226] = provider;
                }
            }
        }
    }

    IOScheduledJob adsJob{};
    adsJob.id = "ads_fast";
    adsJob.periodMs = (cfgData_.adsPollMs < 20) ? 20 : (uint32_t)cfgData_.adsPollMs;
    adsJob.fn = &IOModule::tickFastAds_;
    adsJob.ctx = this;
    scheduler_.add(adsJob);

    IOScheduledJob dsJob{};
    dsJob.id = "ds_slow";
    dsJob.periodMs = (cfgData_.dsPollMs < 250) ? 250 : (uint32_t)cfgData_.dsPollMs;
    dsJob.fn = &IOModule::tickSlowDs_;
    dsJob.ctx = this;
    scheduler_.add(dsJob);

    const bool needI2cAnalogJob = needAnalogSource[IO_SRC_SHT40]
        || needAnalogSource[IO_SRC_BMP280]
        || needAnalogSource[IO_SRC_BME680]
        || needAnalogSource[IO_SRC_INA226];
    IOScheduledJob i2cAnalogJob{};
    if (needI2cAnalogJob) {
        i2cAnalogJob.id = "i2c_analog";
        i2cAnalogJob.periodMs = 20U;
        i2cAnalogJob.fn = &IOModule::tickI2cAnalogs_;
        i2cAnalogJob.ctx = this;
        scheduler_.add(i2cAnalogJob);
    }

    IOScheduledJob dinJob{};
    dinJob.id = "din_poll";
    dinJob.periodMs = (cfgData_.digitalPollMs < 20) ? 20 : (uint32_t)cfgData_.digitalPollMs;
    dinJob.fn = &IOModule::tickDigitalInputs_;
    dinJob.ctx = this;
    scheduler_.add(dinJob);

    runtimeReady_ = true;
    const char* expanderState = "off";
    if (needTcaOutput) expanderState = "tca9554";
    else if (needPcfOutput) expanderState = "pcf8574";
    else if (needMcpOutput || needMcpInput) expanderState = "mcp23017";

    LOGI("I/O ready (ads=%ldms ds=%ldms i2c_ai=%s din=%ldms endpoints=%u expander=%s)",
         (long)adsJob.periodMs,
         (long)dsJob.periodMs,
         needI2cAnalogJob ? "20ms" : "off",
         (long)dinJob.periodMs,
         (unsigned)registry_.count(),
         expanderState);

    return true;
}

void IOModule::pollPulseOutputs_(uint32_t nowMs)
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& s = digitalSlots_[i];
        if (!s.used || s.kind != DIGITAL_SLOT_OUTPUT) continue;
        if (!s.outDef.momentary || !s.pulseArmed || !s.provider.isBound()) continue;
        if ((int32_t)(nowMs - s.pulseDeadlineMs) < 0) continue;
        (void)s.provider.write(false);
        s.pulseArmed = false;
    }
}

AnalogSensorEndpoint* IOModule::allocAnalogEndpoint_(const char* endpointId)
{
    if (!analogEndpointPool_) {
        analogEndpointPool_ = static_cast<AnalogSensorEndpoint*>(
            heap_caps_malloc(sizeof(AnalogSensorEndpoint) * MAX_ANALOG_ENDPOINTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
        if (!analogEndpointPool_) {
            analogEndpointPool_ = static_cast<AnalogSensorEndpoint*>(
                heap_caps_malloc(sizeof(AnalogSensorEndpoint) * MAX_ANALOG_ENDPOINTS, MALLOC_CAP_8BIT)
            );
        }
        if (!analogEndpointPool_) return nullptr;
    }
    if (analogEndpointPoolUsed_ >= MAX_ANALOG_ENDPOINTS) return nullptr;
    void* mem = &analogEndpointPool_[analogEndpointPoolUsed_++];
    return new (mem) AnalogSensorEndpoint(endpointId);
}

DigitalSensorEndpoint* IOModule::allocDigitalSensorEndpoint_(const char* endpointId, uint8_t valueType)
{
    if (digitalSensorEndpointPoolUsed_ >= MAX_DIGITAL_INPUTS) return nullptr;
    void* mem = digitalSensorEndpointPool_[digitalSensorEndpointPoolUsed_++];
    return new (mem) DigitalSensorEndpoint(endpointId, valueType);
}

DigitalActuatorEndpoint* IOModule::allocDigitalActuatorEndpoint_(const char* endpointId, DigitalWriteFn writeFn, void* writeCtx)
{
    if (digitalActuatorEndpointPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = digitalActuatorEndpointPool_[digitalActuatorEndpointPoolUsed_++];
    return new (mem) DigitalActuatorEndpoint(endpointId, writeFn, writeCtx);
}

IDigitalCounterDriver* IOModule::allocGpioDriver_(const char* driverId,
                                                  uint8_t pin,
                                                  bool output,
                                                  bool activeHigh,
                                                  uint8_t inputPullMode,
                                                  bool counterEnabled,
                                                  uint8_t edgeMode,
                                                  uint32_t counterDebounceUs)
{
    if (counterEnabled && !output) {
        if (gpioCounterDriverPoolUsed_ >= MAX_DIGITAL_INPUTS) return nullptr;
        void* mem = gpioCounterDriverPool_[gpioCounterDriverPoolUsed_++];
        return new (mem) PcntCounterDriver(driverId, pin, activeHigh, inputPullMode, edgeMode, counterDebounceUs);
    }

    if (gpioDriverPoolUsed_ >= MAX_DIGITAL_SLOTS) return nullptr;
    void* mem = gpioDriverPool_[gpioDriverPoolUsed_++];
    return new (mem) GpioDriver(driverId, pin, output, activeHigh, inputPullMode, false, 0);
}

IAnalogSourceDriver* IOModule::allocAdsDriver_(const char* driverId, I2CBus* bus, const Ads1115DriverConfig& cfg)
{
    if (adsDriverPoolUsed_ >= 2) return nullptr;
    void* mem = adsDriverPool_[adsDriverPoolUsed_++];
    return new (mem) Ads1115Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocDsDriver_(const char* driverId, OneWireBus* bus, const uint8_t address[8], const Ds18b20DriverConfig& cfg)
{
    if (dsDriverPoolUsed_ >= 2) return nullptr;
    void* mem = dsDriverPool_[dsDriverPoolUsed_++];
    return new (mem) Ds18b20Driver(driverId, bus, address, cfg);
}

IAnalogSourceDriver* IOModule::allocSht40Driver_(const char* driverId, I2CBus* bus, const Sht40DriverConfig& cfg)
{
    if (sht40DriverPoolUsed_ >= 1) return nullptr;
    void* mem = sht40DriverPool_[sht40DriverPoolUsed_++];
    return new (mem) Sht40Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocBmp280Driver_(const char* driverId, I2CBus* bus, const Bmp280DriverConfig& cfg)
{
    if (bmp280DriverPoolUsed_ >= 1) return nullptr;
    void* mem = bmp280DriverPool_[bmp280DriverPoolUsed_++];
    return new (mem) Bmp280Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocBme680Driver_(const char* driverId, I2CBus* bus, const Bme680DriverConfig& cfg)
{
    if (bme680DriverPoolUsed_ >= 1) return nullptr;
    void* mem = bme680DriverPool_[bme680DriverPoolUsed_++];
    return new (mem) Bme680Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocIna226Driver_(const char* driverId, I2CBus* bus, const Ina226DriverConfig& cfg)
{
    if (ina226DriverPoolUsed_ >= 1) return nullptr;
    void* mem = ina226DriverPool_[ina226DriverPoolUsed_++];
    return new (mem) Ina226Driver(driverId, bus, cfg);
}

IDigitalPinDriver* IOModule::allocPcfBitDriver_(const char* driverId, Pcf8574Driver* parent, uint8_t bit, bool activeHigh)
{
    if (pcfBitDriverPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = heap_caps_malloc(sizeof(Pcf8574BitDriver), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(Pcf8574BitDriver), MALLOC_CAP_8BIT);
    if (!mem) return nullptr;
    ++pcfBitDriverPoolUsed_;
    return new (mem) Pcf8574BitDriver(driverId, parent, bit, activeHigh);
}

IDigitalPinDriver* IOModule::allocTcaBitDriver_(const char* driverId, Tca9554Driver* parent, uint8_t bit, bool activeHigh)
{
    if (tcaBitDriverPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = heap_caps_malloc(sizeof(Tca9554BitDriver), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(Tca9554BitDriver), MALLOC_CAP_8BIT);
    if (!mem) return nullptr;
    ++tcaBitDriverPoolUsed_;
    return new (mem) Tca9554BitDriver(driverId, parent, bit, activeHigh);
}

IDigitalPinDriver* IOModule::allocMcpBitDriver_(const char* driverId,
                                                Mcp23017Driver* parent,
                                                uint8_t bit,
                                                bool activeHigh,
                                                bool output,
                                                uint8_t inputPullMode)
{
    if (mcpBitDriverPoolUsed_ >= MAX_DIGITAL_SLOTS) return nullptr;
    void* mem = heap_caps_malloc(sizeof(Mcp23017BitDriver), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(Mcp23017BitDriver), MALLOC_CAP_8BIT);
    if (!mem) return nullptr;
    ++mcpBitDriverPoolUsed_;
    return new (mem) Mcp23017BitDriver(driverId, parent, bit, activeHigh, output, inputPullMode);
}

IMaskOutputDriver* IOModule::beginMaskExpander_(IOExpanderId expanderId, uint8_t expectedKind, bool preserveHardwareState)
{
    if (expanderId >= IO_MAX_EXPANDERS) return nullptr;
    const IOExpanderSpec* spec = expanderSpec_(expanderId);
    if (!spec || spec->kind != expectedKind) return nullptr;
    if (!expanderUsable_(expanderId)) return nullptr;

    RuntimeExpander& rt = runtimeExpanders_[expanderId];
    rt.spec = spec;
    if (rt.beginAttempted) {
        if (!rt.beginOk) return nullptr;
        if (expectedKind == IO_EXPANDER_KIND_PCF8574) return rt.pcf;
        if (expectedKind == IO_EXPANDER_KIND_TCA9554) return rt.tca;
        return nullptr;
    }

    rt.beginAttempted = true;
    const uint8_t address = expanderAddress_(expanderId);
    if (expectedKind == IO_EXPANDER_KIND_PCF8574) {
        rt.pcf = static_cast<Pcf8574Driver*>(allocPcfDriver_("pcf8574", i2cBus_, address));
        if (!rt.pcf) return nullptr;
        rt.beginOk = makeMaskProvider(rt.pcf).begin();
        if (!rt.beginOk) {
            LOGW("PCF8574 expander=%u not detected at 0x%02X", (unsigned)expanderId, address);
            return nullptr;
        }
        return rt.pcf;
    }
    if (expectedKind == IO_EXPANDER_KIND_TCA9554) {
        rt.tca = static_cast<Tca9554Driver*>(allocTcaDriver_("tca9554", i2cBus_, address));
        if (!rt.tca) return nullptr;
        rt.beginOk = preserveHardwareState ? rt.tca->beginPreserveHardwareState()
                                           : makeMaskProvider(rt.tca).begin();
        if (!rt.beginOk) {
            LOGW("TCA9554 expander=%u not detected at 0x%02X", (unsigned)expanderId, address);
            return nullptr;
        }
        return rt.tca;
    }
    return nullptr;
}

Mcp23017Driver* IOModule::beginMcpExpander_(IOExpanderId expanderId)
{
    if (expanderId >= IO_MAX_EXPANDERS) return nullptr;
    const IOExpanderSpec* spec = expanderSpec_(expanderId);
    if (!spec || spec->kind != IO_EXPANDER_KIND_MCP23017) return nullptr;
    if (!expanderUsable_(expanderId)) return nullptr;

    RuntimeExpander& rt = runtimeExpanders_[expanderId];
    rt.spec = spec;
    if (rt.beginAttempted) return rt.beginOk ? rt.mcp : nullptr;

    rt.beginAttempted = true;
    const uint8_t address = expanderAddress_(expanderId);
    rt.mcp = allocMcpDriver_("mcp23017", i2cBus_, address);
    if (!rt.mcp) return nullptr;
    rt.beginOk = rt.mcp->begin();
    if (!rt.beginOk) {
        LOGW("MCP23017 expander=%u not detected at 0x%02X", (unsigned)expanderId, address);
        return nullptr;
    }
    return rt.mcp;
}

IMaskOutputDriver* IOModule::allocPcfDriver_(const char* driverId, I2CBus* bus, uint8_t address)
{
    if (pcfDriverPoolUsed_ >= IO_MAX_EXPANDERS) return nullptr;
    void* mem = pcfDriverPool_[pcfDriverPoolUsed_++];
    return new (mem) Pcf8574Driver(driverId, bus, address);
}

IMaskOutputDriver* IOModule::allocTcaDriver_(const char* driverId, I2CBus* bus, uint8_t address)
{
    if (tcaDriverPoolUsed_ >= IO_MAX_EXPANDERS) return nullptr;
    void* mem = tcaDriverPool_[tcaDriverPoolUsed_++];
    return new (mem) Tca9554Driver(driverId, bus, address);
}

Mcp23017Driver* IOModule::allocMcpDriver_(const char* driverId, I2CBus* bus, uint8_t address)
{
    if (mcpDriverPoolUsed_ >= IO_MAX_EXPANDERS) return nullptr;
    void* mem = mcpDriverPool_[mcpDriverPoolUsed_++];
    return new (mem) Mcp23017Driver(driverId, bus, address);
}

bool IOModule::writeDigitalOut_(void* ctx, bool on)
{
    IOModule::DigitalSlot* s = static_cast<IOModule::DigitalSlot*>(ctx);
    if (!s || !s->provider.isBound()) return false;
    if (!s->used || s->kind != DIGITAL_SLOT_OUTPUT) return false;

    if (!s->outDef.momentary) {
        bool ok = s->provider.write(on);
        if (ok && s->owner) s->owner->markIoCycleChanged_(s->ioId);
        return ok;
    }

    // Momentary outputs always generate a physical pulse on each command.
    if (!s->provider.write(true)) return false;
    uint32_t pulse = (s->outDef.pulseMs == 0) ? 500u : (uint32_t)s->outDef.pulseMs;
    const uint32_t nowMs = millis();
    s->pulseDeadlineMs = nowMs + pulse;
    s->pulseArmed = true;
    if (s->owner) s->owner->markIoCycleChanged_(s->ioId);
    return true;
}

bool IOModule::endpointIndexFromId_(const char* id, uint8_t& idxOut) const
{
    if (!id || id[0] == '\0') return false;
    for (uint8_t i = 0; i < registry_.count(); ++i) {
        IOEndpoint* ep = registry_.at(i);
        if (!ep || !ep->id()) continue;
        if (strcmp(ep->id(), id) != 0) continue;
        idxOut = i;
        return true;
    }
    return false;
}

void IOModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Io;
    if (!ensureScalableStorage_() || !ensureConfigDescriptorStorage_()) return;

    cfgStore_ = &cfg;
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    const I2cBusService* i2cBusSvc = services.get<I2cBusService>(ServiceId::I2cBus);
    i2cBus_ = i2cBusSvc ? i2cBusSvc->bus : nullptr;
    if (!i2cBus_) {
        LOGE("service unavailable: %s", toString(ServiceId::I2cBus));
    }
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    if (!services.add(ServiceId::Io, &ioSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Io));
    }

    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(i2cSdaVar_, kCfgModuleId, kCfgBranchIoBus);
    cfg.registerVar(i2cSclVar_, kCfgModuleId, kCfgBranchIoBus);
    cfg.registerVar(adsPollVar_, kCfgModuleId, kCfgBranchIoAds1115);
    cfg.registerVar(dsPollVar_, kCfgModuleId, kCfgBranchIoDs18b20);
    cfg.registerVar(digitalPollVar_, kCfgModuleId, kCfgBranchIoGpio);
    cfg.registerVar(adsInternalAddrVar_, kCfgModuleId, kCfgBranchIoAdsInt);
    cfg.registerVar(adsExternalAddrVar_, kCfgModuleId, kCfgBranchIoAdsExt);
    cfg.registerVar(adsGainVar_, kCfgModuleId, kCfgBranchIoAds1115);
    cfg.registerVar(adsRateVar_, kCfgModuleId, kCfgBranchIoAds1115);
    cfg.registerVar(sht40EnabledVar_, kCfgModuleId, kCfgBranchIoSht40);
    cfg.registerVar(sht40AddressVar_, kCfgModuleId, kCfgBranchIoSht40);
    cfg.registerVar(sht40PollVar_, kCfgModuleId, kCfgBranchIoSht40);
    cfg.registerVar(bmp280EnabledVar_, kCfgModuleId, kCfgBranchIoBmp280);
    cfg.registerVar(bmp280AddressVar_, kCfgModuleId, kCfgBranchIoBmp280);
    cfg.registerVar(bmp280PollVar_, kCfgModuleId, kCfgBranchIoBmp280);
    cfg.registerVar(bme680EnabledVar_, kCfgModuleId, kCfgBranchIoBme680);
    cfg.registerVar(bme680AddressVar_, kCfgModuleId, kCfgBranchIoBme680);
    cfg.registerVar(bme680PollVar_, kCfgModuleId, kCfgBranchIoBme680);
    cfg.registerVar(ina226EnabledVar_, kCfgModuleId, kCfgBranchIoIna226);
    cfg.registerVar(ina226AddressVar_, kCfgModuleId, kCfgBranchIoIna226);
    cfg.registerVar(ina226PollVar_, kCfgModuleId, kCfgBranchIoIna226);
    cfg.registerVar(ina226ShuntOhmsVar_, kCfgModuleId, kCfgBranchIoIna226);
#define FLOW_IO_REGISTER_EXPANDER_CFG(INDEX, BRANCH) \
    cfg.registerVar(exp##INDEX##EnabledVar_, kCfgModuleId, BRANCH); \
    cfg.registerVar(exp##INDEX##AddressVar_, kCfgModuleId, BRANCH); \
    cfg.registerVar(exp##INDEX##MaskDefaultVar_, kCfgModuleId, BRANCH);
    FLOW_IO_REGISTER_EXPANDER_CFG(0, kCfgBranchIoExp0)
    FLOW_IO_REGISTER_EXPANDER_CFG(1, kCfgBranchIoExp1)
    FLOW_IO_REGISTER_EXPANDER_CFG(2, kCfgBranchIoExp2)
    FLOW_IO_REGISTER_EXPANDER_CFG(3, kCfgBranchIoExp3)
#undef FLOW_IO_REGISTER_EXPANDER_CFG
    cfg.registerVar(traceEnabledVar_, kCfgModuleId, kCfgBranchIoDebug);
    cfg.registerVar(tracePeriodVar_, kCfgModuleId, kCfgBranchIoDebug);

    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        IOConfigDescriptorStorage::AnalogSlot& vars = configDescriptors_->analog[i];
        const uint8_t branch = analogCfgBranch_(i);
        cfg.registerVar(vars.nameVar, kCfgModuleId, branch);
        cfg.registerVar(vars.bindingVar, kCfgModuleId, branch);
        cfg.registerVar(vars.c0Var, kCfgModuleId, branch);
        cfg.registerVar(vars.c1Var, kCfgModuleId, branch);
        cfg.registerVar(vars.precisionVar, kCfgModuleId, branch);
    }

    for (uint8_t i = 0; i < DIGITAL_INPUT_CFG_SLOTS; ++i) {
        IOConfigDescriptorStorage::DigitalInputSlot& vars = configDescriptors_->digitalInputs[i];
        const uint8_t branch = digitalInputCfgBranch_(i);
        cfg.registerVar(vars.nameVar, kCfgModuleId, branch);
        cfg.registerVar(vars.bindingVar, kCfgModuleId, branch);
        cfg.registerVar(vars.activeHighVar, kCfgModuleId, branch);
        cfg.registerVar(vars.pullModeVar, kCfgModuleId, branch);
        cfg.registerVar(vars.edgeModeVar, kCfgModuleId, branch);
        cfg.registerVar(vars.counterDebounceVar, kCfgModuleId, branch);
        cfg.registerVar(vars.c0Var, kCfgModuleId, branch);
        cfg.registerVar(vars.precisionVar, kCfgModuleId, branch);
        cfg.registerVar(vars.modeVar, kCfgModuleId, branch);
        cfg.registerVar(vars.counterTotalVar, kCfgModuleId, branch);
    }

    for (uint8_t i = 0; i < DIGITAL_CFG_SLOTS; ++i) {
        IOConfigDescriptorStorage::DigitalOutputSlot& vars = configDescriptors_->digitalOutputs[i];
        const uint8_t branch = digitalOutputCfgBranch_(i);
        cfg.registerVar(vars.nameVar, kCfgModuleId, branch);
        cfg.registerVar(vars.bindingVar, kCfgModuleId, branch);
        cfg.registerVar(vars.activeHighVar, kCfgModuleId, branch);
        cfg.registerVar(vars.initialOnVar, kCfgModuleId, branch);
        cfg.registerVar(vars.retainWarmVar, kCfgModuleId, branch);
        cfg.registerVar(vars.momentaryVar, kCfgModuleId, branch);
        cfg.registerVar(vars.pulseVar, kCfgModuleId, branch);
    }

    LOGI("I/O config registered");
    if (ensureAnalogPrecisionState_()) {
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            analogPrecisionLast_[i] = sanitizeAnalogPrecision_(analogCfg_[i].precision);
        }
        analogPrecisionLastInit_ = true;
    } else {
        analogPrecisionLastInit_ = false;
        LOGE("failed to allocate analog precision state");
    }
    analogConfigDirtyMask_ = 0;

    (void)logHub_;
}

void IOModule::onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        analogCfg_[i].bindingPort = normalizeConfiguredBindingPort(analogCfg_[i].bindingPort);
    }
    for (uint8_t i = 0; i < DIGITAL_INPUT_CFG_SLOTS; ++i) {
        digitalInCfg_[i].bindingPort = normalizeConfiguredBindingPort(digitalInCfg_[i].bindingPort);
    }
    for (uint8_t i = 0; i < DIGITAL_CFG_SLOTS; ++i) {
        digitalCfg_[i].bindingPort = normalizeConfiguredBindingPort(digitalCfg_[i].bindingPort);
    }
    const bool sdaValid = (cfgData_.i2cSda >= 0) && digitalPinIsValid((uint8_t)cfgData_.i2cSda);
    const bool sclValid = (cfgData_.i2cScl >= 0) && digitalPinIsValid((uint8_t)cfgData_.i2cScl);
    if (!sdaValid || !sclValid) {
        LOGW("io.i2c invalid persisted pins sda=%ld scl=%ld, fallback to board defaults sda=%ld scl=%ld",
             (long)cfgData_.i2cSda,
             (long)cfgData_.i2cScl,
             (long)boardDefaultI2cSda_,
             (long)boardDefaultI2cScl_);
        if (cfgSvc_ && cfgSvc_->eraseKeyAsync) {
            (void)cfgSvc_->eraseKeyAsync(cfgSvc_->ctx, i2cSdaVar_.nvsKey);
            (void)cfgSvc_->eraseKeyAsync(cfgSvc_->ctx, i2cSclVar_.nvsKey);
        }
        cfgData_.i2cSda = boardDefaultI2cSda_;
        cfgData_.i2cScl = boardDefaultI2cScl_;
    }
    logI2cConfigTrace_("onConfigLoaded");
    if (!cfgMqttPubConfigured_) {
        cfgMqttPub_.configure(this,
                              kIoCfgProducerId,
                              kIoCfgRoutes,
                              (uint8_t)(sizeof(kIoCfgRoutes) / sizeof(kIoCfgRoutes[0])),
                              services);
        cfgMqttPubConfigured_ = true;
    }

#if defined(FLOW_PROFILE_MICRONOVA)
    LOGI("io.onConfigLoaded deferred runtime init enabled=%s i2c_sda=%ld i2c_scl=%ld",
         cfgData_.enabled ? "true" : "false",
         (long)cfgData_.i2cSda,
         (long)cfgData_.i2cScl);
#else
    configureRuntimeAfterConfig_();
#endif
}

void IOModule::onStart(ConfigStore& cfg, ServiceRegistry& services)
{
    (void)cfg;
    (void)services;
#if defined(FLOW_PROFILE_MICRONOVA)
    configureRuntimeAfterConfig_();
#endif
}

void IOModule::configureRuntimeAfterConfig_()
{
    if (runtimeInitAttempted_) {
        LOGD("io runtime init already attempted");
        return;
    }

    LOGI("io.runtime init begin enabled=%s i2c_sda=%ld i2c_scl=%ld runtimeReady=%s",
         cfgData_.enabled ? "true" : "false",
         (long)cfgData_.i2cSda,
         (long)cfgData_.i2cScl,
         runtimeReady_ ? "true" : "false");
    logI2cConfigTrace_("runtimeInit");

    runtimeInitAttempted_ = true;
    if (cfgData_.enabled) {
        runtimeReady_ = configureRuntime_();
        if (!runtimeReady_) {
            LOGW("Runtime init failed; no runtime allocations will be attempted later");
        } else {
            LOGI("io.runtime configured");
        }
    } else {
        runtimeReady_ = false;
        LOGI("io.runtime init skipped (disabled)");
    }
}

void IOModule::loop()
{
    const IoStatus st = ioTick_(millis());
    if (st != IO_OK) {
        if (!cfgData_.enabled || !runtimeReady_) {
            vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
