/**
 * @file HMIModule.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/HMIModule.h"

#include "App/BuildFlags.h"
#include "Board/BoardSpec.h"
#include "Board/BoardSerialMap.h"
#include "Core/ConfigStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/AlarmIds.h"
#include "Core/Generated/RuntimeUiAlarmText_Generated.h"
#include "Core/SystemLimits.h"
#include "Domain/Pool/PoolIds.h"
#include "Domain/Pool/PoolDefaults.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#include "Modules/Network/TimeModule/TimeRuntime.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"

namespace {

#ifndef FLOW_HMI_CONFIG_MENU_ENABLED
#define FLOW_HMI_CONFIG_MENU_ENABLED 1
#endif
#ifndef FLOW_TIME_PREFER_INTERNAL_RTC
#define FLOW_TIME_PREFER_INTERNAL_RTC 0
#endif
#ifndef TFT_FIRMW
#define TFT_FIRMW "0.0.0"
#endif
static constexpr bool kConfigMenuEnabled = (FLOW_HMI_CONFIG_MENU_ENABLED != 0);
static constexpr const char* kHmiModulePrefix = "hmi/";
static constexpr const char* kPoolLogicSensorsModule = "poollogic/sensors";
static constexpr const char* kPoolLogicDeviceModule = "poollogic/devices";
static constexpr const char* kPoolLogicModesModule = "poollogic/modes";
static constexpr const char* kPoolLogicPhModule = "poollogic/ph";
static constexpr const char* kPoolLogicChlorineModule = "poollogic/chlorine";
static constexpr size_t kPoolLogicSensorsJsonBufSize = 480U;
static constexpr size_t kPoolLogicDeviceJsonBufSize = 192U;
static constexpr size_t kPoolLogicModeJsonBufSize = 256U;
static constexpr uint8_t kLedBitWifiConnected = 0;
static constexpr uint8_t kLedBitPageSelect = 1;
static constexpr uint8_t kLedBitModeA = 2;
static constexpr uint8_t kLedBitModeB = 3;
// Faceplate wiring: LED1..LED4=P0..P3, then LED5..LED8=P7..P4.
static constexpr uint8_t kLedBitAlarmA = 7;
static constexpr uint8_t kLedBitAlarmB = 6;
static constexpr uint8_t kLedBitAlarmC = 5;
static constexpr uint8_t kLedBitAlarmD = 4;
static constexpr uint32_t kLedPageTogglePeriodMs = 2000U;
static constexpr uint8_t kInvalidRuntimeIndex = 0xFFU;
static constexpr uint32_t kHomePublishWaterTemp = 1UL << 0;
static constexpr uint32_t kHomePublishAirTemp = 1UL << 1;
static constexpr uint32_t kHomePublishPh = 1UL << 2;
static constexpr uint32_t kHomePublishOrp = 1UL << 3;
static constexpr uint32_t kHomePublishPhGauge = 1UL << 4;
static constexpr uint32_t kHomePublishOrpGauge = 1UL << 5;
static constexpr uint32_t kHomePublishStateBits = 1UL << 6;
static constexpr uint32_t kHomePublishTime = 1UL << 7;
static constexpr uint32_t kHomePublishDate = 1UL << 8;
static constexpr uint32_t kHomePublishAlarmBits = 1UL << 9;
static constexpr uint32_t kHomePublishErrorMessage = 1UL << 10;
static constexpr uint32_t kHomePublishPsi = 1UL << 11;
static constexpr uint32_t kHomePublishAll = kHomePublishWaterTemp |
                                            kHomePublishAirTemp |
                                            kHomePublishPh |
                                            kHomePublishOrp |
                                            kHomePublishPhGauge |
                                            kHomePublishOrpGauge |
                                            kHomePublishStateBits |
                                            kHomePublishTime |
                                            kHomePublishDate |
                                            kHomePublishAlarmBits |
                                            kHomePublishErrorMessage |
                                            kHomePublishPsi;
static constexpr uint32_t kClockPublishCheckMs = 1000U;
static constexpr uint32_t kHomePeriodicRefreshPeriodMs = 10000U;
static constexpr uint32_t kNextionPageProbeInitialPeriodMs = 1000U;
static constexpr uint8_t kNextionPageProbeInitialAttempts = 10U;
static constexpr uint32_t kNextionPageProbeSteadyPeriodMs = 30000U;
static constexpr uint32_t kNextionMotionWakeRetryMs = 1000U;
static constexpr uint32_t kDisplayVersionProbePeriodMs = 60000U;
static constexpr uint32_t kInvalidClockStamp = 0xFFFFFFFFUL;
static constexpr uint32_t kRtcFallbackDelayMs = 30000U;
static constexpr uint32_t kRtcFallbackRetryMs = 10000U;
static constexpr uint32_t kRtcPushRetryMs = 60000U;
static constexpr uint16_t kLocalNextionRtcReadTimeoutMs = 180U;
static constexpr uint16_t kRemoteNextionRtcReadTimeoutMs = 2000U;
static constexpr const char* kNextionDisplayVersionExpected = TFT_FIRMW;
static constexpr uint8_t kNextionHomePagePrimary = 0U;
static constexpr uint8_t kNextionHomePageAlias = 1U;
static constexpr uint8_t kNextionConfigPagePrimary = 10U;
static constexpr uint8_t kNextionConfigPageAlias = 2U;
static constexpr uint8_t kNextionAlarmPagePrimary = 11U;
static constexpr uint8_t kNextionAlarmPageAlias = 3U;
static constexpr const char* kNextionDegreeC = "\xC2\xB0""C";
static constexpr bool kFrontLedsSupported = true;
static constexpr bool kWs2812StatusLedDefaultEnabled =
#if FLOW_BUILD_IS_WAVESHARE
    true;
#else
    false;
#endif
static constexpr int8_t kWs2812StatusLedGpio = 38;
static constexpr uint8_t kWs2812T0BlueRed = 0U;
static constexpr uint8_t kWs2812T0BlueGreen = 0U;
static constexpr uint8_t kWs2812T0BlueBlue = 255U;
static constexpr uint8_t kWs2812T0BlueBrightness = 128U;
static constexpr uint8_t kWs2812AlarmRedRed = 255U;
static constexpr uint8_t kWs2812AlarmRedGreen = 0U;
static constexpr uint8_t kWs2812AlarmRedBlue = 0U;
static constexpr uint8_t kWs2812AlarmRedBrightness = 128U;
static constexpr uint8_t kWs2812ProvisioningRed = 255U;
static constexpr uint8_t kWs2812ProvisioningGreen = 96U;
static constexpr uint8_t kWs2812ProvisioningBlue = 0U;
static constexpr uint8_t kWs2812ProvisioningBrightness = 160U;
static constexpr uint16_t kWs2812NetSlowBlinkOnMs = 750U;
static constexpr uint16_t kWs2812NetSlowBlinkOffMs = 750U;
static constexpr uint16_t kWs2812NetFastBlinkOnMs = 150U;
static constexpr uint16_t kWs2812NetFastBlinkOffMs = 150U;
static constexpr uint16_t kWs2812NormalBreathePeriodMs = 900U;
static constexpr uint16_t kWs2812ProvisioningBreathePeriodMs = 700U;
static constexpr uint32_t kWs2812AlarmAlternationMs = 2000U;
static constexpr uint32_t kHmiLedDebugPeriodMs = 5000U;

static bool isSupportedNextionDisplayVersion_(const char* version)
{
    return version && strcmp(version, kNextionDisplayVersionExpected) == 0;
}

static const char* const kMonthNamesFr[] = {
    "Janvier",
    "F\xE9""vrier",
    "Mars",
    "Avril",
    "Mai",
    "Juin",
    "Juillet",
    "Ao\xFB""t",
    "Septembre",
    "Octobre",
    "Novembre",
    "D\xE9""cembre",
};

static const char* const kDayNamesFr[] = {
    "Dimanche",
    "Lundi",
    "Mardi",
    "Mercredi",
    "Jeudi",
    "Vendredi",
    "Samedi",
};

struct PoolLogicModeFlags {
    bool autoMode = false;
    bool winterMode = false;
    bool phAutoMode = false;
    bool orpAutoMode = false;
};

static uint16_t gaugePercentFromSetpoint_(float value, float setpoint)
{
    const float span = fabsf(setpoint) * 0.30f;
    if (span <= 0.0001f) return 140U;

    const float low = setpoint - span;
    const float high = setpoint + span;
    const float normalized = ((value - low) / (high - low)) * 280.0f;
    if (normalized <= 0.0f) return 0U;
    if (normalized >= 280.0f) return 280U;
    return (uint16_t)lroundf(normalized);
}

static inline uint8_t normalizeLedPage_(uint8_t page)
{
    return (page == 2U) ? 2U : 1U;
}

static bool isHomePageCode_(uint8_t pageId)
{
    return pageId == 0U || pageId == 1U;
}

static const char* hmiEventTypeName_(HmiEventType type)
{
    switch (type) {
        case HmiEventType::None: return "None";
        case HmiEventType::Home: return "Home";
        case HmiEventType::Back: return "Back";
        case HmiEventType::Validate: return "Validate";
        case HmiEventType::NextPage: return "NextPage";
        case HmiEventType::PrevPage: return "PrevPage";
        case HmiEventType::RowActivate: return "RowActivate";
        case HmiEventType::RowToggle: return "RowToggle";
        case HmiEventType::RowCycle: return "RowCycle";
        case HmiEventType::RowSetText: return "RowSetText";
        case HmiEventType::RowSetSlider: return "RowSetSlider";
        case HmiEventType::RowEdit: return "RowEdit";
        case HmiEventType::Command: return "Command";
        case HmiEventType::Page: return "Page";
        case HmiEventType::ConfigEnter: return "ConfigEnter";
        case HmiEventType::ConfigExit: return "ConfigExit";
        case HmiEventType::DisplaySleep: return "DisplaySleep";
        case HmiEventType::DisplayWake: return "DisplayWake";
        default: return "Unknown";
    }
}

static bool isAlarmPageCode_(uint8_t pageId)
{
    return pageId == kNextionAlarmPagePrimary || pageId == kNextionAlarmPageAlias;
}

static bool isConfigPageCode_(uint8_t pageId)
{
    return pageId == kNextionConfigPagePrimary || pageId == kNextionConfigPageAlias;
}

static uint32_t alarmMaskFromId_(AlarmId id)
{
    switch (id) {
        case AlarmId::PoolPsiLow: return 1UL;
        case AlarmId::PoolPsiHigh: return 2UL;
        case AlarmId::PoolPhTankLow: return 4UL;
        case AlarmId::PoolChlorineTankLow: return 8UL;
        case AlarmId::PoolPhPumpMaxUptime: return 16UL;
        case AlarmId::PoolChlorinePumpMaxUptime: return 32UL;
        case AlarmId::PoolWaterLevelLow: return 64UL;
        default: return 0UL;
    }
}

static bool isRtcDateTimeValid_(const HmiRtcDateTime& rtc)
{
    return rtc.year >= 2021U &&
           rtc.year <= 2099U &&
           rtc.month >= 1U &&
           rtc.month <= 12U &&
           rtc.day >= 1U &&
           rtc.day <= 31U &&
           rtc.hour <= 23U &&
           rtc.minute <= 59U &&
           rtc.second <= 59U;
}

static bool rtcDateTimeToEpoch_(const HmiRtcDateTime& rtc, uint64_t& epoch)
{
    epoch = 0ULL;
    if (!isRtcDateTimeValid_(rtc)) return false;

    struct tm local{};
    local.tm_year = (int)rtc.year - 1900;
    local.tm_mon = (int)rtc.month - 1;
    local.tm_mday = (int)rtc.day;
    local.tm_hour = (int)rtc.hour;
    local.tm_min = (int)rtc.minute;
    local.tm_sec = (int)rtc.second;
    local.tm_isdst = -1;

    const time_t converted = mktime(&local);
    if (converted < (time_t)1609459200) return false;
    if ((uint16_t)(local.tm_year + 1900) != rtc.year ||
        (uint8_t)(local.tm_mon + 1) != rtc.month ||
        (uint8_t)local.tm_mday != rtc.day ||
        (uint8_t)local.tm_hour != rtc.hour ||
        (uint8_t)local.tm_min != rtc.minute) {
        return false;
    }

    epoch = (uint64_t)converted;
    return true;
}

static bool epochToRtcDateTime_(uint64_t epoch, HmiRtcDateTime& out)
{
    out = HmiRtcDateTime{};
    const time_t t = (time_t)epoch;
    struct tm local{};
    if (!localtime_r(&t, &local)) return false;
    out.year = (uint16_t)(local.tm_year + 1900);
    out.month = (uint8_t)(local.tm_mon + 1);
    out.day = (uint8_t)local.tm_mday;
    out.hour = (uint8_t)local.tm_hour;
    out.minute = (uint8_t)local.tm_min;
    out.second = (uint8_t)local.tm_sec;
    return isRtcDateTimeValid_(out);
}

static uint32_t rtcDayStamp_(uint64_t epoch)
{
    const time_t t = (time_t)epoch;
    struct tm local{};
    if (!localtime_r(&t, &local)) return kInvalidClockStamp;
    return ((uint32_t)(local.tm_year + 1900) * 1000UL) + (uint32_t)local.tm_yday;
}

static bool findJsonBool_(const char* json, const char* key, bool& out)
{
    if (!json || !key || key[0] == '\0') return false;
    char needle[48]{};
    const int nn = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (nn <= 0 || (size_t)nn >= sizeof(needle)) return false;
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += nn;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (strncmp(p, "true", 4) == 0) {
        out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        out = false;
        return true;
    }
    return false;
}

static bool hasValidNetworkIp_(const IpV4& ip)
{
    return ip.b[0] != 0U || ip.b[1] != 0U || ip.b[2] != 0U || ip.b[3] != 0U;
}

static bool isNetworkConnected_(const DataStoreService* dsSvc)
{
    if (!dsSvc || !dsSvc->store) return false;
    const DataStore& ds = *dsSvc->store;
    if (!networkReady(ds)) return false;
    return hasValidNetworkIp_(networkIp(ds));
}

static bool formatSensorIoRef_(IoId ioId, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return false;
    out[0] = '\0';

    if (ioId == IO_ID_INVALID) {
        snprintf(out, outLen, "none");
        return false;
    }

    if (ioId >= IO_ID_AI_BASE && ioId < (IoId)(IO_ID_AI_BASE + Limits::Io::MaxAnalogEndpoints)) {
        snprintf(out, outLen, "a%02u", (unsigned)(ioId - IO_ID_AI_BASE));
        return true;
    }

    if (ioId >= IO_ID_DI_BASE && ioId < (IoId)(IO_ID_DI_BASE + Limits::Io::MaxDigitalInputs)) {
        snprintf(out, outLen, "i%02u", (unsigned)(ioId - IO_ID_DI_BASE));
        return true;
    }

    snprintf(out, outLen, "unknown");
    return false;
}

static bool findJsonUInt16_(const char* json, const char* key, uint16_t& out)
{
    if (!json || !key || key[0] == '\0') return false;
    char needle[48]{};
    const int nn = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (nn <= 0 || (size_t)nn >= sizeof(needle)) return false;
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += nn;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (*p < '0' || *p > '9') return false;

    unsigned long v = 0UL;
    while (*p >= '0' && *p <= '9') {
        v = (v * 10UL) + (unsigned long)(*p - '0');
        if (v > 65535UL) return false;
        ++p;
    }
    out = (uint16_t)v;
    return true;
}

static bool findJsonFloat_(const char* json, const char* key, float& out)
{
    if (!json || !key || key[0] == '\0') return false;
    char needle[48]{};
    const int nn = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (nn <= 0 || (size_t)nn >= sizeof(needle)) return false;
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += nn;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;

    char* end = nullptr;
    const float value = strtof(p, &end);
    if (end == p) return false;
    out = value;
    return true;
}

static bool extractJsonStringField_(const char* json, const char* key, char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';
    if (!json || !key || key[0] == '\0') return false;

    char needle[48]{};
    const int nn = snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    if (nn <= 0 || (size_t)nn >= sizeof(needle)) return false;
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += nn;

    size_t pos = 0;
    while (*p != '\0' && *p != '"' && pos + 1 < outLen) {
        const unsigned char uc = (unsigned char)*p;
        if (uc < 32U) break;
        out[pos++] = (char)uc;
        ++p;
    }
    out[pos] = '\0';
    return pos > 0;
}

#if FLOW_HMI_CONFIG_MENU_ENABLED
static const ConfigMenuHint kHints[] = {
    {"poollogic/filtration", "filtr_start_min", {ConfigMenuWidget::Slider, true, 0.0f, 23.0f, 1.0f, nullptr}},
    {"poollogic/filtration", "filtr_stop_max", {ConfigMenuWidget::Slider, true, 0.0f, 23.0f, 1.0f, nullptr}},
    {"poollogic/ph", "ph_setpoint", {ConfigMenuWidget::Slider, true, 6.6f, 7.8f, 0.1f, nullptr}},
    {"poollogic/chlorine", "dis_setpoint", {ConfigMenuWidget::Slider, true, 450.0f, 950.0f, 10.0f, nullptr}},
    {"time", "tz", {ConfigMenuWidget::Select, true, 0.0f, 0.0f, 1.0f,
                    "CET-1CEST,M3.5.0/2,M10.5.0/3|UTC0|EST5EDT,M3.2.0/2,M11.1.0/2"}}
};
#endif

} // namespace

void HMIModule::applyOutputConfig_()
{
    IHmiDriver* wantedDriver = nullptr;
    const bool nextionUartConfigured = (Board::SerialMap::hmiRxPin() >= 0) && (Board::SerialMap::hmiTxPin() >= 0);
    const bool hmiUartBlockedByNativeUsb = Board::SerialMap::hmiUartBlockedByNativeUsb();
    if (cfgData_.nextionEnabled && !cfgData_.remoteUdpEnabled && hmiUartBlockedByNativeUsb) {
        LOGW("Nextion disabled: HMI UART pins overlap native USB CDC (set FLOW_ALLOW_HMI_ON_NATIVE_USB_PINS=1 to override)");
    } else if (cfgData_.nextionEnabled && !cfgData_.remoteUdpEnabled && !nextionUartConfigured) {
        LOGW("Nextion disabled: no local HMI UART configured on active board");
    }
    if (cfgData_.remoteUdpEnabled) {
        wantedDriver = static_cast<IHmiDriver*>(&remoteUdp_);
    } else if (cfgData_.nextionEnabled && nextionUartConfigured && !nextionDisabledByVersion_) {
        wantedDriver = static_cast<IHmiDriver*>(&nextion_);
    }
    if (driver_ != wantedDriver) {
        driver_ = wantedDriver;
        driverReady_ = false;
        if (!driver_) {
            configMenuActive_ = false;
            alarmPageActive_ = false;
        }
        if (driver_) viewDirty_ = true;
    }

    TfaVeniceRf433Config veniceCfg{};
    veniceCfg.enabled = cfgData_.veniceEnabled;
    veniceCfg.txPin =
        (cfgData_.veniceTxGpio >= 0 && cfgData_.veniceTxGpio <= 127) ? (int8_t)cfgData_.veniceTxGpio : (int8_t)-1;
    venice_.setConfig(veniceCfg);

    (void)ws2812StatusLed_.setEnabled(cfgData_.waveshareLedEnabled);
    ledMaskValid_ = false;
}

void HMIModule::setRemoteUdpServer(HmiUdpServerModule* server)
{
    remoteUdpServer_ = server;
    remoteUdp_.setUdpServer(server);
}

bool HMIModule::requestRefresh_()
{
    if (configMenuActive_ || alarmPageActive_) viewDirty_ = true;
    return true;
}

bool HMIModule::openConfigHome_()
{
#if FLOW_HMI_CONFIG_MENU_ENABLED
    if (!ensureConfigMenuReady_()) return false;
    const bool ok = menu_.home();
    if (ok) {
        configMenuActive_ = true;
        viewDirty_ = true;
    }
    return ok;
#else
    return false;
#endif
}

bool HMIModule::openConfigModule_(const char* module)
{
#if FLOW_HMI_CONFIG_MENU_ENABLED
    if (!ensureConfigMenuReady_()) return false;
    const bool ok = menu_.openModule(module);
    if (ok) {
        configMenuActive_ = true;
        viewDirty_ = true;
    }
    return ok;
#else
    (void)module;
    return false;
#endif
}

bool HMIModule::setLedPage_(uint8_t page)
{
    const uint8_t newPage = normalizeLedPage_(page);
    if (ledPage_ != newPage) {
        ledPage_ = newPage;
        applyLedMask_(true);
    }
    return true;
}

uint8_t HMIModule::getLedPage_() const
{
    return ledPage_;
}

bool HMIModule::setStatusLedState_(const HmiStatusLedState* state)
{
    if (!state) return false;

    Ws2812StatusLedState next{};
    next.enabled = state->enabled;
    next.blinkEnabled = state->blinkEnabled;
    next.red = state->red;
    next.green = state->green;
    next.blue = state->blue;
    next.brightness = state->brightness;
    next.blinkOnMs = state->blinkOnMs;
    next.blinkOffMs = state->blinkOffMs;
    next.breatheEnabled = false;
    ws2812AutoWifiMode_ = false;
    ws2812AutoWifiApplied_ = false;
    return ws2812StatusLed_.setState(next);
}

bool HMIModule::getStatusLedState_(HmiStatusLedState* out) const
{
    if (!out) return false;
    Ws2812StatusLedState current{};
    if (!ws2812StatusLed_.getState(current)) return false;
    out->enabled = current.enabled;
    out->blinkEnabled = current.blinkEnabled || current.breatheEnabled;
    out->red = current.red;
    out->green = current.green;
    out->blue = current.blue;
    out->brightness = current.brightness;
    out->blinkOnMs = current.blinkOnMs;
    out->blinkOffMs = current.blinkOffMs;
    return true;
}

bool HMIModule::setStatusLedAutoWifiMode_(bool enabled)
{
    ws2812AutoWifiMode_ = enabled;
    ws2812AutoWifiApplied_ = false;
    ws2812AutoWifiAlarmRedPhaseLast_ = false;
    return true;
}

bool HMIModule::isStatusLedAutoWifiMode_() const
{
    return ws2812AutoWifiMode_;
}

bool HMIModule::setLedCondition_(HmiLedCondition condition, bool active)
{
    ws2812AutoWifiMode_ = true;
    ws2812StatusLed_.setCondition(condition, active);
    return true;
}

void HMIModule::clearLedConditions_()
{
    ws2812StatusLed_.clearAllConditions();
}

void HMIModule::setBootComplete_()
{
    ws2812StatusLed_.setBootComplete();
}

bool HMIModule::setLedEnabled_(bool enabled)
{
    return ws2812StatusLed_.setEnabled(enabled);
}

bool HMIModule::setLedBrightness_(uint8_t brightness)
{
    return ws2812StatusLed_.setBrightness(brightness);
}

bool HMIModule::getDisplayVersion_(char* out, size_t outLen) const
{
    if (!out || outLen == 0U || !nextionVersionDetected_) return false;
    strncpy(out, nextionVersion_, outLen - 1U);
    out[outLen - 1U] = '\0';
    return true;
}

HMIModule::HMIModule(const BoardSpec& board)
{
    const I2cBusSpec* ioBus = boardFindI2cBus(board, "io");
    if (ioBus) {
        frontLedI2cSda_ = ioBus->sdaPin;
        frontLedI2cScl_ = ioBus->sclPin;
        frontLedI2cFrequencyHz_ = ioBus->frequencyHz;
    }
    const IoPointSpec* tx433 = boardFindIoPoint(board, BoardSignal::Tx433);
    if (tx433) {
        cfgData_.veniceTxGpio = tx433->pin;
    }
}

void HMIModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    services_ = &services;
    cfg.registerVar(ledsEnabledVar_);
    cfg.registerVar(waveshareLedEnabledVar_);
    cfg.registerVar(nextionEnabledVar_);
    cfg.registerVar(nextionMotionIoIdVar_);
    cfg.registerVar(remoteUdpEnabledVar_);
    cfg.registerVar(veniceEnabledVar_);
    cfg.registerVar(veniceTxGpioVar_);

    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    dsSvc_ = services.get<DataStoreService>(ServiceId::DataStore);
    alarmSvc_ = services.get<AlarmService>(ServiceId::Alarm);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    domainStatusSvc_ = services.get<DomainStatusService>(ServiceId::DomainStatus);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    timeSvc_ = services.get<TimeService>(ServiceId::Time);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    localeSvc_ = services.get<LocaleService>(ServiceId::Locale);
    const I2cBusService* i2cBusSvc = services.get<I2cBusService>(ServiceId::I2cBus);
    i2cBus_ = i2cBusSvc ? i2cBusSvc->bus : nullptr;
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    auto* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;

    if (!cfgSvc_) {
        LOGE("Config service unavailable");
        return;
    }

#if FLOW_HMI_CONFIG_MENU_ENABLED
    {
        LOGI("Config menu enabled (lazy init)");
    }
#else
    {
        LOGI("Config menu disabled at compile-time");
    }
#endif

    if (eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &HMIModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::DataChanged, &HMIModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmRaised, &HMIModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmCleared, &HMIModule::onEventStatic_, this);
    }

    if (!services.add(ServiceId::Hmi, &hmiSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Hmi));
    }

    NextionDriverConfig dcfg{};
    dcfg.serial = &Board::SerialMap::hmiSerial();
    dcfg.rxPin = Board::SerialMap::hmiRxPin();
    dcfg.txPin = Board::SerialMap::hmiTxPin();
    dcfg.baud = Board::SerialMap::hmiBaud();
    dcfg.homePageId = kNextionHomePagePrimary;
    dcfg.homePageAliasId = kNextionHomePageAlias;
    dcfg.configPageId = kNextionConfigPagePrimary;
    dcfg.configPageAliasId = kNextionConfigPageAlias;
    dcfg.alarmPageId = kNextionAlarmPagePrimary;
    dcfg.alarmPageAliasId = kNextionAlarmPageAlias;
    nextion_.setConfig(dcfg);
    applyOutputConfig_();
    Ws2812StatusLedDriver::Config wsCfg{};
    wsCfg.enabled = kWs2812StatusLedDefaultEnabled;
    wsCfg.gpio = kWs2812StatusLedGpio;
    ws2812StatusLed_.setConfig(wsCfg);
    const bool ws2812Ready = ws2812StatusLed_.begin();
    (void)ws2812StatusLed_.setEnabled(cfgData_.waveshareLedEnabled);
    driverReady_ = false;
    nextionDisabledByVersion_ = false;
    homePageVisible_ = false;
    menuSessionActive_ = false;
    menuPageVisible_ = false;
    alarmPageActive_ = false;
    configMenuReady_ = false;
    configMenuActive_ = false;
    viewDirty_ = true;
    lastRenderMs_ = 0;
    lastHmiLedDebugMs_ = 0;
    ledPage_ = 1U;
    ledMaskValid_ = false;
    ws2812AutoWifiMode_ = true;
    ws2812AutoWifiApplied_ = false;
    ws2812AutoWifiConnectedLast_ = false;
    ws2812AutoWifiMqttLast_ = false;
    ws2812AutoWifiApModeLast_ = false;
    ws2812AutoWifiNormalLast_ = false;
    ws2812AutoWifiAlarmActiveLast_ = false;
    ws2812AutoWifiAlarmRedPhaseLast_ = false;
    lastLedApplyTryMs_ = 0;
    lastLedPageToggleMs_ = millis();
    lastClockCheckMs_ = 0;
    lastHomePeriodicRefreshMs_ = 0;
    lastNextionMotionWakeAttemptMs_ = 0;
    lastDisplayVersionProbeMs_ = 0;
    lastClockMinuteStamp_ = kInvalidClockStamp;
    lastClockDayStamp_ = kInvalidClockStamp;
    lastRtcFallbackAttemptMs_ = 0;
    lastRtcPushAttemptMs_ = 0;
    lastRtcPushDayStamp_ = kInvalidClockStamp;
    rtcFallbackCompleted_ = false;
    rtcPushPending_ = false;
    nextionVersionDetected_ = false;
    nextionMotionInputReady_ = false;
    nextionMotionActiveLast_ = false;
    nextionMotionReadErrorLogged_ = false;
    nextionVersion_[0] = '\0';
    activeConfigContextToken_ = 0U;
    nextConfigContextToken_ = 1U;
    alarmPageIndex_ = 0U;
    alarmPageCount_ = 1U;
    alarmRowCount_ = 0U;
    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        alarmRowIds_[i] = AlarmId::None;
        alarmRowResettable_[i] = false;
    }
    snprintf(localeLang_, sizeof(localeLang_), "fr");
    localeGenerationSeen_ = 0U;
    refreshLocale_();
    phIoId_ = ioIdFromSlot(analogInputSlot(1));
    orpIoId_ = ioIdFromSlot(analogInputSlot(0));
    psiIoId_ = ioIdFromSlot(analogInputSlot(2));
    airTempIoId_ = ioIdFromSlot(analogInputSlot(5));
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    poolLevelIoId_ = ioIdFromSlot(digitalInputSlot(11));
    phLevelIoId_ = ioIdFromSlot(digitalInputSlot(9));
    chlorineLevelIoId_ = ioIdFromSlot(digitalInputSlot(10));
#else
    poolLevelIoId_ = ioIdFromSlot(digitalInputSlot(0));
    phLevelIoId_ = ioIdFromSlot(digitalInputSlot(1));
    chlorineLevelIoId_ = ioIdFromSlot(digitalInputSlot(2));
#endif
    waterTempIoId_ = ioIdFromSlot(analogInputSlot(4));
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    waterCounterIoId_ = ioIdFromSlot(digitalInputSlot(12));
#else
    waterCounterIoId_ = ioIdFromSlot(digitalInputSlot(3));
#endif
    phRuntimeIndex_ = kInvalidRuntimeIndex;
    orpRuntimeIndex_ = kInvalidRuntimeIndex;
    psiRuntimeIndex_ = kInvalidRuntimeIndex;
    waterTempRuntimeIndex_ = kInvalidRuntimeIndex;
    airTempRuntimeIndex_ = kInvalidRuntimeIndex;
    poolLevelRuntimeIndex_ = kInvalidRuntimeIndex;
    phLevelRuntimeIndex_ = kInvalidRuntimeIndex;
    chlorineLevelRuntimeIndex_ = kInvalidRuntimeIndex;
    waterCounterRuntimeIndex_ = kInvalidRuntimeIndex;
    refreshNetworkExpectations_();
    homeErrorMessage_[0] = '\0';
    applyWs2812AutoWifiProfile_();
    if (wsCfg.enabled && !ws2812Ready) {
        LOGW("WS2812 status LED init failed on gpio=%d", (int)wsCfg.gpio);
    }

    LOGI("HMI service registered with driver=%s led_panel_i2c=%s ws2812=%s",
         driver_ ? driver_->driverId() : "none",
         i2cBus_ ? "available" : "unavailable",
         ws2812Ready ? "on" : "off");
}

void HMIModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!ioSvc_) ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    nextionMotionInputReady_ = false;
    nextionMotionActiveLast_ = false;
    nextionMotionReadErrorLogged_ = false;
    lastNextionMotionWakeAttemptMs_ = 0U;
    domainStatusSvc_ = services.get<DomainStatusService>(ServiceId::DomainStatus);
    refreshNetworkExpectations_();
    refreshLocale_();
    applyOutputConfig_();
    applyFrontLedPanelConfig_();
    refreshHomeBindings_();
    queueHomePublish_(kHomePublishAll);
    applyLedMask_(true);
    ws2812AutoWifiApplied_ = false;
    applyWs2812AutoWifiProfile_();
}

void HMIModule::refreshNetworkExpectations_()
{
    wifiNetworkExpected_ = true;
    ethernetNetworkExpected_ = false;

    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return;

    char jsonBuf[160] = {0};
    if (cfgSvc_->toJsonModule(cfgSvc_->ctx, "wifi", jsonBuf, sizeof(jsonBuf), nullptr)) {
        (void)findJsonBool_(jsonBuf, "enabled", wifiNetworkExpected_);
    }

    memset(jsonBuf, 0, sizeof(jsonBuf));
    if (cfgSvc_->toJsonModule(cfgSvc_->ctx, "ethernet", jsonBuf, sizeof(jsonBuf), nullptr)) {
        (void)findJsonBool_(jsonBuf, "enabled", ethernetNetworkExpected_);
    }
}

void HMIModule::onEventStatic_(const Event& e, void* user)
{
    HMIModule* self = static_cast<HMIModule*>(user);
    if (self) self->onEvent_(e);
}

bool HMIModule::resolveIoRuntimeIndex_(IoId ioId, uint8_t& outIndex) const
{
    outIndex = kInvalidRuntimeIndex;

    if (ioId == IO_ID_INVALID) return false;

    if (!ioSvc_ || !ioSvc_->count || !ioSvc_->idAt) return false;

    const uint8_t count = ioSvc_->count(ioSvc_->ctx);
    for (uint8_t i = 0; i < count; ++i) {
        IoId curId = IO_ID_INVALID;
        if (ioSvc_->idAt(ioSvc_->ctx, i, &curId) != IO_OK) continue;
        if (curId != ioId) continue;
        outIndex = i;
        return true;
    }
    return false;
}

void HMIModule::refreshHomeBindings_()
{
    if (cfgSvc_ && cfgSvc_->toJsonModule) {
        char jsonBuf[kPoolLogicSensorsJsonBufSize]{};
        bool truncated = false;
        bool sensorsTruncated = false;
        bool foundPoolLevel = false;
        bool foundPh = false;
        bool foundOrp = false;
        bool foundPsi = false;
        bool foundWaterTemp = false;
        bool foundAirTemp = false;
        bool foundPhLevel = false;
        bool foundChlorineLevel = false;
        bool foundWaterCounter = false;
        bool foundFiltrationSlot = false;
        bool foundPhPumpSlot = false;
        bool foundOrpPumpSlot = false;
        bool foundRobotSlot = false;
        bool foundFillingSlot = false;

        if (cfgSvc_->toJsonModule(cfgSvc_->ctx,
                                  kPoolLogicSensorsModule,
                                  jsonBuf,
                                  sizeof(jsonBuf),
                                  &truncated)) {
            sensorsTruncated = truncated;
            uint16_t ioId = (uint16_t)poolLevelIoId_;
            foundPoolLevel = findJsonUInt16_(jsonBuf, "pool_lvl_io_id", ioId);
            if (foundPoolLevel) {
                poolLevelIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)phIoId_;
            foundPh = findJsonUInt16_(jsonBuf, "ph_io_id", ioId);
            if (foundPh) {
                phIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)orpIoId_;
            foundOrp = findJsonUInt16_(jsonBuf, "dis_io_id", ioId);
            if (foundOrp) {
                orpIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)psiIoId_;
            foundPsi = findJsonUInt16_(jsonBuf, "psi_io_id", ioId);
            if (foundPsi) {
                psiIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)waterTempIoId_;
            foundWaterTemp = findJsonUInt16_(jsonBuf, "wat_temp_io_id", ioId);
            if (foundWaterTemp) {
                waterTempIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)airTempIoId_;
            foundAirTemp = findJsonUInt16_(jsonBuf, "air_temp_io_id", ioId);
            if (foundAirTemp) {
                airTempIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)phLevelIoId_;
            foundPhLevel = findJsonUInt16_(jsonBuf, "ph_lvl_io_id", ioId);
            if (foundPhLevel) {
                phLevelIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)chlorineLevelIoId_;
            foundChlorineLevel = findJsonUInt16_(jsonBuf, "chl_lvl_io_id", ioId);
            if (foundChlorineLevel) {
                chlorineLevelIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)waterCounterIoId_;
            foundWaterCounter = findJsonUInt16_(jsonBuf, "water_counter_io_id", ioId);
            if (foundWaterCounter) {
                waterCounterIoId_ = (IoId)ioId;
            }
        } else {
            LOGW("HMI poollogic sensors export failed");
        }

        truncated = false;
        memset(jsonBuf, 0, sizeof(jsonBuf));
        if (cfgSvc_->toJsonModule(cfgSvc_->ctx,
                                  kPoolLogicDeviceModule,
                                  jsonBuf,
                                  kPoolLogicDeviceJsonBufSize,
                                  &truncated)) {
            uint16_t slot = filtrationDeviceSlot_;
            foundFiltrationSlot = findJsonUInt16_(jsonBuf, "filtr_slot", slot);
            if (foundFiltrationSlot) {
                filtrationDeviceSlot_ = (uint8_t)slot;
            }
            slot = phPumpDeviceSlot_;
            foundPhPumpSlot = findJsonUInt16_(jsonBuf, "ph_pump_slot", slot);
            if (foundPhPumpSlot) {
                phPumpDeviceSlot_ = (uint8_t)slot;
            }
            slot = orpPumpDeviceSlot_;
            foundOrpPumpSlot = findJsonUInt16_(jsonBuf, "dis_pump_slot", slot);
            if (foundOrpPumpSlot) {
                orpPumpDeviceSlot_ = (uint8_t)slot;
            }
            slot = robotDeviceSlot_;
            foundRobotSlot = findJsonUInt16_(jsonBuf, "robot_slot", slot);
            if (foundRobotSlot) {
                robotDeviceSlot_ = (uint8_t)slot;
            }
            slot = fillingDeviceSlot_;
            foundFillingSlot = findJsonUInt16_(jsonBuf, "fill_slot", slot);
            if (foundFillingSlot) {
                fillingDeviceSlot_ = (uint8_t)slot;
            }

            LOGD("HMI poollogic cfg sensors_trunc=%u device_trunc=%u keys lvl=%u ph=%u orp=%u psi=%u wat=%u air=%u phlvl=%u chllvl=%u wc=%u filtr=%u php=%u orpp=%u robot=%u fill=%u",
                 sensorsTruncated ? 1U : 0U,
                 truncated ? 1U : 0U,
                 foundPoolLevel ? 1U : 0U,
                 foundPh ? 1U : 0U,
                 foundOrp ? 1U : 0U,
                 foundPsi ? 1U : 0U,
                 foundWaterTemp ? 1U : 0U,
                 foundAirTemp ? 1U : 0U,
                 foundPhLevel ? 1U : 0U,
                 foundChlorineLevel ? 1U : 0U,
                 foundWaterCounter ? 1U : 0U,
                 foundFiltrationSlot ? 1U : 0U,
                 foundPhPumpSlot ? 1U : 0U,
                 foundOrpPumpSlot ? 1U : 0U,
                 foundRobotSlot ? 1U : 0U,
                 foundFillingSlot ? 1U : 0U);
        } else {
            LOGW("HMI poollogic device export failed");
        }
    }

    (void)resolveIoRuntimeIndex_(phIoId_, phRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(orpIoId_, orpRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(psiIoId_, psiRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(waterTempIoId_, waterTempRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(airTempIoId_, airTempRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(poolLevelIoId_, poolLevelRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(phLevelIoId_, phLevelRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(chlorineLevelIoId_, chlorineLevelRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(waterCounterIoId_, waterCounterRuntimeIndex_);

    LOGD("HMI home bindings ph=%u(rt=%u) orp=%u(rt=%u) psi=%u(rt=%u) wat=%u(rt=%u) air=%u(rt=%u) lvl=%u(rt=%u)",
         (unsigned)phIoId_,
         (unsigned)phRuntimeIndex_,
         (unsigned)orpIoId_,
         (unsigned)orpRuntimeIndex_,
         (unsigned)psiIoId_,
         (unsigned)psiRuntimeIndex_,
         (unsigned)waterTempIoId_,
         (unsigned)waterTempRuntimeIndex_,
         (unsigned)airTempIoId_,
         (unsigned)airTempRuntimeIndex_,
         (unsigned)poolLevelIoId_,
         (unsigned)poolLevelRuntimeIndex_);
}

bool HMIModule::readPoolLogicModeFlags_(bool& autoMode,
                                        bool& winterMode,
                                        bool& phAutoMode,
                                        bool& orpAutoMode) const
{
    autoMode = false;
    winterMode = false;
    phAutoMode = false;
    orpAutoMode = false;
    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return false;

    char jsonBuf[kPoolLogicModeJsonBufSize]{};
    bool truncated = false;
    if (!cfgSvc_->toJsonModule(cfgSvc_->ctx,
                               kPoolLogicModesModule,
                               jsonBuf,
                               sizeof(jsonBuf),
                               &truncated)) {
        return false;
    }

    bool v = false;
    if (findJsonBool_(jsonBuf, "auto_mode", v)) autoMode = v;
    if (findJsonBool_(jsonBuf, "winter_mode", v)) winterMode = v;

    memset(jsonBuf, 0, sizeof(jsonBuf));
    truncated = false;
    if (cfgSvc_->toJsonModule(cfgSvc_->ctx,
                              kPoolLogicPhModule,
                              jsonBuf,
                              sizeof(jsonBuf),
                              &truncated)) {
        if (findJsonBool_(jsonBuf, "ph_auto_mode", v)) phAutoMode = v;
    }

    memset(jsonBuf, 0, sizeof(jsonBuf));
    truncated = false;
    if (cfgSvc_->toJsonModule(cfgSvc_->ctx,
                              kPoolLogicChlorineModule,
                              jsonBuf,
                              sizeof(jsonBuf),
                              &truncated)) {
        if (findJsonBool_(jsonBuf, "dis_auto_mode", v)) orpAutoMode = v;
    }
    return true;
}

bool HMIModule::readPidSetpoints_(float& phSetpoint, float& orpSetpoint) const
{
    phSetpoint = 0.0f;
    orpSetpoint = 0.0f;
    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return false;

    char jsonBuf[kPoolLogicModeJsonBufSize]{};
    bool truncated = false;
    if (!cfgSvc_->toJsonModule(cfgSvc_->ctx,
                               kPoolLogicPhModule,
                               jsonBuf,
                               sizeof(jsonBuf),
                               &truncated)) {
        return false;
    }

    bool found = false;
    float value = 0.0f;
    if (findJsonFloat_(jsonBuf, "ph_setpoint", value)) {
        phSetpoint = value;
        found = true;
    }

    memset(jsonBuf, 0, sizeof(jsonBuf));
    truncated = false;
    if (cfgSvc_->toJsonModule(cfgSvc_->ctx,
                              kPoolLogicChlorineModule,
                              jsonBuf,
                              sizeof(jsonBuf),
                              &truncated) &&
        findJsonFloat_(jsonBuf, "dis_setpoint", value)) {
        orpSetpoint = value;
        found = true;
    }
    return found;
}

bool HMIModule::readPoolDeviceActualOn_(uint8_t slot, bool& on) const
{
    on = false;
    if (!dsSvc_ || !dsSvc_->store) return false;
    PoolDeviceRuntimeStateEntry state{};
    if (!poolDeviceRuntimeState(*dsSvc_->store, slot, state)) return false;
    on = state.actualOn;
    return true;
}

bool HMIModule::isAlarmActive_(AlarmId id) const
{
    return alarmSvc_ && alarmSvc_->isActive && alarmSvc_->isActive(alarmSvc_->ctx, id);
}

bool HMIModule::isWaterLevelLow_() const
{
    if (isAlarmActive_(AlarmId::PoolWaterLevelLow)) return true;
    if (!ioSvc_ || !ioSvc_->readDigital) return false;
    if (poolLevelIoId_ == IO_ID_INVALID) return false;

    uint8_t levelLow = 0U;
    if (ioSvc_->readDigital(ioSvc_->ctx, poolLevelIoId_, &levelLow, nullptr, nullptr) != IO_OK) return false;
    return levelLow != 0U;
}

uint32_t HMIModule::buildHomeStateBits_() const
{
    uint32_t bits = 0U;
    PoolLogicModeFlags modes{};

    if (dsSvc_ && dsSvc_->store) {
        PoolDeviceRuntimeStateEntry state{};
        if (poolDeviceRuntimeState(*dsSvc_->store, filtrationDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_FILTER_PUMP_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, phPumpDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_PH_PUMP_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, orpPumpDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_ORP_PUMP_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, robotDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_ROBOT_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, lightsDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_LIGHTS_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, heaterDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_HEATER_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, fillingDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_FILLING_ON);
        }
        if (isNetworkConnected_(dsSvc_)) bits |= (1UL << HMI_HOME_STATE_WIFI_READY);
        if (mqttReady(*dsSvc_->store)) bits |= (1UL << HMI_HOME_STATE_MQTT_READY);
    }

    (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
    if (modes.autoMode) bits |= (1UL << HMI_HOME_STATE_AUTO_MODE);
    if (modes.phAutoMode) bits |= (1UL << HMI_HOME_STATE_PH_AUTO_MODE);
    if (modes.orpAutoMode) bits |= (1UL << HMI_HOME_STATE_ORP_AUTO_MODE);
    if (modes.winterMode) bits |= (1UL << HMI_HOME_STATE_WINTER_MODE);
    return bits;
}

uint32_t HMIModule::buildHomeAlarmBits_() const
{
    uint32_t bits = 0U;
    if (isWaterLevelLow_()) bits |= (1UL << HMI_HOME_ALARM_WATER_LEVEL_LOW);
    if (isAlarmActive_(AlarmId::PoolPhTankLow)) bits |= (1UL << HMI_HOME_ALARM_PH_TANK_LOW);
    if (isAlarmActive_(AlarmId::PoolChlorineTankLow)) bits |= (1UL << HMI_HOME_ALARM_CHLORINE_TANK_LOW);
    if (isAlarmActive_(AlarmId::PoolPhPumpMaxUptime)) bits |= (1UL << HMI_HOME_ALARM_PH_PUMP_RUNTIME);
    if (isAlarmActive_(AlarmId::PoolChlorinePumpMaxUptime)) bits |= (1UL << HMI_HOME_ALARM_ORP_PUMP_RUNTIME);
    if (isAlarmActive_(AlarmId::PoolPsiLow) || isAlarmActive_(AlarmId::PoolPsiHigh)) {
        bits |= (1UL << HMI_HOME_ALARM_PSI);
    }
    return bits;
}

bool HMIModule::publishHomeText_(HmiHomeTextField field)
{
    if (!driver_) return false;

    IoId ioId = IO_ID_INVALID;
    uint8_t runtimeIndex = kInvalidRuntimeIndex;
    char value[32]{};
    if (field == HmiHomeTextField::ErrorMessage) {
        snprintf(value, sizeof(value), "%s", homeErrorMessage_);
        return driver_->publishHomeText(field, value);
    }
    if (field == HmiHomeTextField::Date) {
        return true;
    }
    if (field == HmiHomeTextField::Time ||
        field == HmiHomeTextField::DayText ||
        field == HmiHomeTextField::MonthText) {
        struct tm local{};
        bool hasTime = timeSvc_ &&
                       timeSvc_->isSynced &&
                       timeSvc_->epoch &&
                       timeSvc_->isSynced(timeSvc_->ctx);
        if (hasTime) {
            const time_t epoch = (time_t)timeSvc_->epoch(timeSvc_->ctx);
            hasTime = localtime_r(&epoch, &local) != nullptr;
        }

        if (field == HmiHomeTextField::Time) {
            if (hasTime) {
                snprintf(value, sizeof(value), "%02d:%02d", local.tm_hour, local.tm_min);
            } else {
                snprintf(value, sizeof(value), "--:--");
            }
        } else if (field == HmiHomeTextField::DayText) {
            if (hasTime && local.tm_wday >= 0 && local.tm_wday < 7) {
                snprintf(value, sizeof(value), "%s", kDayNamesFr[local.tm_wday]);
            } else {
                snprintf(value, sizeof(value), "--");
            }
        } else {
            if (hasTime && local.tm_mon >= 0 && local.tm_mon < 12) {
                snprintf(value, sizeof(value), "%s", kMonthNamesFr[local.tm_mon]);
            } else {
                snprintf(value, sizeof(value), "----");
            }
        }
        return driver_->publishHomeText(field, value);
    }

    switch (field) {
        case HmiHomeTextField::WaterTemp:
            ioId = waterTempIoId_;
            runtimeIndex = waterTempRuntimeIndex_;
            break;
        case HmiHomeTextField::AirTemp:
            ioId = airTempIoId_;
            runtimeIndex = airTempRuntimeIndex_;
            break;
        case HmiHomeTextField::Ph:
            ioId = phIoId_;
            runtimeIndex = phRuntimeIndex_;
            break;
        case HmiHomeTextField::Orp:
            ioId = orpIoId_;
            runtimeIndex = orpRuntimeIndex_;
            break;
        default:
            return false;
    }

    bool hasValue = false;
    float rawValue = 0.0f;
    if (ioSvc_ && ioSvc_->readAnalog && ioId != IO_ID_INVALID) {
        hasValue = (ioSvc_->readAnalog(ioSvc_->ctx, ioId, &rawValue, nullptr, nullptr) == IO_OK);
    }
    if (!hasValue && dsSvc_ && dsSvc_->store && runtimeIndex != kInvalidRuntimeIndex) {
        hasValue = ioEndpointFloat(*dsSvc_->store, runtimeIndex, rawValue);
    }

    switch (field) {
        case HmiHomeTextField::WaterTemp:
            if (hasValue) snprintf(value, sizeof(value), "%.1f%s", (double)rawValue, kNextionDegreeC);
            else snprintf(value, sizeof(value), "--.-%s", kNextionDegreeC);
            break;
        case HmiHomeTextField::AirTemp:
            if (hasValue) snprintf(value, sizeof(value), "%.1f%s", (double)rawValue, kNextionDegreeC);
            else snprintf(value, sizeof(value), "--.-%s", kNextionDegreeC);
            break;
        case HmiHomeTextField::Ph:
            if (hasValue) snprintf(value, sizeof(value), "%.2f", (double)rawValue);
            else snprintf(value, sizeof(value), "--.--");
            break;
        case HmiHomeTextField::Orp:
            if (hasValue) snprintf(value, sizeof(value), "%.0f", (double)rawValue);
            else snprintf(value, sizeof(value), "---");
            break;
        default:
            return false;
    }

    return driver_->publishHomeText(field, value);
}

bool HMIModule::publishHomeGaugePercent_(HmiHomeGaugeField field)
{
    if (!driver_) return false;

    IoId ioId = IO_ID_INVALID;
    uint8_t runtimeIndex = kInvalidRuntimeIndex;
    float setpoint = 0.0f;
    float phSetpoint = 0.0f;
    float orpSetpoint = 0.0f;
    (void)readPidSetpoints_(phSetpoint, orpSetpoint);

    switch (field) {
        case HmiHomeGaugeField::PhPercent:
            ioId = phIoId_;
            runtimeIndex = phRuntimeIndex_;
            setpoint = phSetpoint;
            break;
        case HmiHomeGaugeField::OrpPercent:
            ioId = orpIoId_;
            runtimeIndex = orpRuntimeIndex_;
            setpoint = orpSetpoint;
            break;
        default:
            return false;
    }

    bool hasValue = false;
    float rawValue = 0.0f;
    if (ioSvc_ && ioSvc_->readAnalog && ioId != IO_ID_INVALID) {
        hasValue = (ioSvc_->readAnalog(ioSvc_->ctx, ioId, &rawValue, nullptr, nullptr) == IO_OK);
    }
    if (!hasValue && dsSvc_ && dsSvc_->store && runtimeIndex != kInvalidRuntimeIndex) {
        hasValue = ioEndpointFloat(*dsSvc_->store, runtimeIndex, rawValue);
    }

    const uint16_t percent = hasValue ? gaugePercentFromSetpoint_(rawValue, setpoint) : 140U;
    return driver_->publishHomeGaugePercent(field, percent);
}

bool HMIModule::publishHomeStateBits_()
{
    return driver_ && driver_->publishHomeStateBits(buildHomeStateBits_());
}

bool HMIModule::publishHomeAlarmBits_()
{
    return driver_ && driver_->publishHomeAlarmBits(buildHomeAlarmBits_());
}

bool HMIModule::validateDriverDisplayVersion_(bool requireDetection)
{
    if (!driver_) return true;
    if (!driver_->hasDisplayVersion()) {
        if (!requireDetection) return true;
        LOGW("Ecran Nextion version non detectee. Affichage Nextion desactive.");
        nextionDisabledByVersion_ = true;
        applyOutputConfig_();
        return false;
    }

    const char* version = driver_->displayVersion();
    if (nextionVersionDetected_ && strcmp(nextionVersion_, version ? version : "") == 0) {
        return isSupportedNextionDisplayVersion_(version);
    }

    nextionVersionDetected_ = true;
    strncpy(nextionVersion_, version ? version : "", sizeof(nextionVersion_) - 1U);
    nextionVersion_[sizeof(nextionVersion_) - 1U] = '\0';
    LOGI("Ecran Nextion version %s detecte driver=%s.",
         nextionVersion_,
         driver_->driverId());

    if (!isSupportedNextionDisplayVersion_(nextionVersion_)) {
        LOGW("Ecran Nextion version %s non supportee (supportee %s). Affichage Nextion desactive.",
             nextionVersion_,
             kNextionDisplayVersionExpected);
        nextionDisabledByVersion_ = true;
        applyOutputConfig_();
        return false;
    }

    if (driver_->isLegacyV2()) {
        LOGI("Ecran Nextion V2 detecte: rendu Home aligne V1 avec jauges percent.");
    }
    viewDirty_ = true;
    rtcPushPending_ = true;
    lastRtcPushAttemptMs_ = 0;
    queueHomePublish_(kHomePublishAll);
    return true;
}

bool HMIModule::readNextionRtcAndSetTime_()
{
    if (!driver_ || !timeSvc_ || !timeSvc_->setExternalEpoch) return false;

    HmiRtcDateTime rtc{};
    const uint16_t timeoutMs =
        (driver_ == static_cast<IHmiDriver*>(&remoteUdp_)) ? kRemoteNextionRtcReadTimeoutMs : kLocalNextionRtcReadTimeoutMs;
    if (!driver_->readRtc(rtc, timeoutMs)) {
        LOGW("HMI Nextion RTC read failed");
        return false;
    }

    uint64_t epoch = 0ULL;
    if (!rtcDateTimeToEpoch_(rtc, epoch)) {
        LOGW("HMI Nextion RTC invalid %u-%02u-%02u %02u:%02u:%02u",
             (unsigned)rtc.year,
             (unsigned)rtc.month,
             (unsigned)rtc.day,
             (unsigned)rtc.hour,
             (unsigned)rtc.minute,
             (unsigned)rtc.second);
        return false;
    }

    if (!timeSvc_->setExternalEpoch(timeSvc_->ctx, epoch)) {
        LOGW("HMI external RTC import rejected epoch=%llu", (unsigned long long)epoch);
        return false;
    }

    LOGI("HMI imported Nextion RTC %u-%02u-%02u %02u:%02u:%02u",
         (unsigned)rtc.year,
         (unsigned)rtc.month,
         (unsigned)rtc.day,
         (unsigned)rtc.hour,
         (unsigned)rtc.minute,
         (unsigned)rtc.second);
    return true;
}

bool HMIModule::pushEspTimeToNextionRtc_()
{
    if (!driver_ || !timeSvc_ || !timeSvc_->epoch) return false;

    const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
    HmiRtcDateTime rtc{};
    if (!epochToRtcDateTime_(epoch, rtc)) return false;

    if (!driver_->writeRtc(rtc)) {
        LOGW("HMI Nextion RTC write failed");
        return false;
    }

    LOGI("HMI pushed ESP time to Nextion RTC %u-%02u-%02u %02u:%02u:%02u",
         (unsigned)rtc.year,
         (unsigned)rtc.month,
         (unsigned)rtc.day,
         (unsigned)rtc.hour,
         (unsigned)rtc.minute,
         (unsigned)rtc.second);
    return true;
}

bool HMIModule::readRtcSvc_(HmiRtcDateTime* out, uint16_t timeoutMs)
{
    if (!out || !driver_ || !driverReady_) return false;
    return driver_->readRtc(*out, timeoutMs);
}

bool HMIModule::writeRtcSvc_(const HmiRtcDateTime* value)
{
    if (!value || !driver_ || !driverReady_) return false;
    return driver_->writeRtc(*value);
}

void HMIModule::resetClockPublishStamps_()
{
    lastClockMinuteStamp_ = kInvalidClockStamp;
    lastClockDayStamp_ = kInvalidClockStamp;
}

void HMIModule::serviceRtcBridge_(uint32_t nowMs)
{
    (void)nowMs;
}

void HMIModule::queueHomePublish_(uint32_t mask)
{
    if (mask == 0U) return;
    uint32_t before = 0U;
    uint32_t after = 0U;
    portENTER_CRITICAL(&homePublishMux_);
    before = homePublishMask_;
    homePublishMask_ |= mask;
    after = homePublishMask_;
    portEXIT_CRITICAL(&homePublishMux_);
    if (after == before) {
        static uint32_t noChangeCount = 0U;
        static uint32_t lastNoChangeLogMs = 0U;
        ++noChangeCount;
        const uint32_t now = millis();
        if (lastNoChangeLogMs == 0U ||
            (uint32_t)(now - lastNoChangeLogMs) >= 5000U) {
            LOGD("HMI Home queue unchanged mask=0x%03lx pending=0x%03lx repeats=%lu home=%d cfg=%d alarm=%d",
                 (unsigned long)mask,
                 (unsigned long)after,
                 (unsigned long)noChangeCount,
                 homePageVisible_ ? 1 : 0,
                 configMenuActive_ ? 1 : 0,
                 alarmPageActive_ ? 1 : 0);
            noChangeCount = 0U;
            lastNoChangeLogMs = now;
        }
        return;
    }
    LOGD("HMI Home queue add=0x%03lx pending:0x%03lx->0x%03lx home=%d cfg=%d alarm=%d",
         (unsigned long)mask,
         (unsigned long)before,
         (unsigned long)after,
         homePageVisible_ ? 1 : 0,
         configMenuActive_ ? 1 : 0,
         alarmPageActive_ ? 1 : 0);
}

void HMIModule::queueClockPublishIfDue_(uint32_t nowMs)
{
    if (!driver_ || !driverReady_) return;
    if ((uint32_t)(nowMs - lastClockCheckMs_) < kClockPublishCheckMs) return;
    lastClockCheckMs_ = nowMs;

    const bool synced = timeSvc_ &&
                        timeSvc_->isSynced &&
                        timeSvc_->epoch &&
                        timeSvc_->isSynced(timeSvc_->ctx);
    if (!synced) {
        if (lastClockMinuteStamp_ != kInvalidClockStamp || lastClockDayStamp_ != kInvalidClockStamp) {
            lastClockMinuteStamp_ = kInvalidClockStamp;
            lastClockDayStamp_ = kInvalidClockStamp;
            queueHomePublish_(kHomePublishTime | kHomePublishDate);
        }
        return;
    }

    const time_t epoch = (time_t)timeSvc_->epoch(timeSvc_->ctx);
    struct tm local{};
    if (!localtime_r(&epoch, &local)) return;

    const uint32_t year = (uint32_t)(local.tm_year + 1900);
    const uint32_t minuteStamp = (year * 1000000UL) +
                                 ((uint32_t)local.tm_yday * 1440UL) +
                                 ((uint32_t)local.tm_hour * 60UL) +
                                 (uint32_t)local.tm_min;
    const uint32_t dayStamp = (year * 1000UL) + (uint32_t)local.tm_yday;

    uint32_t mask = 0U;
    if (minuteStamp != lastClockMinuteStamp_) {
        lastClockMinuteStamp_ = minuteStamp;
        mask |= kHomePublishTime;
    }
    if (dayStamp != lastClockDayStamp_) {
        lastClockDayStamp_ = dayStamp;
        mask |= kHomePublishDate;
    }
    queueHomePublish_(mask);
}

void HMIModule::flushHomePublish_()
{
    if (!driver_ || !driverReady_) return;
    if (!homePageVisible_) return;

    uint32_t pending = 0U;
    portENTER_CRITICAL(&homePublishMux_);
    pending = homePublishMask_;
    portEXIT_CRITICAL(&homePublishMux_);
    if (pending == 0U) return;

    uint32_t sent = 0U;
    if ((pending & kHomePublishWaterTemp) != 0U && publishHomeText_(HmiHomeTextField::WaterTemp)) {
        sent |= kHomePublishWaterTemp;
    }
    if ((pending & kHomePublishAirTemp) != 0U && publishHomeText_(HmiHomeTextField::AirTemp)) {
        sent |= kHomePublishAirTemp;
    }
    if ((pending & kHomePublishPh) != 0U && publishHomeText_(HmiHomeTextField::Ph)) {
        sent |= kHomePublishPh;
    }
    if ((pending & kHomePublishOrp) != 0U && publishHomeText_(HmiHomeTextField::Orp)) {
        sent |= kHomePublishOrp;
    }
    if ((pending & kHomePublishPhGauge) != 0U &&
        publishHomeGaugePercent_(HmiHomeGaugeField::PhPercent)) {
        sent |= kHomePublishPhGauge;
    }
    if ((pending & kHomePublishOrpGauge) != 0U &&
        publishHomeGaugePercent_(HmiHomeGaugeField::OrpPercent)) {
        sent |= kHomePublishOrpGauge;
    }
    if ((pending & kHomePublishStateBits) != 0U && publishHomeStateBits_()) {
        sent |= kHomePublishStateBits;
    }
    if ((pending & kHomePublishAlarmBits) != 0U && publishHomeAlarmBits_()) {
        sent |= kHomePublishAlarmBits;
    }
    if ((pending & kHomePublishTime) != 0U && publishHomeText_(HmiHomeTextField::Time)) {
        sent |= kHomePublishTime;
    }
    if ((pending & kHomePublishDate) != 0U &&
        publishHomeText_(HmiHomeTextField::DayText) &&
        publishHomeText_(HmiHomeTextField::MonthText)) {
        sent |= kHomePublishDate;
    }
    if ((pending & kHomePublishErrorMessage) != 0U && publishHomeText_(HmiHomeTextField::ErrorMessage)) {
        sent |= kHomePublishErrorMessage;
    }
    if ((pending & kHomePublishPsi) != 0U) {
        sent |= kHomePublishPsi;
    }

    if (sent == 0U) {
        LOGD("HMI Home flush no-progress pending=0x%03lx sleep=%d home=%d driver=%s",
             (unsigned long)pending,
             isDisplaySleeping_() ? 1 : 0,
             homePageVisible_ ? 1 : 0,
             driver_ ? driver_->driverId() : "none");
        return;
    }

    portENTER_CRITICAL(&homePublishMux_);
    homePublishMask_ &= ~sent;
    const uint32_t remaining = homePublishMask_;
    portEXIT_CRITICAL(&homePublishMux_);
    LOGD("HMI Home flush sent=0x%03lx pending=0x%03lx remain=0x%03lx",
         (unsigned long)sent,
         (unsigned long)pending,
         (unsigned long)remaining);
}

void HMIModule::applyWs2812AutoWifiProfile_()
{
    updateHmiLedConditions_();
}

void HMIModule::updateHmiLedConditions_()
{
    if (!ws2812AutoWifiMode_) return;

    bool alarmActive = false;
    if (alarmSvc_ && alarmSvc_->activeCount) {
        alarmActive = (alarmSvc_->activeCount(alarmSvc_->ctx) > 0U);
    }

    const bool networkExpected = wifiNetworkExpected_ || ethernetNetworkExpected_;
    const bool networkLost = networkExpected && !isNetworkConnected_(dsSvc_);
    const bool domainSlotError = hasDomainSlotError_();

    ws2812StatusLed_.setCondition(HmiLedCondition::AlarmActive, alarmActive);
    ws2812StatusLed_.setCondition(HmiLedCondition::DomainSlotError, domainSlotError);
    ws2812StatusLed_.setCondition(HmiLedCondition::NetworkLost, networkLost);
    ws2812AutoWifiApplied_ = true;
}

bool HMIModule::hasDomainSlotError_() const
{
    return domainStatusSvc_ &&
           domainStatusSvc_->hasDomainSlotError &&
           domainStatusSvc_->hasDomainSlotError(domainStatusSvc_->ctx);
}

bool HMIModule::firstDomainSlotError_(DomainSlotStatus& outStatus) const
{
    outStatus = DomainSlotStatus{};
    return domainStatusSvc_ &&
           domainStatusSvc_->firstError &&
           domainStatusSvc_->firstError(domainStatusSvc_->ctx, &outStatus);
}

const char* HMIModule::hmiLedDisplayStateName_(HmiLedDisplayState state)
{
    switch (state) {
        case HmiLedDisplayState::AlarmActive: return "AlarmActive";
        case HmiLedDisplayState::DomainSlotError: return "DomainSlotError";
        case HmiLedDisplayState::CaptivePortalActive: return "CaptivePortalActive";
        case HmiLedDisplayState::OtaInProgress: return "OtaInProgress";
        case HmiLedDisplayState::NetworkLost: return "NetworkLost";
        case HmiLedDisplayState::Booting: return "Booting";
        case HmiLedDisplayState::Normal: return "Normal";
        default: return "Unknown";
    }
}

void HMIModule::logHmiLedDebug_(uint32_t nowMs)
{
    if ((uint32_t)(nowMs - lastHmiLedDebugMs_) < kHmiLedDebugPeriodMs) return;
    lastHmiLedDebugMs_ = nowMs;

    const HmiLedDisplayState current = ws2812StatusLed_.currentDisplayState();
    const HmiLedDisplayState target = ws2812StatusLed_.targetDisplayState();
    DomainSlotStatus domainError{};
    const bool hasDomainError = firstDomainSlotError_(domainError);
    char ioRef[12]{};
    if (hasDomainError && domainError.ioId != IO_ID_INVALID) {
        (void)formatSensorIoRef_(domainError.ioId, ioRef, sizeof(ioRef));
    } else {
        snprintf(ioRef, sizeof(ioRef), "none");
    }
    LOGD("Waveshare HMI LED enabled=%u ready=%u state=%s target=%s transition=%u domain_slot=%u io=%s error=%s block=%u",
         ws2812StatusLed_.isEnabled() ? 1U : 0U,
         ws2812StatusLed_.isReady() ? 1U : 0U,
         hmiLedDisplayStateName_(current),
         hmiLedDisplayStateName_(target),
         ws2812StatusLed_.isTransitionActive() ? 1U : 0U,
         hasDomainError ? (unsigned)domainError.domainSlot : (unsigned)DOMAIN_SLOT_INVALID,
         ioRef,
         hasDomainError ? domainSlotStatusReasonName(domainError.reason) : "",
         hasDomainError && domainError.hasPoolDevice ? (unsigned)domainError.poolMeta.blockReason : 0U);
}

bool HMIModule::ensureFrontLedPanelReady_()
{
    if (!kFrontLedsSupported) return false;
    if (frontLedPanel_.isReady()) return true;
    if (!i2cBus_) return false;

    if (!i2cBus_->beginOk()) {
        i2cBus_->begin(frontLedI2cSda_, frontLedI2cScl_, frontLedI2cFrequencyHz_);
    }
    if (!i2cBus_->beginOk()) return false;
    if (!frontLedPanel_.begin(*i2cBus_)) return false;

    ledMaskValid_ = false;
    LOGI("PCF8574A LED panel ready addr=0x%02X active_low=true",
         (unsigned)Pcf8574LedPanelDriver::Address);
    return true;
}

void HMIModule::applyFrontLedPanelConfig_()
{
    if (!kFrontLedsSupported) return;
    if (!ensureFrontLedPanelReady_()) {
        ledMaskValid_ = false;
        LOGW("PCF8574A LED panel unavailable addr=0x%02X",
             (unsigned)Pcf8574LedPanelDriver::Address);
        return;
    }
    if (!frontLedPanel_.setEnabled(cfgData_.ledsEnabled)) {
        ledMaskValid_ = false;
        LOGW("PCF8574A LED panel enable update failed");
        return;
    }
    ledMaskValid_ = false;
}

void HMIModule::applyLedMask_(bool force)
{
    if (!kFrontLedsSupported) return;
    if (!cfgData_.ledsEnabled) {
        if (frontLedPanel_.isReady()) (void)frontLedPanel_.setEnabled(false);
        ledMaskValid_ = false;
        return;
    }
    if (!ensureFrontLedPanelReady_()) return;
    if (!frontLedPanel_.isEnabled() && !frontLedPanel_.setEnabled(true)) return;

    PoolLogicModeFlags modes{};
    const bool wifiConnected = wifiSvc_ &&
                               wifiSvc_->isConnected &&
                               wifiSvc_->isConnected(wifiSvc_->ctx);
    (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
    const bool waterLevelLow = isWaterLevelLow_();
    const bool psiAlarm = isAlarmActive_(AlarmId::PoolPsiLow) || isAlarmActive_(AlarmId::PoolPsiHigh);
    const bool phTankLowAlarm = isAlarmActive_(AlarmId::PoolPhTankLow);
    const bool chlorineTankLowAlarm = isAlarmActive_(AlarmId::PoolChlorineTankLow);
    const bool phPumpRuntimeAlarm = isAlarmActive_(AlarmId::PoolPhPumpMaxUptime);
    const bool chlorinePumpRuntimeAlarm = isAlarmActive_(AlarmId::PoolChlorinePumpMaxUptime);

    uint8_t mask = 0U;
    if (wifiConnected) {
        mask |= (uint8_t)(1U << kLedBitWifiConnected);
    }

    // Direct mapping: ledPage_=1 drives page 1, ledPage_=2 drives page 2.
    const bool page2 = (ledPage_ == 2U);
    if (!page2) mask |= (uint8_t)(1U << kLedBitPageSelect);

    if (!page2) {
        // Page 1:
        // LED3=pH auto, LED4=chlorine auto, LED5=chlorine pump uptime,
        // LED6=pH pump uptime, LED7=chlorine level, LED8=pH level.
        if (modes.phAutoMode) mask |= (uint8_t)(1U << kLedBitModeA);
        if (modes.orpAutoMode) mask |= (uint8_t)(1U << kLedBitModeB);
        if (chlorinePumpRuntimeAlarm) mask |= (uint8_t)(1U << kLedBitAlarmA);
        if (phPumpRuntimeAlarm) mask |= (uint8_t)(1U << kLedBitAlarmB);
        if (chlorineTankLowAlarm) mask |= (uint8_t)(1U << kLedBitAlarmC);
        if (phTankLowAlarm) mask |= (uint8_t)(1U << kLedBitAlarmD);
    } else {
        // Page 2:
        // LED3=automatic regulation, LED4=winter mode, LED5=PSI alarm,
        // LED6=water level alarm, LED7/LED8 unused.
        if (modes.autoMode) mask |= (uint8_t)(1U << kLedBitModeA);
        if (modes.winterMode) mask |= (uint8_t)(1U << kLedBitModeB);
        if (psiAlarm) mask |= (uint8_t)(1U << kLedBitAlarmA);
        if (waterLevelLow) mask |= (uint8_t)(1U << kLedBitAlarmB);
    }

    if (!force && ledMaskValid_ && ledMaskLast_ == mask) return;
    if (frontLedPanel_.writeLogicalMask(mask)) {
        ledMaskLast_ = mask;
        ledMaskValid_ = true;
    }
}

bool HMIModule::refreshCurrentModule_()
{
#if !FLOW_HMI_CONFIG_MENU_ENABLED
    return false;
#else
    if (!configMenuReady_) return false;
    const uint8_t prevPage = menu_.pageIndex();
    if (!menu_.refreshCurrent()) return false;
    while (menu_.pageIndex() < prevPage && menu_.nextPage()) {
    }
    return true;
#endif
}

bool HMIModule::ensureConfigMenuReady_()
{
#if FLOW_HMI_CONFIG_MENU_ENABLED
    if (configMenuReady_) return true;
    if (!cfgSvc_) return false;

    const bool okMenu = menu_.begin(cfgSvc_);
    if (!okMenu) {
        LOGE("Config menu init failed free_heap=%u", (unsigned)xPortGetFreeHeapSize());
        return false;
    }

    menu_.setHints(kHints, (uint8_t)(sizeof(kHints) / sizeof(kHints[0])));
    configMenuReady_ = true;
    LOGI("Config menu ready mode=stateless rows_per_page=%u row_cap=%u module_cap=%u free_heap=%u",
         (unsigned)ConfigMenuModel::RowsPerPage,
         (unsigned)ConfigMenuModel::MaxRows,
         (unsigned)ConfigMenuModel::MaxModules,
         (unsigned)xPortGetFreeHeapSize());
    return true;
#else
    return false;
#endif
}

void HMIModule::onEvent_(const Event& e)
{
    bool ledDirty = false;
    uint32_t homePublishMask = 0U;

    if (e.id == EventId::ConfigChanged && e.payload && e.len >= sizeof(ConfigChangedPayload)) {
        const ConfigChangedPayload* p = static_cast<const ConfigChangedPayload*>(e.payload);
#if FLOW_HMI_CONFIG_MENU_ENABLED
        if (configMenuReady_) {
            if (configMenuActive_) {
                viewDirty_ = true;
            }
        }
#endif
        const bool poolLogicChangedByName =
            p->module[0] && strncmp(p->module, "poollogic/", 10) == 0;
        const bool poolLogicBindingsChanged =
            strcmp(p->module, kPoolLogicSensorsModule) == 0 ||
            strcmp(p->module, kPoolLogicDeviceModule) == 0;
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic && !poolLogicChangedByName) {
            LOGW("HMI ignored non-branched PoolLogic config module=%s key=%s",
                 p->module[0] ? p->module : "<empty>",
                 p->nvsKey[0] ? p->nvsKey : "<none>");
        }
        if (poolLogicBindingsChanged) {
            homeBindingsRefreshPending_ = true;
        }
        if (poolLogicChangedByName) {
            ledDirty = true;
            homePublishMask |= kHomePublishAll;
        }
        if (p->module[0] && strncmp(p->module, kHmiModulePrefix, strlen(kHmiModulePrefix)) == 0) {
            if (strcmp(p->nvsKey, NvsKeys::Hmi::NextionMotionIoId) == 0) {
                nextionMotionInputReady_ = false;
                nextionMotionActiveLast_ = false;
                nextionMotionReadErrorLogged_ = false;
                lastNextionMotionWakeAttemptMs_ = 0U;
            }
            applyOutputConfig_();
            applyFrontLedPanelConfig_();
            ledDirty = cfgData_.ledsEnabled;
            if (cfgData_.nextionEnabled || cfgData_.remoteUdpEnabled) {
                homePublishMask |= kHomePublishAll;
            }
        }
        if ((p->module[0] && strcmp(p->module, "mqtt") == 0) ||
            strcmp(p->nvsKey, NvsKeys::Mqtt::Enabled) == 0) {
            homePublishMask |= kHomePublishStateBits;
        }
        if ((p->module[0] && (strcmp(p->module, "wifi") == 0 || strcmp(p->module, "ethernet") == 0))) {
            refreshNetworkExpectations_();
        }
        if ((p->moduleId == (uint8_t)ConfigModuleId::System &&
             strcmp(p->nvsKey, NvsKeys::System::Language) == 0) ||
            (strcmp(p->module, "system") == 0 &&
             strcmp(p->nvsKey, NvsKeys::System::Language) == 0)) {
            refreshLocale_();
            markAlarmViewDirty_();
        }
    } else if (e.id == EventId::DataChanged && e.payload && e.len >= sizeof(DataChangedPayload)) {
        const DataChangedPayload* p = static_cast<const DataChangedPayload*>(e.payload);
        if (p->id == DATAKEY_NETWORK_READY || p->id == DATAKEY_NETWORK_IP || p->id == DATAKEY_MQTT_READY) {
            ledDirty = true;
            homePublishMask |= kHomePublishStateBits;
        } else if (p->id == DATAKEY_TIME_READY) {
            homePublishMask |= kHomePublishTime | kHomePublishDate;
            rtcPushPending_ = true;
            lastRtcPushAttemptMs_ = 0;
        } else if (p->id >= DATAKEY_IO_BASE &&
                   p->id < (DataKey)(DATAKEY_IO_BASE + IO_MAX_ENDPOINTS)) {
            ledDirty = true;
            homePublishMask |= kHomePublishWaterTemp |
                               kHomePublishAirTemp |
                               kHomePublishPh |
                               kHomePublishOrp |
                               kHomePublishPhGauge |
                               kHomePublishOrpGauge |
                               kHomePublishPsi |
                               kHomePublishAlarmBits;
        } else if (p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + filtrationDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + phPumpDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + orpPumpDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + robotDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + lightsDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + heaterDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + fillingDeviceSlot_)) {
            homePublishMask |= kHomePublishStateBits;
        }
    } else if ((e.id == EventId::AlarmRaised || e.id == EventId::AlarmCleared) &&
               e.payload &&
               e.len >= sizeof(AlarmPayload)) {
        const AlarmPayload* p = static_cast<const AlarmPayload*>(e.payload);
        const AlarmId id = (AlarmId)p->alarmId;
        markAlarmViewDirty_();
        if (id == AlarmId::PoolPhTankLow ||
            id == AlarmId::PoolChlorineTankLow ||
            id == AlarmId::PoolPhPumpMaxUptime ||
            id == AlarmId::PoolChlorinePumpMaxUptime ||
            id == AlarmId::PoolWaterLevelLow ||
            id == AlarmId::PoolPsiLow ||
            id == AlarmId::PoolPsiHigh) {
            ledDirty = true;
            homePublishMask |= kHomePublishAlarmBits;
        }
    }

    if (ledDirty) {
        applyLedMask_();
    }
    queueHomePublish_(homePublishMask);
}

void HMIModule::handleDriverEvent_(const HmiEvent& e)
{
    if (e.type == HmiEventType::DisplaySleep ||
        e.type == HmiEventType::DisplayWake ||
        e.type == HmiEventType::Home ||
        e.type == HmiEventType::Page ||
        e.type == HmiEventType::ConfigEnter ||
        e.type == HmiEventType::ConfigExit ||
        e.type == HmiEventType::Command) {
        LOGD("HMI event type=%s page=%u row=%u val=%u ctx=%lu home=%d cfg=%d alarm=%d",
             hmiEventTypeName_(e.type),
             (unsigned)e.pageId,
             (unsigned)e.row,
             (unsigned)e.value,
             (unsigned long)e.contextRef,
             homePageVisible_ ? 1 : 0,
             configMenuActive_ ? 1 : 0,
             alarmPageActive_ ? 1 : 0);
    }

    const bool configInputEvent = e.type == HmiEventType::Back ||
                                  e.type == HmiEventType::Validate ||
                                  e.type == HmiEventType::NextPage ||
                                  e.type == HmiEventType::PrevPage ||
                                  e.type == HmiEventType::RowActivate ||
                                  e.type == HmiEventType::RowToggle ||
                                  e.type == HmiEventType::RowCycle ||
                                  e.type == HmiEventType::RowSetText ||
                                  e.type == HmiEventType::RowSetSlider ||
                                  e.type == HmiEventType::RowEdit;

    if (e.type == HmiEventType::Command) {
        if (e.command == HmiCommandId::HomeConfigOpen) {
            return;
        }
        if (executeHmiCommand_(e.command, e.value)) {
            setHomeErrorMessage_("", true);
        }
        return;
    }

    if (e.type == HmiEventType::DisplaySleep) {
        LOGI("HMI display reported sleep");
        return;
    }
    if (e.type == HmiEventType::DisplayWake) {
        LOGI("HMI display reported wake");
        if (alarmPageActive_ || configMenuActive_) {
            viewDirty_ = true;
        } else if (homePageVisible_) {
            resetClockPublishStamps_();
            queueHomePublish_(kHomePublishAll);
        }
        return;
    }

    if (e.type == HmiEventType::Home) {
        homePageVisible_ = true;
        menuSessionActive_ = false;
        menuPageVisible_ = false;
        alarmPageActive_ = false;
        configMenuActive_ = false;
        viewDirty_ = false;
        resetClockPublishStamps_();
        queueHomePublish_(kHomePublishAll);
        LOGI("HMI home page active (event Home)");
    } else if (e.type == HmiEventType::Page) {
        homePageVisible_ = false;
        LOGI("HMI page changed page=%u alarm=%d config=%d",
             (unsigned)e.pageId,
             isAlarmPageId_(e.pageId) ? 1 : 0,
             isConfigPageCode_(e.pageId) ? 1 : 0);
        if (isAlarmPageId_(e.pageId)) {
            alarmPageActive_ = true;
            menuPageVisible_ = false;
            configMenuActive_ = false;
            menuSessionActive_ = false;
            viewDirty_ = true;
            return;
        }
        alarmPageActive_ = false;
        if (!isConfigPageCode_(e.pageId)) {
            menuPageVisible_ = false;
            configMenuActive_ = false;
        }
    }

    if (e.type == HmiEventType::ConfigEnter) {
        LOGI("HMI config enter page=%u ctx=%lu", (unsigned)e.pageId, (unsigned long)e.contextRef);
        if (isAlarmPageId_(e.pageId)) {
            menuSessionActive_ = false;
            menuPageVisible_ = false;
            alarmPageActive_ = true;
            configMenuActive_ = false;
            homePageVisible_ = false;
            viewDirty_ = true;
            return;
        }
        menuSessionActive_ = true;
        menuPageVisible_ = true;
        alarmPageActive_ = false;
        homePageVisible_ = false;
        if (e.contextRef != 0U && restoreConfigContext_(e.contextRef)) {
            configMenuActive_ = true;
            viewDirty_ = true;
            return;
        }
        (void)openConfigHome_();
        return;
    }

    if (e.type == HmiEventType::ConfigExit) {
        LOGI("HMI config exit page=%u", (unsigned)e.pageId);
        if (alarmPageActive_) {
            alarmPageActive_ = false;
            viewDirty_ = false;
            if (isHomePageCode_(e.pageId)) {
                homePageVisible_ = true;
                resetClockPublishStamps_();
                queueHomePublish_(kHomePublishAll);
            }
            return;
        }
        menuPageVisible_ = false;
        configMenuActive_ = false;
        viewDirty_ = false;
        homePageVisible_ = false;
        if (e.pageId == 0xFFU) {
            // Explicit NAV exit: end menu session.
            menuSessionActive_ = false;
        } else if (isHomePageCode_(e.pageId)) {
            menuSessionActive_ = false;
            homePageVisible_ = true;
            resetClockPublishStamps_();
            queueHomePublish_(kHomePublishAll);
        } else {
            // Leaving Config to keyboard/overlay keeps session alive but hides menu refresh.
            menuSessionActive_ = true;
        }
        return;
    }

    if (alarmPageActive_) {
        switch (e.type) {
            case HmiEventType::NextPage:
                (void)nextAlarmPage_();
                return;
            case HmiEventType::PrevPage:
                (void)prevAlarmPage_();
                return;
            case HmiEventType::RowActivate:
                (void)resetAlarmRow_(e.row);
                return;
            default:
                return;
        }
    }

#if !FLOW_HMI_CONFIG_MENU_ENABLED
    {
        if (e.type == HmiEventType::NextPage) {
            ledPage_ = 2U;
            applyLedMask_(true);
        } else if (e.type == HmiEventType::PrevPage ||
                   e.type == HmiEventType::Home ||
                   e.type == HmiEventType::Back) {
            ledPage_ = 1U;
            applyLedMask_(true);
        }
        return;
    }
#else
    if (!configMenuActive_) {
        if (configInputEvent) {
            bool restored = false;
            if (e.contextRef != 0U) {
                restored = restoreConfigContext_(e.contextRef);
            }
            if (!restored && menuSessionActive_) {
                restored = ensureConfigMenuReady_();
                if (restored) {
                    LOGI("HMI config event resumed active session type=%u row=%u",
                         (unsigned)e.type,
                         (unsigned)e.row);
                }
            }
            if (restored) {
                configMenuActive_ = true;
                menuPageVisible_ = true;
                homePageVisible_ = false;
            } else {
                LOGW("HMI config event ignored inactive type=%u row=%u value=%u session=%d ctxRef=%lu",
                     (unsigned)e.type,
                     (unsigned)e.row,
                     (unsigned)e.value,
                     menuSessionActive_ ? 1 : 0,
                     (unsigned long)e.contextRef);
            }
        }
        if (configMenuActive_) {
            LOGI("HMI config event restored inactive menu type=%u row=%u value=%u",
                 (unsigned)e.type,
                 (unsigned)e.row,
                 (unsigned)e.value);
        } else {
        if (e.type == HmiEventType::NextPage) {
            ledPage_ = 2U;
            applyLedMask_(true);
        } else if (e.type == HmiEventType::PrevPage ||
                   e.type == HmiEventType::Home ||
                   e.type == HmiEventType::Back) {
            ledPage_ = 1U;
            applyLedMask_(true);
        }
        return;
        }
    }

    if (configInputEvent) {
        LOGI("HMI config event received type=%u row=%u value=%u",
             (unsigned)e.type,
             (unsigned)e.row,
             (unsigned)e.value);
    }

    bool changed = false;
    switch (e.type) {
        case HmiEventType::Home:
            changed = menu_.home();
            break;
        case HmiEventType::Back:
            if (menu_.isHome()) {
                configMenuActive_ = false;
                viewDirty_ = false;
                return;
            }
            changed = menu_.back();
            break;
        case HmiEventType::Validate: {
            char ack[96]{};
            changed = menu_.validate(ack, sizeof(ack));
            if (!changed) {
                LOGW("Validate failed");
            }
            break;
        }
        case HmiEventType::NextPage:
            changed = menu_.nextPage();
            break;
        case HmiEventType::PrevPage:
            changed = menu_.prevPage();
            break;
        case HmiEventType::RowActivate: {
            changed = menu_.enterRow(e.row);
            if (!changed) {
                ConfigMenuView view{};
                menu_.buildView(view);
                if (e.row < ConfigMenuModel::RowsPerPage && view.rows[e.row].visible) {
                    const ConfigMenuWidget widget = view.rows[e.row].widget;
                    if (widget == ConfigMenuWidget::Switch) changed = menu_.toggleSwitch(e.row);
                    else if (widget == ConfigMenuWidget::Select) changed = menu_.cycleSelect(e.row, 1);
                }
            }
            break;
        }
        case HmiEventType::RowEdit:
            changed = menu_.editRow(e.row);
            break;
        case HmiEventType::RowToggle:
            changed = menu_.toggleSwitch(e.row);
            break;
        case HmiEventType::RowCycle:
            changed = menu_.cycleSelect(e.row, e.direction);
            break;
        case HmiEventType::RowSetText:
            changed = menu_.setText(e.row, e.text);
            if (!changed && strchr(e.text, ',') != nullptr) {
                char normalized[sizeof(e.text)]{};
                size_t p = 0U;
                while (p + 1U < sizeof(normalized) && e.text[p] != '\0') {
                    normalized[p] = (e.text[p] == ',') ? '.' : e.text[p];
                    ++p;
                }
                normalized[p] = '\0';
                changed = menu_.setText(e.row, normalized);
            }
            if (!changed) {
                ConfigMenuView view{};
                menu_.buildView(view);
                if (e.row < ConfigMenuModel::RowsPerPage && view.rows[e.row].visible) {
                    const ConfigMenuRowView& rv = view.rows[e.row];
                    LOGW("Config RowSetText rejected row=%u key=%s editType=%u value='%s' text='%s'",
                         (unsigned)e.row,
                         rv.key,
                         (unsigned)rv.editType,
                         rv.value,
                         e.text);
                } else {
                    LOGW("Config RowSetText rejected row=%u text='%s' (row not visible)",
                         (unsigned)e.row,
                         e.text);
                }
            }
            break;
        case HmiEventType::RowSetSlider:
            changed = menu_.setSlider(e.row, e.sliderValue);
            break;
        case HmiEventType::ConfigEnter:
        case HmiEventType::ConfigExit:
            break;
        case HmiEventType::None:
        default:
            break;
    }

    if (changed) {
        viewDirty_ = true;
    }
    if (configInputEvent) {
        LOGI("HMI config event handled type=%u row=%u changed=%d dirty=%d",
             (unsigned)e.type,
             (unsigned)e.row,
             changed ? 1 : 0,
             viewDirty_ ? 1 : 0);
    }
#endif
}

void HMIModule::setHomeErrorMessage_(const char* message, bool forceStateRefresh)
{
    const char* src = (message && message[0] != '\0') ? message : "";
    char next[sizeof(homeErrorMessage_)]{};
    snprintf(next, sizeof(next), "%s", src);

    if (strncmp(homeErrorMessage_, next, sizeof(homeErrorMessage_)) != 0) {
        snprintf(homeErrorMessage_, sizeof(homeErrorMessage_), "%s", next);
    }

    uint32_t mask = kHomePublishErrorMessage;
    if (forceStateRefresh) mask |= kHomePublishStateBits;
    queueHomePublish_(mask);
}

void HMIModule::reportCommandError_(const char* operation, const char* reply)
{
    char code[40]{};
    char where[48]{};
    const bool haveCode = extractJsonStringField_(reply, "code", code, sizeof(code));
    const bool haveWhere = extractJsonStringField_(reply, "where", where, sizeof(where));

    char message[sizeof(homeErrorMessage_)]{};
    if (haveCode && haveWhere) {
        snprintf(message, sizeof(message), "%s (%s)", code, where);
    } else if (haveCode) {
        snprintf(message, sizeof(message), "%s", code);
    } else if (operation && operation[0] != '\0') {
        snprintf(message, sizeof(message), "%s failed", operation);
    } else {
        snprintf(message, sizeof(message), "Command failed");
    }

    setHomeErrorMessage_(message, true);
}

bool HMIModule::executeHmiCommand_(HmiCommandId command, uint8_t value)
{
    bool current = false;
    PoolLogicModeFlags modes{};

    switch (command) {
        case HmiCommandId::HomeSyncRequest:
            queueHomePublish_(kHomePublishAll);
            return true;

        case HmiCommandId::HomeConfigOpen:
            // Nextion owns visual navigation. The config menu is opened only
            // when pageCfgMenu announces itself with the PAGE frame.
            return true;

        case HmiCommandId::HomeFiltrationSet:
            return executeCommandBool_("poollogic.filtration.write", value != 0U);

        case HmiCommandId::HomeAutoModeSet:
            return executePoolLogicModePatch_("auto_mode", value != 0U);

        case HmiCommandId::HomePhPumpSet:
            return executeCommandBool_("poollogic.ph_pump.write", value != 0U);

        case HmiCommandId::HomeOrpPumpSet:
            return executeCommandBool_("poollogic.dis_pump.write", value != 0U);

        case HmiCommandId::HomePhPumpToggle:
            if (!readPoolDeviceActualOn_(phPumpDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executeCommandBool_("poollogic.ph_pump.write", !current);

        case HmiCommandId::HomeOrpPumpToggle:
            if (!readPoolDeviceActualOn_(orpPumpDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executeCommandBool_("poollogic.dis_pump.write", !current);

        case HmiCommandId::HomeFiltrationToggle:
            if (!readPoolDeviceActualOn_(filtrationDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executeCommandBool_("poollogic.filtration.write", !current);

        case HmiCommandId::HomeAutoModeToggle:
            (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
            return executePoolLogicModePatch_("auto_mode", !modes.autoMode);

        case HmiCommandId::HomeOrpAutoModeToggle:
            (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
            return executePoolLogicModePatch_("dis_auto_mode", !modes.orpAutoMode);

        case HmiCommandId::HomePhAutoModeToggle:
            (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
            return executePoolLogicModePatch_("ph_auto_mode", !modes.phAutoMode);

        case HmiCommandId::HomeWinterModeToggle:
            (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
            return executePoolLogicModePatch_("winter_mode", !modes.winterMode);

        case HmiCommandId::HomeLightsToggle:
            if (!readPoolDeviceActualOn_(lightsDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executePoolDeviceWrite_(lightsDeviceSlot_, !current);

        case HmiCommandId::HomeRobotToggle:
            if (!readPoolDeviceActualOn_(robotDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executeCommandBool_("poollogic.robot.write", !current);

        case HmiCommandId::DisplayWifiFactoryReset:
            setHomeErrorMessage_("DisplayOnly", true);
            return true;

        case HmiCommandId::None:
        default:
            setHomeErrorMessage_("UnknownCmd", true);
            return false;
    }
}

bool HMIModule::executeCommandBool_(const char* cmdName, bool value)
{
    if (!cmdName || cmdName[0] == '\0') return false;
    if (!cmdSvc_ || !cmdSvc_->execute) {
        LOGW("HMI command service unavailable cmd=%s", cmdName);
        setHomeErrorMessage_("CmdServiceUnavailable", true);
        return false;
    }

    char args[32]{};
    char reply[192]{};
    snprintf(args, sizeof(args), "{\"value\":%s}", value ? "true" : "false");

    const bool ok = cmdSvc_->execute(cmdSvc_->ctx, cmdName, args, nullptr, reply, sizeof(reply));
    if (!ok) {
        LOGW("HMI command failed cmd=%s args=%s reply=%s",
             cmdName,
             args,
             reply[0] ? reply : "{}");
        reportCommandError_(cmdName, reply[0] ? reply : nullptr);
        return false;
    }

    LOGI("HMI command ok cmd=%s args=%s", cmdName, args);
    queueHomePublish_(kHomePublishAll);
    return true;
}

bool HMIModule::executePoolDeviceWrite_(uint8_t slot, bool value)
{
    if (!cmdSvc_ || !cmdSvc_->execute) {
        LOGW("HMI command service unavailable cmd=pooldevice.write");
        setHomeErrorMessage_("CmdServiceUnavailable", true);
        return false;
    }

    char args[40]{};
    char reply[192]{};
    snprintf(args, sizeof(args), "{\"slot\":%u,\"value\":%s}", (unsigned)slot, value ? "true" : "false");

    const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "pooldevice.write", args, nullptr, reply, sizeof(reply));
    if (!ok) {
        LOGW("HMI command failed cmd=pooldevice.write args=%s reply=%s",
             args,
             reply[0] ? reply : "{}");
        reportCommandError_("pooldevice.write", reply[0] ? reply : nullptr);
        return false;
    }

    LOGI("HMI command ok cmd=pooldevice.write args=%s", args);
    queueHomePublish_(kHomePublishAll);
    return true;
}

bool HMIModule::executePoolLogicModePatch_(const char* key, bool value)
{
    if (!key || key[0] == '\0') return false;
    if (!cfgSvc_ || !cfgSvc_->applyJson) {
        LOGW("HMI config service unavailable key=%s", key);
        setHomeErrorMessage_("CfgServiceUnavailable", true);
        return false;
    }

    char json[96]{};
    const char* moduleName = "poollogic/modes";
    if (strcmp(key, "ph_auto_mode") == 0) {
        moduleName = "poollogic/ph";
    } else if (strcmp(key, "dis_auto_mode") == 0) {
        moduleName = "poollogic/chlorine";
    }
    snprintf(json, sizeof(json), "{\"%s\":{\"%s\":%s}}", moduleName, key, value ? "true" : "false");
    const bool ok = cfgSvc_->applyJson(cfgSvc_->ctx, json);
    if (!ok) {
        LOGW("HMI config patch failed json=%s", json);
        reportCommandError_("cfg.applyJson", "{\"ok\":false,\"err\":{\"code\":\"CfgApplyFailed\",\"where\":\"cfg.applyJson\"}}");
        return false;
    }

    LOGI("HMI config patch ok json=%s", json);
    queueHomePublish_(kHomePublishAll);
    return true;
}

uint32_t HMIModule::cacheCurrentConfigContext_()
{
#if !FLOW_HMI_CONFIG_MENU_ENABLED
    return 0U;
#else
    uint32_t token = nextConfigContextToken_++;
    if (token == 0U) {
        token = 1U;
        nextConfigContextToken_ = 2U;
    }
    activeConfigContextToken_ = token;
    return token;
#endif
}

bool HMIModule::restoreConfigContext_(uint32_t token)
{
#if !FLOW_HMI_CONFIG_MENU_ENABLED
    (void)token;
    return false;
#else
    if (token == 0U) return false;
    if (token != activeConfigContextToken_) return false;
    if (!ensureConfigMenuReady_()) return false;
    LOGI("HMI resumed config context token=%lu mode=%s page=%u module='%s'",
         (unsigned long)token,
         menu_.isEditing() ? "edit" : "browse",
         (unsigned)(menu_.pageIndex() + 1U),
         menu_.currentModule());
    return true;
#endif
}

void HMIModule::refreshLocale_()
{
    const char* nextLang = "fr";
    uint32_t nextGeneration = localeGenerationSeen_;
    if (localeSvc_ && localeSvc_->language) {
        const char* svcLang = localeSvc_->language(localeSvc_->ctx);
        if (svcLang && runtimeUiIsEnglishLang(svcLang)) {
            nextLang = "en";
        }
    }
    if (localeSvc_ && localeSvc_->generation) {
        nextGeneration = localeSvc_->generation(localeSvc_->ctx);
    }

    const bool changed = strncmp(localeLang_, nextLang, sizeof(localeLang_)) != 0;
    if (changed) {
        snprintf(localeLang_, sizeof(localeLang_), "%s", nextLang);
    }
    if (changed || nextGeneration != localeGenerationSeen_) {
        localeGenerationSeen_ = nextGeneration;
        markAlarmViewDirty_();
    }
}

void HMIModule::markAlarmViewDirty_()
{
    if (alarmPageActive_) {
        viewDirty_ = true;
    }
}

uint8_t HMIModule::collectAlarmIds_(AlarmId* out, uint8_t max) const
{
    if (!out || max == 0U || !alarmSvc_ || !alarmSvc_->listIds) return 0U;
    return alarmSvc_->listIds(alarmSvc_->ctx, out, max);
}

bool HMIModule::readAlarmState_(AlarmId id, bool& active, bool& resettable, AlarmCondState& condition) const
{
    active = false;
    resettable = false;
    condition = AlarmCondState::Unknown;
    if (!alarmSvc_) return false;

    if (alarmSvc_->isActive) {
        active = alarmSvc_->isActive(alarmSvc_->ctx, id);
    }
    if (alarmSvc_->isResettable) {
        resettable = alarmSvc_->isResettable(alarmSvc_->ctx, id);
    }

    if (!alarmSvc_->buildAlarmState) return true;

    char json[128]{};
    if (!alarmSvc_->buildAlarmState(alarmSvc_->ctx, id, json, sizeof(json))) return true;

    uint16_t c = 2U;
    if (!findJsonUInt16_(json, "c", c)) return true;
    if (c == 0U) condition = AlarmCondState::False;
    else if (c == 1U) condition = AlarmCondState::True;
    else condition = AlarmCondState::Unknown;
    return true;
}

const char* HMIModule::alarmLabelForId_(AlarmId id) const
{
    const char* shortLabel = alarmLabelShortForId_(id);
    if (shortLabel && shortLabel[0] != '\0') return shortLabel;
    const uint32_t mask = alarmMaskFromId_(id);
    if (mask == 0U) return "";
    return runtimeUiAlarmActiveFlagLabel(mask, localeLang_);
}

const char* HMIModule::alarmLabelShortForId_(AlarmId id) const
{
    const bool en = runtimeUiIsEnglishLang(localeLang_);
    switch (id) {
        case AlarmId::PoolPsiLow: return en ? "Low PSI" : "PSI bas";
        case AlarmId::PoolPsiHigh: return en ? "High PSI" : "PSI haut";
        case AlarmId::PoolPhTankLow: return en ? "pH empty" : "pH vide";
        case AlarmId::PoolChlorineTankLow: return en ? "Chlorine empty" : "Chlore vide";
        case AlarmId::PoolPhPumpMaxUptime: return en ? "pH uptime" : "pH uptime";
        case AlarmId::PoolChlorinePumpMaxUptime: return en ? "ORP uptime" : "ORP uptime";
        case AlarmId::PoolWaterLevelLow: return en ? "Low water" : "Eau basse";
        default: return "";
    }
}

bool HMIModule::buildAlarmPageView_(ConfigMenuView& out, bool)
{
    out = ConfigMenuView{};
    snprintf(out.breadcrumb, sizeof(out.breadcrumb), "%s", runtimeUiIsEnglishLang(localeLang_) ? "Alarms" : "Alarmes");
    out.contextRef = 0U;
    out.mode = ConfigMenuMode::Browse;
    out.canHome = true;
    out.canBack = false;
    out.canValidate = false;
    out.isHome = false;

    AlarmId ids[Limits::Alarm::MaxAlarms]{};
    const uint8_t totalCount = collectAlarmIds_(ids, (uint8_t)Limits::Alarm::MaxAlarms);
    const uint8_t pageCount = totalCount == 0U
        ? 1U
        : (uint8_t)((totalCount + ConfigMenuModel::RowsPerPage - 1U) / ConfigMenuModel::RowsPerPage);
    uint8_t pageIndex = alarmPageIndex_;
    if (pageIndex >= pageCount) pageIndex = (uint8_t)(pageCount - 1U);

    out.pageIndex = pageIndex;
    out.pageCount = pageCount;

    const uint8_t start = (uint8_t)(pageIndex * ConfigMenuModel::RowsPerPage);
    uint8_t visibleCount = 0U;
    for (uint8_t row = 0; row < ConfigMenuModel::RowsPerPage; ++row) {
        const uint8_t idx = (uint8_t)(start + row);
        ConfigMenuRowView& viewRow = out.rows[row];

        if (idx >= totalCount) {
            viewRow.visible = false;
            alarmRowIds_[row] = AlarmId::None;
            alarmRowResettable_[row] = false;
            continue;
        }
        ++visibleCount;

        const AlarmId id = ids[idx];
        bool active = false;
        bool resettable = false;
        AlarmCondState condition = AlarmCondState::Unknown;
        (void)readAlarmState_(id, active, resettable, condition);

        const char* label = alarmLabelForId_(id);
        char fallback[20]{};
        if (!label || label[0] == '\0') {
            snprintf(fallback, sizeof(fallback), "Alarm %u", (unsigned)((uint16_t)id));
            label = fallback;
        }

        const bool critical = active && condition == AlarmCondState::True;
        const bool latchOk = active && !critical && resettable;
        const bool showStateValue = critical || latchOk;
        const char* stateValue = "";
        if (critical) {
            stateValue = "f";
        } else if (latchOk) {
            stateValue = "o";
        }

        viewRow.visible = true;
        viewRow.valueVisible = showStateValue;
        viewRow.editable = false;
        viewRow.dirty = false;
        viewRow.canEnter = resettable;
        viewRow.canEdit = false;
        viewRow.widget = ConfigMenuWidget::Text;
        viewRow.editType = 0U;
        snprintf(viewRow.key, sizeof(viewRow.key), "alarm_%u", (unsigned)row);
        snprintf(viewRow.label, sizeof(viewRow.label), "%s", label);
        snprintf(viewRow.value, sizeof(viewRow.value), "%s", stateValue);

        alarmRowIds_[row] = id;
        alarmRowResettable_[row] = resettable;
    }
    out.rowCountOnPage = visibleCount;

    alarmPageIndex_ = pageIndex;
    alarmPageCount_ = pageCount;
    alarmRowCount_ = visibleCount;
    return true;
}

bool HMIModule::resetAlarmRow_(uint8_t rowIndex)
{
    if (rowIndex >= ConfigMenuModel::RowsPerPage) return false;
    if (!alarmSvc_ || !alarmSvc_->reset) return false;
    if (!alarmRowResettable_[rowIndex]) return false;
    const AlarmId id = alarmRowIds_[rowIndex];
    if (id == AlarmId::None) return false;
    const bool ok = alarmSvc_->reset(alarmSvc_->ctx, id);
    if (ok) {
        viewDirty_ = true;
        queueHomePublish_(kHomePublishAlarmBits);
    }
    return ok;
}

bool HMIModule::nextAlarmPage_()
{
    if (alarmPageIndex_ + 1U >= alarmPageCount_) return false;
    ++alarmPageIndex_;
    viewDirty_ = true;
    return true;
}

bool HMIModule::prevAlarmPage_()
{
    if (alarmPageIndex_ == 0U) return false;
    --alarmPageIndex_;
    viewDirty_ = true;
    return true;
}

bool HMIModule::isAlarmPageId_(uint8_t pageId) const
{
    return isAlarmPageCode_(pageId);
}

bool HMIModule::isDisplaySleeping_() const
{
    if (driver_ == static_cast<const IHmiDriver*>(&nextion_)) {
        return nextion_.isSleeping();
    }
    if (driver_ == static_cast<const IHmiDriver*>(&remoteUdp_) &&
        remoteUdpServer_) {
        return remoteUdpServer_->isDisplaySleeping();
    }
    return false;
}

void HMIModule::updateNextionMotionWake_(uint32_t nowMs)
{
    if (!driverReady_ || driver_ != static_cast<IHmiDriver*>(&nextion_)) return;

    // IO_ID_INVALID is the non-Waveshare default and disables motion wake.
    if (cfgData_.nextionMotionIoId == IO_ID_INVALID) return;

    uint8_t motionActive = 0U;
    IoStatus status = IO_ERR_INVALID_ARG;
    if (cfgData_.nextionMotionIoId >= IO_ID_DI_BASE && cfgData_.nextionMotionIoId < IO_ID_AI_BASE) {
        status = (ioSvc_ && ioSvc_->readDigital)
            ? ioSvc_->readDigital(ioSvc_->ctx, cfgData_.nextionMotionIoId, &motionActive, nullptr, nullptr)
            : IO_ERR_NOT_READY;
    }

    if (status != IO_OK) {
        if (!nextionMotionReadErrorLogged_) {
            LOGW("HMI Nextion motion input unavailable io_id=%u status=%u; motion wake disabled",
                 (unsigned)cfgData_.nextionMotionIoId,
                 (unsigned)status);
            nextionMotionReadErrorLogged_ = true;
        }
        nextionMotionInputReady_ = false;
        nextionMotionActiveLast_ = false;
        return;
    }

    const bool active = motionActive != 0U;
    const bool activated = active && (!nextionMotionInputReady_ || !nextionMotionActiveLast_);
    nextionMotionInputReady_ = true;
    nextionMotionActiveLast_ = active;
    nextionMotionReadErrorLogged_ = false;

    // Wake on a new motion edge even if the sleep notification was missed. If
    // the panel sleeps while the PIR remains active, wake it again immediately.
    if (!active || (!activated && !nextion_.isSleeping())) return;
    if (lastNextionMotionWakeAttemptMs_ != 0U &&
        (uint32_t)(nowMs - lastNextionMotionWakeAttemptMs_) < kNextionMotionWakeRetryMs) {
        return;
    }

    lastNextionMotionWakeAttemptMs_ = nowMs;
    const bool woke = nextion_.wakeFromSleep();
    if (!woke) {
        LOGW("HMI Nextion motion wake failed io_id=%u", (unsigned)cfgData_.nextionMotionIoId);
        return;
    }

    LOGI("HMI Nextion woke by motion io_id=%u", (unsigned)cfgData_.nextionMotionIoId);
    resetClockPublishStamps_();
    queueHomePublish_(kHomePublishAll);
    lastHomePeriodicRefreshMs_ = nowMs;
    viewDirty_ = true;
}

bool HMIModule::render_()
{
    if (!driver_) return false;
    ConfigMenuView view{};
#if FLOW_HMI_CONFIG_MENU_ENABLED
    {
        if (!ensureConfigMenuReady_()) return false;
        menu_.buildView(view);
        view.contextRef = cacheCurrentConfigContext_();
    }
#else
    {
        snprintf(view.breadcrumb, sizeof(view.breadcrumb), "Configuration indisponible");
        view.contextRef = 0U;
        view.pageIndex = 0;
        view.pageCount = 1;
        view.canHome = false;
        view.canBack = false;
        view.canValidate = false;
        view.isHome = true;
        view.rowCountOnPage = 1;
        view.rows[0].visible = true;
        view.rows[0].editable = false;
        snprintf(view.rows[0].key, sizeof(view.rows[0].key), "menu");
        snprintf(view.rows[0].label, sizeof(view.rows[0].label), "Config");
        snprintf(view.rows[0].value, sizeof(view.rows[0].value), "disabled");
    }
#endif
    const bool ok = driver_->renderConfigMenu(view);
    if (ok) {
        lastRenderMs_ = millis();
        lastConfigValueRefreshMs_ = lastRenderMs_;
    }
    return ok;
}

bool HMIModule::renderAlarmPage_()
{
    if (!driver_) return false;
    ConfigMenuView view{};
    if (!buildAlarmPageView_(view, false)) return false;
    const bool ok = driver_->renderConfigMenu(view);
    if (ok) {
        lastRenderMs_ = millis();
        lastConfigValueRefreshMs_ = lastRenderMs_;
    }
    return ok;
}

bool HMIModule::refreshConfigMenuValues_()
{
    if (!driver_) return false;

#if FLOW_HMI_CONFIG_MENU_ENABLED
    {
        if (!ensureConfigMenuReady_()) return false;
        ConfigMenuView view{};
        menu_.buildView(view);
        view.contextRef = cacheCurrentConfigContext_();
        const bool ok = driver_->refreshConfigMenuValues(view);
        if (ok) lastConfigValueRefreshMs_ = millis();
        return ok;
    }
#else
    return false;
#endif
}

bool HMIModule::refreshAlarmPageValues_()
{
    if (!driver_) return false;
    ConfigMenuView view{};
    if (!buildAlarmPageView_(view, true)) return false;
    const bool ok = driver_->refreshConfigMenuValues(view);
    if (ok) lastConfigValueRefreshMs_ = millis();
    return ok;
}

bool HMIModule::buildMenuJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;

#if !FLOW_HMI_CONFIG_MENU_ENABLED
    {
        DynamicJsonDocument doc(256);
        JsonObject root = doc.to<JsonObject>();
        root["ok"] = true;
        root["disabled"] = true;
        root["driver"] = driver_ ? driver_->driverId() : "";
        root["path"] = "Configuration indisponible";
        root["page"] = 1U;
        root["pages"] = 1U;
        root["rows"] = 0U;
        root["can_home"] = false;
        root["can_back"] = false;
        root["can_validate"] = false;
        const size_t written = serializeJson(root, out, outLen);
        return written > 0 && written < outLen;
    }
#else
    if (!ensureConfigMenuReady_()) return false;
    ConfigMenuView view{};
    menu_.buildView(view);
    view.contextRef = cacheCurrentConfigContext_();

    DynamicJsonDocument doc(2048);
    JsonObject root = doc.to<JsonObject>();
    root["ok"] = true;
    root["driver"] = driver_ ? driver_->driverId() : "";
    root["path"] = view.breadcrumb;
    root["ctx_ref"] = view.contextRef;
    root["page"] = (uint32_t)view.pageIndex + 1U;
    root["pages"] = (uint32_t)view.pageCount;
    root["rows"] = (uint32_t)view.rowCountOnPage;
    root["mode"] = (view.mode == ConfigMenuMode::Edit) ? "edit" : "browse";
    root["can_home"] = view.canHome;
    root["can_back"] = view.canBack;
    root["can_validate"] = view.canValidate;

    JsonArray arr = root.createNestedArray("items");
    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        const ConfigMenuRowView& row = view.rows[i];
        if (!row.visible) continue;
        JsonObject it = arr.createNestedObject();
        it["i"] = i;
        it["key"] = row.key;
        it["label"] = row.label;
        it["value"] = row.value;
        it["value_visible"] = row.valueVisible;
        it["can_enter"] = row.canEnter;
        it["can_edit"] = row.canEdit;
        it["editable"] = row.editable;
        it["dirty"] = row.dirty;
        it["widget"] = (uint8_t)row.widget;
    }

    const size_t written = serializeJson(root, out, outLen);
    return written > 0 && written < outLen;
#endif
}

void HMIModule::loop()
{
    const uint32_t wsLedNow = millis();
    applyWs2812AutoWifiProfile_();
    ws2812StatusLed_.tick(wsLedNow);
    logHmiLedDebug_(wsLedNow);

    if (driver_) {
        if (!driverReady_) {
            driverReady_ = driver_->begin();
            if (!driverReady_) {
                venice_.tick(millis(), ioSvc_, waterTempIoId_);
                vTaskDelay(pdMS_TO_TICKS(500));
                return;
            }
            if (!validateDriverDisplayVersion_(driver_ == static_cast<IHmiDriver*>(&nextion_))) {
                vTaskDelay(pdMS_TO_TICKS(25));
                return;
            }
            resetClockPublishStamps_();
            viewDirty_ = true;
            queueHomePublish_(kHomePublishAll);
            lastHomePeriodicRefreshMs_ = millis();
            lastNextionPageProbeMs_ = 0U;
            if (driver_ == static_cast<IHmiDriver*>(&nextion_)) {
                const bool woke = nextion_.wakeFromSleep();
                LOGI("HMI boot wakeFromSleep sent=%d", woke ? 1 : 0);
            }
            LOGI("HMI driver ready id=%s", driver_ ? driver_->driverId() : "none");
            rtcPushPending_ = true;
            lastRtcPushAttemptMs_ = 0;
        }

        if (!validateDriverDisplayVersion_(false)) {
            vTaskDelay(pdMS_TO_TICKS(25));
            return;
        }

        if (driverReady_ && driver_ == static_cast<IHmiDriver*>(&remoteUdp_) &&
            remoteUdpServer_ && remoteUdpServer_->consumeFullRefreshRequested()) {
            resetClockPublishStamps_();
            queueHomePublish_(kHomePublishAll);
            lastHomePeriodicRefreshMs_ = millis();
            rtcPushPending_ = true;
            lastRtcPushAttemptMs_ = 0;
            viewDirty_ = true;
        }

        HmiEvent ev{};
        while (driver_->pollEvent(ev)) {
            handleDriverEvent_(ev);
        }
    } else {
        driverReady_ = false;
    }

    const uint32_t now = millis();
    updateNextionMotionWake_(now);
    if (homeBindingsRefreshPending_) {
        homeBindingsRefreshPending_ = false;
        refreshHomeBindings_();
    }
    const bool displaySleeping = isDisplaySleeping_();
    if (driverReady_ &&
        driver_ == static_cast<IHmiDriver*>(&nextion_) &&
        !displaySleeping) {
        uint8_t nextionPage = 0xFFU;
        if (nextion_.currentPage(nextionPage)) {
            lastNextionPageProbeMs_ = 0U;
        } else {
            const uint32_t probeFastWindowMs =
                (uint32_t)kNextionPageProbeInitialAttempts * kNextionPageProbeInitialPeriodMs;
            const uint32_t probePeriodMs =
                ((uint32_t)(now - lastHomePeriodicRefreshMs_) < probeFastWindowMs)
                    ? kNextionPageProbeInitialPeriodMs
                    : kNextionPageProbeSteadyPeriodMs;
            if (lastNextionPageProbeMs_ == 0U ||
                (uint32_t)(now - lastNextionPageProbeMs_) >= probePeriodMs) {
                uint32_t pending = 0U;
                portENTER_CRITICAL(&homePublishMux_);
                pending = homePublishMask_;
                portEXIT_CRITICAL(&homePublishMux_);
                lastNextionPageProbeMs_ = now;
                const bool sent = nextion_.requestPageReport();
                LOGD("HMI Nextion page probe sent=%d period_ms=%lu since_boot_ms=%lu home=%d cfg=%d alarm=%d pending=0x%03lx",
                     sent ? 1 : 0,
                     (unsigned long)probePeriodMs,
                     (unsigned long)(now - lastHomePeriodicRefreshMs_),
                     homePageVisible_ ? 1 : 0,
                     configMenuActive_ ? 1 : 0,
                     alarmPageActive_ ? 1 : 0,
                     (unsigned long)pending);
            }
        }
    }
    if (driverReady_ &&
        driver_ == static_cast<IHmiDriver*>(&nextion_) &&
        !displaySleeping &&
        !configMenuActive_ &&
        (uint32_t)(now - lastDisplayVersionProbeMs_) >= kDisplayVersionProbePeriodMs) {
        lastDisplayVersionProbeMs_ = now;
        const bool hadVersion = nextion_.hasDisplayVersion();
        char previousVersion[HMI_DISPLAY_VERSION_TEXT_MAX]{};
        if (hadVersion) {
            strncpy(previousVersion, nextion_.displayVersion(), sizeof(previousVersion) - 1U);
            previousVersion[sizeof(previousVersion) - 1U] = '\0';
        }
        if (nextion_.detectDisplayVersion(0U, true)) {
            const char* currentVersion = nextion_.displayVersion();
            if (!hadVersion || strcmp(currentVersion, previousVersion) != 0) {
                LOGI("Ecran Nextion version relue: %s -> %s",
                     hadVersion ? previousVersion : "",
                     currentVersion);
            }
            (void)validateDriverDisplayVersion_(false);
        }
    }
    if (kFrontLedsSupported && cfgData_.ledsEnabled) {
        if (frontLedPanel_.isReady() &&
            (uint32_t)(now - lastLedPageToggleMs_) >= kLedPageTogglePeriodMs) {
            lastLedPageToggleMs_ = now;
            const uint8_t nextPage = (ledPage_ == 1U) ? 2U : 1U;
            ledPage_ = nextPage;
            applyLedMask_(true);
        }
        if (!ledMaskValid_ || (uint32_t)(now - lastLedApplyTryMs_) >= 1000U) {
            applyLedMask_();
            lastLedApplyTryMs_ = now;
        }
    } else if (kFrontLedsSupported &&
               frontLedPanel_.isReady() &&
               frontLedPanel_.isEnabled() &&
               (uint32_t)(now - lastLedApplyTryMs_) >= 1000U) {
        (void)frontLedPanel_.setEnabled(false);
        lastLedApplyTryMs_ = now;
    }
    serviceRtcBridge_(now);
    queueClockPublishIfDue_(now);
    refreshLocale_();
    if (driverReady_ &&
        driver_ &&
        homePageVisible_ &&
        !displaySleeping &&
        !configMenuActive_ &&
        !alarmPageActive_ &&
        (uint32_t)(now - lastHomePeriodicRefreshMs_) >= kHomePeriodicRefreshPeriodMs) {
        lastHomePeriodicRefreshMs_ = now;
        queueHomePublish_(kHomePublishAll);
        LOGD("HMI periodic Home refresh queued");
    }
    if (alarmPageActive_ && driver_ && !displaySleeping && viewDirty_) {
        if (renderAlarmPage_()) {
            viewDirty_ = false;
        }
    } else if (alarmPageActive_ &&
               driver_ &&
               !displaySleeping &&
               (uint32_t)(now - lastConfigValueRefreshMs_) >= 2000U) {
        (void)refreshAlarmPageValues_();
    } else if (kConfigMenuEnabled && configMenuActive_ && driver_ && !displaySleeping && viewDirty_) {
        if (render_()) {
            viewDirty_ = false;
        }
    } else if (kConfigMenuEnabled &&
               configMenuActive_ &&
               driver_ &&
               !displaySleeping &&
               (uint32_t)(now - lastConfigValueRefreshMs_) >= 5000U) {
        (void)refreshConfigMenuValues_();
    }
    if (homePageVisible_) {
        flushHomePublish_();
    }

    if (driver_) driver_->tick(now);
    venice_.tick(now, ioSvc_, waterTempIoId_);
    vTaskDelay(pdMS_TO_TICKS(25));
}
