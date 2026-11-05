/**
 * @file TFTModuleS3.cpp
 * @brief Local Waveshare ESP32-S3 TFT display.
 */

#include "Modules/TFTModuleS3/TFTModuleS3.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Board/BoardSpec.h"
#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/FirmwareVersion.h"
#include "Core/Generated/RuntimeUiAlarmText_Generated.h"
#include "Core/Generated/RuntimeUiManifest_Generated.h"
#include "Core/Services/IAlarm.h"
#include "Core/SystemLimits.h"
#include "Core/SystemStats.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include "Modules/SupervisorHMIModule/Drivers/FlowIoLogoBitmap.h"
#include <WiFi.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"

namespace {
constexpr uint8_t kCfgProducerId = 54;
constexpr uint8_t kCfgBranch = 1;
constexpr uint8_t kCfgBranchSensorBase = 16;
constexpr uint8_t kCfgBranchAlarmBase = 32;
constexpr uint16_t kCfgMsgSensorBase = 16U;
constexpr uint16_t kCfgMsgAlarmBase = 32U;
constexpr uint32_t kRenderPeriodMs = 1000U;
constexpr uint32_t kBacklightTimeoutMs = 60000U;
constexpr uint32_t kStartupSplashHoldMs = 5000U;
constexpr uint32_t kPageRotateMs = 10000U;

constexpr uint16_t rgb565_(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

constexpr uint16_t kColorBg = rgb565_(255, 255, 255);
constexpr uint16_t kColorHeader = rgb565_(255, 255, 255);
constexpr uint16_t kColorText = rgb565_(17, 24, 39);
constexpr uint16_t kColorBrandFlow = rgb565_(7, 59, 102);
constexpr uint16_t kColorBrandIo = rgb565_(0, 174, 239);
constexpr uint16_t kColorMuted = rgb565_(108, 122, 141);
constexpr uint16_t kColorWifiOff = rgb565_(226, 235, 246);
constexpr uint16_t kColorOn = rgb565_(44, 204, 116);
constexpr uint16_t kColorOff = rgb565_(148, 163, 184);
constexpr uint16_t kColorAlarmAct = rgb565_(224, 72, 72);
constexpr uint16_t kColorAlarmAck = rgb565_(240, 178, 85);
constexpr uint16_t kColorGaugeOk = rgb565_(47, 158, 104);
constexpr uint16_t kColorCardBg = rgb565_(255, 255, 255);
constexpr uint16_t kColorCardBorder = rgb565_(217, 227, 239);
constexpr uint16_t kColorValue = rgb565_(14, 23, 43);
constexpr uint16_t kColorCheckMark = rgb565_(255, 255, 255);
constexpr int16_t kHeaderH = 48;
constexpr int16_t kSidePad = 8;
constexpr uint8_t kAlarmSlotCount = 8U;
constexpr uint8_t kOverviewStatusCount = 5U;

constexpr const char* kOverviewStatusLabels[kOverviewStatusCount] = {
    "Auto",
    "Auto pH",
    "Auto ORP",
    "Auto SWG",
    "Winter",
};

constexpr const char* kSensorModuleNames[TFTModuleS3::DashboardSlotCount] = {
    "tft/s3/sensors/slot00",
    "tft/s3/sensors/slot01",
    "tft/s3/sensors/slot02",
    "tft/s3/sensors/slot03",
    "tft/s3/sensors/slot04",
    "tft/s3/sensors/slot05",
    "tft/s3/sensors/slot06",
    "tft/s3/sensors/slot07",
};

constexpr const char* kSensorEnabledKeys[TFTModuleS3::DashboardSlotCount] = {
    "tfs00en", "tfs01en", "tfs02en", "tfs03en", "tfs04en", "tfs05en", "tfs06en", "tfs07en"
};

constexpr const char* kSensorRuntimeIdKeys[TFTModuleS3::DashboardSlotCount] = {
    "tfs00ri", "tfs01ri", "tfs02ri", "tfs03ri", "tfs04ri", "tfs05ri", "tfs06ri", "tfs07ri"
};

constexpr const char* kSensorLabelKeys[TFTModuleS3::DashboardSlotCount] = {
    "tfs00lb", "tfs01lb", "tfs02lb", "tfs03lb", "tfs04lb", "tfs05lb", "tfs06lb", "tfs07lb"
};

constexpr const char* kSensorColorIdKeys[TFTModuleS3::DashboardSlotCount] = {
    "tfs00uc", "tfs01uc", "tfs02uc", "tfs03uc", "tfs04uc", "tfs05uc", "tfs06uc", "tfs07uc"
};

constexpr const char* kAlarmModuleNames[TFTModuleS3::AlarmDashboardSlotCount] = {
    "tft/s3/alarms/slot00",
    "tft/s3/alarms/slot01",
    "tft/s3/alarms/slot02",
    "tft/s3/alarms/slot03",
    "tft/s3/alarms/slot04",
    "tft/s3/alarms/slot05",
    "tft/s3/alarms/slot06",
    "tft/s3/alarms/slot07",
};

constexpr const char* kAlarmEnabledKeys[TFTModuleS3::AlarmDashboardSlotCount] = {
    "tfa00en", "tfa01en", "tfa02en", "tfa03en", "tfa04en", "tfa05en", "tfa06en", "tfa07en"
};

constexpr const char* kAlarmIdKeys[TFTModuleS3::AlarmDashboardSlotCount] = {
    "tfa00id", "tfa01id", "tfa02id", "tfa03id", "tfa04id", "tfa05id", "tfa06id", "tfa07id"
};

constexpr const char* kAlarmLabelKeys[TFTModuleS3::AlarmDashboardSlotCount] = {
    "tfa00lb", "tfa01lb", "tfa02lb", "tfa03lb", "tfa04lb", "tfa05lb", "tfa06lb", "tfa07lb"
};

constexpr const char* kAlarmColorIdKeys[TFTModuleS3::AlarmDashboardSlotCount] = {
    "tfa00uc", "tfa01uc", "tfa02uc", "tfa03uc", "tfa04uc", "tfa05uc", "tfa06uc", "tfa07uc"
};

struct DashboardColorPreset {
    uint8_t id;
    uint16_t rgb565;
};

constexpr RuntimeUiId kDashboardDefaultRuntimeUiIds[TFTModuleS3::DashboardSlotCount] = {
    makeRuntimeUiId(ModuleId::Io, 1),
    makeRuntimeUiId(ModuleId::Io, 2),
    makeRuntimeUiId(ModuleId::Io, 3),
    makeRuntimeUiId(ModuleId::Io, 4),
    makeRuntimeUiId(ModuleId::Io, 5),
    makeRuntimeUiId(ModuleId::Io, 8),
    makeRuntimeUiId(ModuleId::Io, 7),
    makeRuntimeUiId(ModuleId::Io, 6),
};

constexpr const char* kDashboardDefaultLabels[TFTModuleS3::DashboardSlotCount] = {
    "Eau",
    "Air",
    "pH",
    "ORP",
    "Compteur",
    "BME680",
    "BMP280",
    "PSI",
};

constexpr DashboardColorPreset kDashboardColorPresets[] = {
    {0U, rgb565_(230, 239, 255)},
    {1U, rgb565_(229, 248, 252)},
    {2U, rgb565_(232, 250, 239)},
    {3U, rgb565_(240, 234, 254)},
    {4U, rgb565_(228, 246, 250)},
    {5U, rgb565_(227, 247, 254)},
    {6U, rgb565_(234, 248, 253)},
    {7U, rgb565_(254, 240, 232)},
    {8U, rgb565_(252, 231, 239)},
    {9U, rgb565_(255, 240, 225)},
    {10U, rgb565_(255, 247, 217)},
    {11U, rgb565_(237, 248, 231)},
    {12U, rgb565_(240, 250, 230)},
    {13U, rgb565_(245, 238, 255)},
    {14U, rgb565_(235, 238, 255)},
    {15U, rgb565_(238, 247, 255)},
    {16U, rgb565_(244, 250, 222)},
    {17U, rgb565_(255, 233, 228)},
    {18U, rgb565_(249, 238, 232)},
    {19U, rgb565_(241, 244, 248)},
    {20U, rgb565_(255, 255, 255)},
};

constexpr uint8_t kDashboardDefaultColorIds[TFTModuleS3::DashboardSlotCount] = {
    0U,
    1U,
    2U,
    3U,
    4U,
    5U,
    6U,
    7U,
};

constexpr uint16_t kAlarmDefaultIds[TFTModuleS3::AlarmDashboardSlotCount] = {
    (uint16_t)AlarmId::PoolPsiLow,
    (uint16_t)AlarmId::PoolPsiHigh,
    (uint16_t)AlarmId::PoolPhTankLow,
    (uint16_t)AlarmId::PoolChlorineTankLow,
    (uint16_t)AlarmId::PoolPhPumpMaxUptime,
    (uint16_t)AlarmId::PoolChlorinePumpMaxUptime,
    (uint16_t)AlarmId::PoolWaterLevelLow,
    (uint16_t)AlarmId::PoolWaterLevelLow,
};

constexpr bool kAlarmDefaultEnabled[TFTModuleS3::AlarmDashboardSlotCount] = {
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    false,
};

constexpr const char* kAlarmDefaultLabels[TFTModuleS3::AlarmDashboardSlotCount] = {
    "PSI bas",
    "PSI haut",
    "pH vide",
    "Chlore vide",
    "pH uptime",
    "ORP uptime",
    "Eau basse",
    "",
};

constexpr uint8_t kAlarmDefaultColorIds[TFTModuleS3::AlarmDashboardSlotCount] = {
    17U,
    17U,
    8U,
    10U,
    9U,
    7U,
    5U,
    19U,
};

const MqttConfigRouteProducer::Route kCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::TftS3, kCfgBranch}, "tft/s3", "tft/s3", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgSensorBase + 0U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchSensorBase + 0U)}, kSensorModuleNames[0], kSensorModuleNames[0], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgSensorBase + 1U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchSensorBase + 1U)}, kSensorModuleNames[1], kSensorModuleNames[1], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgSensorBase + 2U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchSensorBase + 2U)}, kSensorModuleNames[2], kSensorModuleNames[2], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgSensorBase + 3U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchSensorBase + 3U)}, kSensorModuleNames[3], kSensorModuleNames[3], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgSensorBase + 4U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchSensorBase + 4U)}, kSensorModuleNames[4], kSensorModuleNames[4], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgSensorBase + 5U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchSensorBase + 5U)}, kSensorModuleNames[5], kSensorModuleNames[5], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgSensorBase + 6U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchSensorBase + 6U)}, kSensorModuleNames[6], kSensorModuleNames[6], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgSensorBase + 7U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchSensorBase + 7U)}, kSensorModuleNames[7], kSensorModuleNames[7], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgAlarmBase + 0U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchAlarmBase + 0U)}, kAlarmModuleNames[0], kAlarmModuleNames[0], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgAlarmBase + 1U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchAlarmBase + 1U)}, kAlarmModuleNames[1], kAlarmModuleNames[1], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgAlarmBase + 2U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchAlarmBase + 2U)}, kAlarmModuleNames[2], kAlarmModuleNames[2], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgAlarmBase + 3U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchAlarmBase + 3U)}, kAlarmModuleNames[3], kAlarmModuleNames[3], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgAlarmBase + 4U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchAlarmBase + 4U)}, kAlarmModuleNames[4], kAlarmModuleNames[4], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgAlarmBase + 5U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchAlarmBase + 5U)}, kAlarmModuleNames[5], kAlarmModuleNames[5], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgAlarmBase + 6U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchAlarmBase + 6U)}, kAlarmModuleNames[6], kAlarmModuleNames[6], (uint8_t)MqttPublishPriority::Normal, nullptr},
    {(uint16_t)(kCfgMsgAlarmBase + 7U), {(uint8_t)ConfigModuleId::TftS3, (uint8_t)(kCfgBranchAlarmBase + 7U)}, kAlarmModuleNames[7], kAlarmModuleNames[7], (uint8_t)MqttPublishPriority::Normal, nullptr},
};

static_assert((sizeof(kCfgRoutes) / sizeof(kCfgRoutes[0])) <= MqttConfigRouteProducer::MaxRoutes,
              "TFT S3 cfg routes exceed MQTT producer capacity");

const SupervisorBoardSpec& fallbackBoardSpec_()
{
    static constexpr SupervisorBoardSpec kFallback{
        {
            240,
            320,
            1,
            0,
            0,
            21,
            45,
            1,
            47,
            -1,
            2,
            48,
            false,
            true,
            40000000U,
            80
        },
        {
            11,
            120,
            true,
            -1,
            40
        },
        {
            -1,
            -1,
            -1,
            115200U
        }
    };
    return kFallback;
}

int clampi_(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

uint8_t pctFromRssi_(int32_t dbm)
{
    const int v = clampi_((int)dbm, -100, -40);
    return (uint8_t)(((v + 100) * 100) / 60);
}

uint8_t barsFromPct_(uint8_t pct)
{
    return (uint8_t)((pct + 19U) / 20U);
}

struct Rect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

Rect overviewTopCardRect_(int16_t w, uint8_t column)
{
    const int16_t gap = 6;
    const int16_t cardW = (int16_t)((w - (2 * kSidePad) - gap) / 2);
    return Rect{
        (int16_t)(kSidePad + ((column > 0U) ? (cardW + gap) : 0)),
        (int16_t)(kHeaderH + 8),
        cardW,
        34
    };
}

Rect overviewAlarmCardRect_(int16_t w, int16_t h, uint8_t column)
{
    const Rect topCard = overviewTopCardRect_(w, 0U);
    const int16_t gap = 6;
    const int16_t top = (int16_t)(topCard.y + topCard.h + 8);
    const int16_t cardW = (int16_t)((w - (2 * kSidePad) - gap) / 2);
    return Rect{
        (int16_t)(kSidePad + ((column > 0U) ? (cardW + gap) : 0)),
        top,
        cardW,
        (int16_t)(h - top - 8)
    };
}

Rect metricCardRect_(int16_t w, int16_t h, uint8_t index)
{
    const int16_t gap = 6;
    const int16_t cardW = (int16_t)((w - (2 * kSidePad) - gap) / 2);
    const int16_t cardH = (int16_t)((h - (2 * kSidePad) - (3 * gap)) / 4);
    const int16_t row = (int16_t)(index / 2U);
    const int16_t col = (int16_t)(index % 2U);
    return Rect{
        (int16_t)(kSidePad + (col * (cardW + gap))),
        (int16_t)(kSidePad + (row * (cardH + gap))),
        cardW,
        cardH
    };
}

bool isDegreeCUnit_(const char* unit)
{
    if (!unit || unit[0] == '\0') return false;
    if ((uint8_t)unit[0] == 0xB0 && unit[1] == 'C' && unit[2] == '\0') return true;
    return (uint8_t)unit[0] == 0xC2 && (uint8_t)unit[1] == 0xB0 && unit[2] == 'C' && unit[3] == '\0';
}

void trimTrailingZeros_(char* text)
{
    if (!text) return;
    char* dot = strchr(text, '.');
    if (!dot) return;
    char* end = text + strlen(text);
    while (end > dot && end[-1] == '0') --end;
    if (end > dot && end[-1] == '.') --end;
    *end = '\0';
    if (strcmp(text, "-0") == 0) snprintf(text, 4, "0");
}

uint16_t alarmIndicatorColor_(uint32_t activeMask, uint32_t conditionMask, uint8_t alarmIndex)
{
    const uint32_t mask = (alarmIndex < 32U) ? (1UL << alarmIndex) : 0U;
    const bool active = (activeMask & mask) != 0U;
    const bool conditionTrue = (conditionMask & mask) != 0U;
    if (active && conditionTrue) return kColorAlarmAct;
    if (active) return kColorAlarmAck;
    return kColorGaugeOk;
}

uint16_t alarmTextColor_(uint32_t activeMask, uint8_t alarmIndex)
{
    const uint32_t mask = (alarmIndex < 32U) ? (1UL << alarmIndex) : 0U;
    return (activeMask & mask) != 0U ? kColorText : kColorMuted;
}
} // namespace

TFTModuleS3::TFTModuleS3(const BoardSpec& board)
    : displayCfg_(displaySpecFromBoard_(board)),
      display_(&spiBus_, displayCfg_.csPin, displayCfg_.dcPin, displayCfg_.rstPin)
{
    lastMotionMs_ = millis();

    for (uint8_t i = 0; i < DashboardSlotCount; ++i) {
        DashboardSlotConfig& slotCfg = dashboardCfg_[i];
        slotCfg.enabled = true;
        slotCfg.runtimeUiId = kDashboardDefaultRuntimeUiIds[i];
        slotCfg.colorId = kDashboardDefaultColorIds[i];
        snprintf(slotCfg.label, sizeof(slotCfg.label), "%s", kDashboardDefaultLabels[i]);

        dashboardEnabledVars_[i].nvsKey = kSensorEnabledKeys[i];
        dashboardEnabledVars_[i].jsonName = "enabled";
        dashboardEnabledVars_[i].moduleName = kSensorModuleNames[i];
        dashboardEnabledVars_[i].type = ConfigType::Bool;
        dashboardEnabledVars_[i].value = &slotCfg.enabled;
        dashboardEnabledVars_[i].persistence = ConfigPersistence::Persistent;
        dashboardEnabledVars_[i].size = 0U;

        dashboardRuntimeIdVars_[i].nvsKey = kSensorRuntimeIdKeys[i];
        dashboardRuntimeIdVars_[i].jsonName = "runtime_ui_id";
        dashboardRuntimeIdVars_[i].moduleName = kSensorModuleNames[i];
        dashboardRuntimeIdVars_[i].type = ConfigType::UInt16;
        dashboardRuntimeIdVars_[i].value = &slotCfg.runtimeUiId;
        dashboardRuntimeIdVars_[i].persistence = ConfigPersistence::Persistent;
        dashboardRuntimeIdVars_[i].size = 0U;

        dashboardLabelVars_[i].nvsKey = kSensorLabelKeys[i];
        dashboardLabelVars_[i].jsonName = "label";
        dashboardLabelVars_[i].moduleName = kSensorModuleNames[i];
        dashboardLabelVars_[i].type = ConfigType::CharArray;
        dashboardLabelVars_[i].value = slotCfg.label;
        dashboardLabelVars_[i].persistence = ConfigPersistence::Persistent;
        dashboardLabelVars_[i].size = sizeof(slotCfg.label);

        dashboardColorIdVars_[i].nvsKey = kSensorColorIdKeys[i];
        dashboardColorIdVars_[i].jsonName = "color_id";
        dashboardColorIdVars_[i].moduleName = kSensorModuleNames[i];
        dashboardColorIdVars_[i].type = ConfigType::UInt8;
        dashboardColorIdVars_[i].value = &slotCfg.colorId;
        dashboardColorIdVars_[i].persistence = ConfigPersistence::Persistent;
        dashboardColorIdVars_[i].size = 0U;
    }

    for (uint8_t i = 0; i < AlarmDashboardSlotCount; ++i) {
        AlarmSlotConfig& slotCfg = alarmDashboardCfg_[i];
        slotCfg.enabled = kAlarmDefaultEnabled[i];
        slotCfg.alarmId = kAlarmDefaultIds[i];
        slotCfg.colorId = kAlarmDefaultColorIds[i];
        snprintf(slotCfg.label, sizeof(slotCfg.label), "%s", kAlarmDefaultLabels[i]);

        alarmEnabledVars_[i].nvsKey = kAlarmEnabledKeys[i];
        alarmEnabledVars_[i].jsonName = "enabled";
        alarmEnabledVars_[i].moduleName = kAlarmModuleNames[i];
        alarmEnabledVars_[i].type = ConfigType::Bool;
        alarmEnabledVars_[i].value = &slotCfg.enabled;
        alarmEnabledVars_[i].persistence = ConfigPersistence::Persistent;
        alarmEnabledVars_[i].size = 0U;

        alarmIdVars_[i].nvsKey = kAlarmIdKeys[i];
        alarmIdVars_[i].jsonName = "alarm_id";
        alarmIdVars_[i].moduleName = kAlarmModuleNames[i];
        alarmIdVars_[i].type = ConfigType::UInt16;
        alarmIdVars_[i].value = &slotCfg.alarmId;
        alarmIdVars_[i].persistence = ConfigPersistence::Persistent;
        alarmIdVars_[i].size = 0U;

        alarmLabelVars_[i].nvsKey = kAlarmLabelKeys[i];
        alarmLabelVars_[i].jsonName = "label";
        alarmLabelVars_[i].moduleName = kAlarmModuleNames[i];
        alarmLabelVars_[i].type = ConfigType::CharArray;
        alarmLabelVars_[i].value = slotCfg.label;
        alarmLabelVars_[i].persistence = ConfigPersistence::Persistent;
        alarmLabelVars_[i].size = sizeof(slotCfg.label);

        alarmColorIdVars_[i].nvsKey = kAlarmColorIdKeys[i];
        alarmColorIdVars_[i].jsonName = "color_id";
        alarmColorIdVars_[i].moduleName = kAlarmModuleNames[i];
        alarmColorIdVars_[i].type = ConfigType::UInt8;
        alarmColorIdVars_[i].value = &slotCfg.colorId;
        alarmColorIdVars_[i].persistence = ConfigPersistence::Persistent;
        alarmColorIdVars_[i].size = 0U;
    }
}

St7789DisplaySpec TFTModuleS3::displaySpecFromBoard_(const BoardSpec& board)
{
    const SupervisorBoardSpec* sup = boardSupervisorConfig(board);
    return sup ? sup->display : fallbackBoardSpec_().display;
}

void TFTModuleS3::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;
    constexpr uint8_t module = (uint8_t)ConfigModuleId::TftS3;
    cfg.registerVar(enabledVar_, module, kCfgBranch);
    cfg.registerVar(autoOffVar_, module, kCfgBranch);
    cfg.registerVar(motionIoIdVar_, module, kCfgBranch);

    for (uint8_t i = 0; i < DashboardSlotCount; ++i) {
        const uint8_t branch = (uint8_t)(kCfgBranchSensorBase + i);
        cfg.registerVar(dashboardEnabledVars_[i], module, branch);
        cfg.registerVar(dashboardRuntimeIdVars_[i], module, branch);
        cfg.registerVar(dashboardLabelVars_[i], module, branch);
        cfg.registerVar(dashboardColorIdVars_[i], module, branch);
    }

    for (uint8_t i = 0; i < AlarmDashboardSlotCount; ++i) {
        const uint8_t branch = (uint8_t)(kCfgBranchAlarmBase + i);
        cfg.registerVar(alarmEnabledVars_[i], module, branch);
        cfg.registerVar(alarmIdVars_[i], module, branch);
        cfg.registerVar(alarmLabelVars_[i], module, branch);
        cfg.registerVar(alarmColorIdVars_[i], module, branch);
    }

    alarmSvc_ = services.get<AlarmService>(ServiceId::Alarm);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    dsSvc_ = services.get<DataStoreService>(ServiceId::DataStore);
    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;

    if (eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &TFTModuleS3::onEventStatic_, this);
        eventBus_->subscribe(EventId::DataChanged, &TFTModuleS3::onEventStatic_, this);
    }
}

void TFTModuleS3::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!ioSvc_) ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    if (!cfgMqttPubConfigured_) {
        cfgMqttPub_.configure(this,
                              kCfgProducerId,
                              kCfgRoutes,
                              (uint8_t)(sizeof(kCfgRoutes) / sizeof(kCfgRoutes[0])),
                              services);
        cfgMqttPubConfigured_ = true;
    }
    resetMotionInput_();
    redrawRequested_ = true;
}

void TFTModuleS3::loop()
{
    if (!cfgData_.enabled) {
        applyBacklight_(false);
        delay(250);
        return;
    }

    if (!beginDisplay_()) {
        delay(500);
        return;
    }

    updateBacklight_();
    const uint32_t now = millis();
    if (!splashHeld_) {
        if ((int32_t)(now - splashHoldUntilMs_) < 0) {
            delay(30);
            return;
        }
        splashHeld_ = true;
        pageCycleStartMs_ = now;
        lastRenderedPageCycle_ = 0xFFFFFFFFU;
        redrawRequested_ = true;
    }

    const uint32_t pageCycle = currentPageCycle_();
    const bool pageChanged = pageCycle != lastRenderedPageCycle_;
    const bool fullRedraw = !layoutDrawn_ || pageChanged;
    if (fullRedraw || redrawRequested_ || (now - lastRenderMs_) >= kRenderPeriodMs) {
        render_(fullRedraw);
        lastRenderedPageCycle_ = pageCycle;
    }
    delay(30);
}

void TFTModuleS3::onEventStatic_(const Event& e, void* user)
{
    TFTModuleS3* self = static_cast<TFTModuleS3*>(user);
    if (self) self->onEvent_(e);
}

void TFTModuleS3::onEvent_(const Event& e)
{
    if (e.id == EventId::ConfigChanged && e.payload && e.len >= sizeof(ConfigChangedPayload)) {
        const ConfigChangedPayload* p = static_cast<const ConfigChangedPayload*>(e.payload);
        if (p->moduleId == (uint8_t)ConfigModuleId::TftS3) {
            resetMotionInput_();
            invalidateRenderCache_();
            redrawRequested_ = true;
        }
        return;
    }
    if (e.id == EventId::DataChanged) {
        redrawRequested_ = true;
    }
}

bool TFTModuleS3::beginDisplay_()
{
    if (displayReady_) return true;
    if (displayCfg_.csPin < 0 || displayCfg_.dcPin < 0 || displayCfg_.mosiPin < 0 || displayCfg_.sclkPin < 0) {
        LOGW("TFT disabled: invalid SPI pins");
        return false;
    }

    spiBus_.begin(displayCfg_.sclkPin, displayCfg_.misoPin, displayCfg_.mosiPin, displayCfg_.csPin);
    if (displayCfg_.backlightPin >= 0) {
        pinMode(displayCfg_.backlightPin, OUTPUT);
        digitalWrite(displayCfg_.backlightPin, HIGH);
        backlightOn_ = true;
    }
    display_.setSPISpeed(displayCfg_.spiHz);
    display_.init(displayCfg_.resX, displayCfg_.resY);
    display_.setColRowStart(displayCfg_.colStart, displayCfg_.rowStart);
    display_.setRotation(displayCfg_.rotation & 0x03U);
    display_.invertDisplay(displayCfg_.invertColors);
    display_.setTextWrap(false);
    display_.fillScreen(color_(kColorBg));
    displayReady_ = true;
    invalidateRenderCache_();
    redrawRequested_ = true;
    splashHeld_ = false;
    splashHoldUntilMs_ = millis() + kStartupSplashHoldMs;
    pageCycleStartMs_ = 0;
    lastRenderedPageCycle_ = 0xFFFFFFFFU;
    drawBootLogo_();
    LOGI("TFT S3 ready %ux%u", (unsigned)displayCfg_.resX, (unsigned)displayCfg_.resY);
    return true;
}

void TFTModuleS3::applyBacklight_(bool on)
{
    if (backlightOn_ == on) return;
    if (displayCfg_.backlightPin >= 0) {
        pinMode(displayCfg_.backlightPin, OUTPUT);
        digitalWrite(displayCfg_.backlightPin, on ? HIGH : LOW);
    }
    backlightOn_ = on;
    if (on) {
        invalidateRenderCache_();
        redrawRequested_ = true;
    }
}

void TFTModuleS3::resetMotionInput_()
{
    motionInputReady_ = false;
    motionReadErrorLogged_ = false;
    lastMotionMs_ = millis();
    if (cfgData_.enabled && displayReady_) applyBacklight_(true);
}

void TFTModuleS3::updateBacklight_()
{
    if (!cfgData_.autoOff60s) {
        applyBacklight_(true);
        return;
    }

    const uint32_t now = millis();
    uint8_t motionActive = 0U;
    IoStatus status = IO_ERR_INVALID_ARG;
    if (cfgData_.motionIoId >= IO_ID_DI_BASE && cfgData_.motionIoId < IO_ID_AI_BASE) {
        status = (ioSvc_ && ioSvc_->readDigital)
            ? ioSvc_->readDigital(ioSvc_->ctx, cfgData_.motionIoId, &motionActive, nullptr, nullptr)
            : IO_ERR_NOT_READY;
    }
    if (status != IO_OK) {
        if (!motionReadErrorLogged_) {
            LOGW("TFT motion input unavailable io_id=%u status=%u; backlight forced on",
                 (unsigned)cfgData_.motionIoId,
                 (unsigned)status);
            motionReadErrorLogged_ = true;
        }
        motionInputReady_ = false;
        lastMotionMs_ = now;
        applyBacklight_(true);
        return;
    }

    if (!motionInputReady_) lastMotionMs_ = now;
    motionInputReady_ = true;
    motionReadErrorLogged_ = false;
    if (motionActive != 0U) lastMotionMs_ = now;
    applyBacklight_((now - lastMotionMs_) < kBacklightTimeoutMs);
}

void TFTModuleS3::render_(bool force)
{
    if (!displayReady_) return;
    if (!backlightOn_ && !force) return;

    const uint32_t now = millis();
    if (!force && (now - lastRenderMs_) < displayCfg_.minRenderGapMs) return;

    const uint32_t pageCycle = currentPageCycle_() % 3U;
    const Page page = pageCycle == 0U ? Page::Overview : (pageCycle == 1U ? Page::Metrics : Page::Alarms);
    const uint8_t pageId = (uint8_t)page;
    const bool fullRedraw = force || !layoutDrawn_ || lastPage_ != pageId;
    if (fullRedraw) {
        drawStaticLayout_(page);
        overviewCache_.valid = false;
        for (uint8_t i = 0U; i < DashboardSlotCount; ++i) {
            dashboardSlotCache_[i].valid = false;
        }
        for (uint8_t i = 0U; i < AlarmDashboardSlotCount; ++i) {
            alarmSlotCache_[i].valid = false;
        }
    }

    if (page == Page::Overview) {
        char timeBuf[16] = "--:--";
        time_t t = time(nullptr);
        if (t > 1600000000) {
            struct tm tmv{};
            localtime_r(&t, &tmv);
            (void)strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tmv);
        }
        const bool apMode = (WiFi.getMode() & WIFI_MODE_AP) != 0 && !WiFi.isConnected();
        const uint8_t bars = WiFi.isConnected() ? barsFromPct_(pctFromRssi_(WiFi.RSSI())) : 0U;

        char ip[20] = {0};
        formatIp_(ip, sizeof(ip));
        OverviewStatusRenderState status{};
        readOverviewStatus_(status);

        const bool headerChanged = !overviewCache_.valid ||
                                   overviewCache_.apMode != apMode ||
                                   overviewCache_.wifiBars != bars ||
                                   strncmp(overviewCache_.timeText, timeBuf, sizeof(overviewCache_.timeText)) != 0;
        if (fullRedraw || headerChanged) {
            drawHeader_(timeBuf, bars, apMode);
        }

        const int16_t w = display_.width();
        const int16_t h = display_.height();
        const Rect leftTop = overviewTopCardRect_(w, 0U);
        const Rect rightTop = overviewTopCardRect_(w, 1U);
        const Rect leftStatus = overviewAlarmCardRect_(w, h, 0U);
        const Rect rightStatus = overviewAlarmCardRect_(w, h, 1U);
        if (fullRedraw || !overviewCache_.valid || strncmp(overviewCache_.ip, ip, sizeof(overviewCache_.ip)) != 0) {
            drawCenteredTextCard_(leftTop.x, leftTop.y, leftTop.w, leftTop.h, ip, true);
        }
        if (fullRedraw || !overviewCache_.valid) {
            drawCenteredTextCard_(rightTop.x, rightTop.y, rightTop.w, rightTop.h, "http://flowio.local", false);
        }
        if (fullRedraw || !overviewCache_.valid ||
            overviewCache_.statusAvailable != status.available ||
            overviewCache_.autoMode != status.autoMode ||
            overviewCache_.phAutoMode != status.phAutoMode ||
            overviewCache_.orpAutoMode != status.orpAutoMode ||
            overviewCache_.swgAutoMode != status.swgAutoMode ||
            overviewCache_.winterMode != status.winterMode) {
            drawStatusCard_(leftStatus.x, leftStatus.y, leftStatus.w, leftStatus.h, 0U, 3U, status);
            drawStatusCard_(rightStatus.x, rightStatus.y, rightStatus.w, rightStatus.h, 3U, 2U, status);
        }

        snprintf(overviewCache_.timeText, sizeof(overviewCache_.timeText), "%s", timeBuf);
        snprintf(overviewCache_.ip, sizeof(overviewCache_.ip), "%s", ip);
        overviewCache_.apMode = apMode;
        overviewCache_.wifiBars = bars;
        overviewCache_.statusAvailable = status.available;
        overviewCache_.autoMode = status.autoMode;
        overviewCache_.phAutoMode = status.phAutoMode;
        overviewCache_.orpAutoMode = status.orpAutoMode;
        overviewCache_.swgAutoMode = status.swgAutoMode;
        overviewCache_.winterMode = status.winterMode;
        overviewCache_.valid = true;
    } else if (page == Page::Metrics) {
        const int16_t w = display_.width();
        const int16_t h = display_.height();
        for (uint8_t i = 0U; i < DashboardSlotCount; ++i) {
            DashboardSlotRenderState state{};
            readDashboardSlotState_(i, state);
            if (fullRedraw || !dashboardSlotStateEquals_(dashboardSlotCache_[i], state)) {
                const Rect r = metricCardRect_(w, h, i);
                drawDashboardSlot_(i, r.x, r.y, r.w, r.h, state);
                dashboardSlotCache_[i] = state;
            }
        }
    } else {
        const int16_t w = display_.width();
        const int16_t h = display_.height();
        for (uint8_t i = 0U; i < AlarmDashboardSlotCount; ++i) {
            AlarmSlotRenderState state{};
            readAlarmSlotState_(i, state);
            if (fullRedraw || !alarmSlotStateEquals_(alarmSlotCache_[i], state)) {
                const Rect r = metricCardRect_(w, h, i);
                drawAlarmDashboardSlot_(i, r.x, r.y, r.w, r.h, state);
                alarmSlotCache_[i] = state;
            }
        }
    }

    layoutDrawn_ = true;
    lastPage_ = pageId;
    lastRenderMs_ = now;
    redrawRequested_ = false;
}

void TFTModuleS3::invalidateRenderCache_()
{
    layoutDrawn_ = false;
    lastPage_ = 0xFFU;
    overviewCache_.valid = false;
    for (uint8_t i = 0U; i < DashboardSlotCount; ++i) {
        dashboardSlotCache_[i].valid = false;
    }
    for (uint8_t i = 0U; i < AlarmDashboardSlotCount; ++i) {
        alarmSlotCache_[i].valid = false;
    }
}

uint16_t TFTModuleS3::color_(uint16_t rgb565) const
{
    return displayCfg_.swapColorBytes ? (uint16_t)((rgb565 << 8) | (rgb565 >> 8)) : rgb565;
}

void TFTModuleS3::setGfxFont_(const GFXfont* font, uint16_t fg, uint16_t bg)
{
    display_.setFont(font);
    display_.setTextSize(1);
    display_.setTextColor(color_(fg), color_(bg));
}

int16_t TFTModuleS3::gfxTextWidth_(const char* text)
{
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    display_.getTextBounds(text ? text : "", 0, 0, &x1, &y1, &w, &h);
    return (int16_t)w;
}

int16_t TFTModuleS3::gfxBaselineCenteredInBox_(const char* text, int16_t boxY, int16_t boxH)
{
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    display_.getTextBounds(text ? text : "", 0, 0, &x1, &y1, &w, &h);
    return (int16_t)(boxY + ((boxH - (int16_t)h) / 2) - y1);
}

void TFTModuleS3::drawGfxText_(const GFXfont* font, uint16_t fg, uint16_t bg, int16_t x, int16_t baselineY, const char* text)
{
    setGfxFont_(font, fg, bg);
    display_.setCursor(x, baselineY);
    display_.print(text ? text : "");
}

void TFTModuleS3::drawGfxTextCenteredY_(const GFXfont* font,
                                        uint16_t fg,
                                        uint16_t bg,
                                        int16_t x,
                                        int16_t boxY,
                                        int16_t boxH,
                                        const char* text)
{
    setGfxFont_(font, fg, bg);
    display_.setCursor(x, gfxBaselineCenteredInBox_(text, boxY, boxH));
    display_.print(text ? text : "");
}

void TFTModuleS3::drawBootLogo_()
{
    const int16_t w = display_.width();
    const int16_t h = display_.height();
    display_.fillScreen(color_(kColorHeader));
    int16_t x = (int16_t)((w - (int16_t)kFlowIoLogoWidth) / 2);
    int16_t y = (int16_t)(((h - (int16_t)kFlowIoLogoHeight) / 2) - 30);
    if (x < 0) x = 0;
    if (y < 4) y = 4;
    display_.drawRGBBitmap(x, y, kFlowIoLogoBitmap, kFlowIoLogoWidth, kFlowIoLogoHeight);
    int16_t textY = (int16_t)(y + (int16_t)kFlowIoLogoHeight + 8);
    if ((int16_t)(textY + 44) > h) textY = (int16_t)(h - 44);
    drawBrandWordmark_(0, textY, w, 44, &FreeSans18pt7b, kColorHeader);
}

void TFTModuleS3::drawStaticLayout_(Page page)
{
    display_.fillScreen(color_(kColorBg));
    if (page == Page::Overview) {
        display_.fillRect(0, 0, display_.width(), kHeaderH, color_(kColorHeader));
    }
}

void TFTModuleS3::drawWifiBars_(int16_t x, int16_t y, uint8_t bars)
{
    static constexpr uint16_t kWifiColors[] = {
        rgb565_(92, 147, 255),
        rgb565_(78, 135, 252),
        rgb565_(62, 122, 247),
        rgb565_(40, 201, 171),
        rgb565_(30, 181, 136),
    };
    static constexpr int16_t kBarW = 7;
    static constexpr int16_t kStep = 10;
    static constexpr int16_t kUnitH = 5;
    for (uint8_t i = 1U; i <= 5U; ++i) {
        const int16_t h = (int16_t)(i * kUnitH);
        const int16_t yi = (int16_t)(y + (5 * kUnitH) - h);
        const uint16_t c = (i <= bars) ? kWifiColors[i - 1U] : kColorWifiOff;
        display_.fillRoundRect((int16_t)(x + ((i - 1U) * kStep)), yi, kBarW, h, 2, color_(c));
    }
}

void TFTModuleS3::drawBrandWordmark_(int16_t x, int16_t y, int16_t w, int16_t h, const GFXfont* font, uint16_t bg)
{
    static constexpr char kBrandFlow[] = "flow";
    static constexpr char kBrandIo[] = ".io";
    static constexpr char kBrandFull[] = "flow.io";

    setGfxFont_(font, kColorBrandFlow, bg);
    const int16_t flowW = gfxTextWidth_(kBrandFlow);
    setGfxFont_(font, kColorBrandIo, bg);
    const int16_t ioW = gfxTextWidth_(kBrandIo);
    const int16_t startX = (int16_t)(x + ((w - (flowW + ioW)) / 2));
    const int16_t baseY = gfxBaselineCenteredInBox_(kBrandFull, y, h);

    setGfxFont_(font, kColorBrandFlow, bg);
    display_.setCursor(startX, baseY);
    display_.print(kBrandFlow);
    setGfxFont_(font, kColorBrandIo, bg);
    display_.setCursor((int16_t)(startX + flowW), baseY);
    display_.print(kBrandIo);
}

void TFTModuleS3::drawHeader_(const char* timeText, uint8_t wifiBars, bool apMode)
{
    const int16_t w = display_.width();
    display_.fillRect(0, 0, w, kHeaderH, color_(kColorHeader));
    if (apMode) {
        drawGfxTextCenteredY_(&FreeSansBold12pt7b, kColorOn, kColorHeader, 9, 0, kHeaderH, "AP");
    } else {
        drawWifiBars_(8, 12, wifiBars);
    }
    drawBrandWordmark_(0, 7, w, 34, &FreeSansBold12pt7b, kColorHeader);
    setGfxFont_(&FreeSansBold12pt7b, kColorText, kColorHeader);
    const char* cleanTime = (timeText && timeText[0] != '\0') ? timeText : "--:--";
    const int16_t tw = gfxTextWidth_(cleanTime);
    display_.setCursor((int16_t)(w - tw - 12), 31);
    display_.print(cleanTime);
}

void TFTModuleS3::drawCenteredTextCard_(int16_t x, int16_t y, int16_t w, int16_t h, const char* text, bool alignLeft)
{
    display_.fillRoundRect(x, y, w, h, 12, color_(kColorCardBg));
    display_.drawRoundRect(x, y, w, h, 12, color_(kColorCardBorder));
    const char* value = (text && text[0] != '\0') ? text : "--";
    setGfxFont_(&FreeSans9pt7b, kColorValue, kColorCardBg);
    int16_t tw = gfxTextWidth_(value);
    if (tw > (int16_t)(w - 12)) {
        display_.setFont(nullptr);
        display_.setTextSize(1);
        display_.setTextColor(color_(kColorValue), color_(kColorCardBg));
        tw = (int16_t)(strlen(value) * 6U);
        display_.setCursor(alignLeft ? (int16_t)(x + 8) : (int16_t)(x + ((w - tw) / 2)), (int16_t)(y + ((h - 8) / 2) + 5));
        display_.print(value);
        return;
    }
    const int16_t textX = alignLeft ? (int16_t)(x + 8) : (int16_t)(x + ((w - tw) / 2));
    display_.setCursor(textX, (int16_t)(gfxBaselineCenteredInBox_(value, (int16_t)(y - 2), (int16_t)(h - 8)) + 7));
    display_.print(value);
}

void TFTModuleS3::drawAlarmIndicator_(int16_t cx, int16_t cy, uint16_t fill, bool active)
{
    display_.fillCircle(cx, cy, 11, color_(fill));
    const uint16_t markColor = color_(kColorCheckMark);
    if (!active) {
        for (int8_t off = -1; off <= 1; ++off) {
            display_.drawLine((int16_t)(cx - 5), (int16_t)(cy + off), (int16_t)(cx - 1), (int16_t)(cy + 4 + off), markColor);
            display_.drawLine((int16_t)(cx - 1), (int16_t)(cy + 4 + off), (int16_t)(cx + 6), (int16_t)(cy - 4 + off), markColor);
        }
        return;
    }
    display_.fillRoundRect((int16_t)(cx - 2), (int16_t)(cy - 7), 4, 10, 2, markColor);
    display_.fillCircle(cx, (int16_t)(cy + 6), 2, markColor);
}

void TFTModuleS3::drawStatusIndicator_(int16_t cx, int16_t cy, bool on, bool known)
{
    const uint16_t fill = !known ? kColorWifiOff : (on ? kColorOn : kColorOff);
    display_.fillCircle(cx, cy, 11, color_(fill));
    const uint16_t markColor = color_(kColorCheckMark);
    if (known && on) {
        for (int8_t off = -1; off <= 1; ++off) {
            display_.drawLine((int16_t)(cx - 5), (int16_t)(cy + off), (int16_t)(cx - 1), (int16_t)(cy + 4 + off), markColor);
            display_.drawLine((int16_t)(cx - 1), (int16_t)(cy + 4 + off), (int16_t)(cx + 6), (int16_t)(cy - 4 + off), markColor);
        }
        return;
    }
    display_.fillRoundRect((int16_t)(cx - 5), (int16_t)(cy - 1), 10, 3, 1, markColor);
}

void TFTModuleS3::drawStatusCard_(int16_t x,
                                  int16_t y,
                                  int16_t w,
                                  int16_t h,
                                  uint8_t startSlot,
                                  uint8_t count,
                                  const OverviewStatusRenderState& state)
{
    display_.fillRoundRect(x, y, w, h, 12, color_(kColorCardBg));
    display_.drawRoundRect(x, y, w, h, 12, color_(kColorCardBorder));

    const int16_t iconCx = (int16_t)(x + w - 22);
    const int16_t labelX = (int16_t)(x + 12);
    const int16_t topPad = 18;
    const int16_t bottomPad = 18;
    const int16_t rowSpan = (int16_t)(h - topPad - bottomPad);
    const uint8_t rows = count > 0U ? count : 1U;
    const int16_t rowGap = rows > 1U ? (int16_t)(rowSpan / (int16_t)(rows - 1U)) : 0;

    for (uint8_t i = 0U; i < count; ++i) {
        const uint8_t statusIndex = (uint8_t)(startSlot + i);
        if (statusIndex >= kOverviewStatusCount) break;

        bool on = false;
        switch (statusIndex) {
            case 0: on = state.autoMode; break;
            case 1: on = state.phAutoMode; break;
            case 2: on = state.orpAutoMode; break;
            case 3: on = state.swgAutoMode; break;
            case 4: on = state.winterMode; break;
            default: break;
        }

        const int16_t rowCenterY = rows > 1U
            ? (int16_t)(y + topPad + ((int16_t)i * rowGap))
            : (int16_t)(y + (h / 2));
        const uint16_t textColor = (state.available && on) ? kColorText : kColorMuted;
        drawGfxTextCenteredY_(&FreeSans9pt7b,
                              textColor,
                              kColorCardBg,
                              labelX,
                              (int16_t)(rowCenterY - 12),
                              24,
                              kOverviewStatusLabels[statusIndex]);
        drawStatusIndicator_(iconCx, rowCenterY, on, state.available);
    }
}

void TFTModuleS3::drawAlarmCard_(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t startSlot)
{
    uint32_t activeMask = 0U;
    uint32_t resettableMask = 0U;
    uint32_t conditionMask = 0U;
    loadAlarmMasks_(activeMask, resettableMask, conditionMask);
    (void)resettableMask;

    display_.fillRoundRect(x, y, w, h, 12, color_(kColorCardBg));
    display_.drawRoundRect(x, y, w, h, 12, color_(kColorCardBorder));

    const int16_t iconCx = (int16_t)(x + w - 22);
    const int16_t labelX = (int16_t)(x + 12);
    const int16_t topPad = 18;
    const int16_t bottomPad = 18;
    const int16_t rowSpan = (int16_t)(h - topPad - bottomPad);
    const int16_t rowGap = (int16_t)(rowSpan / 3);
    for (uint8_t i = 0U; i < 4U; ++i) {
        const uint8_t alarmIndex = (uint8_t)(startSlot + i);
        if (alarmIndex >= kAlarmSlotCount) break;
        const uint32_t mask = (alarmIndex < 32U) ? (1UL << alarmIndex) : 0U;
        const char* label = runtimeUiAlarmActiveFlagLabel(mask, "fr");
        if (!label || label[0] == '\0') label = "Alarme";
        const int16_t rowCenterY = (int16_t)(y + topPad + ((int16_t)i * rowGap));
        const uint16_t textColor = alarmTextColor_(activeMask, alarmIndex);
        const uint16_t indicatorColor = alarmIndicatorColor_(activeMask, conditionMask, alarmIndex);
        const bool active = (activeMask & mask) != 0U;
        drawGfxTextCenteredY_(&FreeSans9pt7b, textColor, kColorCardBg, labelX, (int16_t)(rowCenterY - 12), 24, label);
        drawAlarmIndicator_(iconCx, rowCenterY, indicatorColor, active);
    }
}

void TFTModuleS3::drawOverviewPage_()
{
    const int16_t w = display_.width();
    const int16_t h = display_.height();
    char ip[20] = {0};
    formatIp_(ip, sizeof(ip));

    const Rect leftTop = overviewTopCardRect_(w, 0U);
    const Rect rightTop = overviewTopCardRect_(w, 1U);
    const Rect leftStatus = overviewAlarmCardRect_(w, h, 0U);
    const Rect rightStatus = overviewAlarmCardRect_(w, h, 1U);
    OverviewStatusRenderState status{};
    readOverviewStatus_(status);
    drawCenteredTextCard_(leftTop.x, leftTop.y, leftTop.w, leftTop.h, ip, true);
    drawCenteredTextCard_(rightTop.x, rightTop.y, rightTop.w, rightTop.h, "http://flowio.local", false);
    drawStatusCard_(leftStatus.x, leftStatus.y, leftStatus.w, leftStatus.h, 0U, 3U, status);
    drawStatusCard_(rightStatus.x, rightStatus.y, rightStatus.w, rightStatus.h, 3U, 2U, status);
}

void TFTModuleS3::drawMetricsPage_()
{
    const int16_t w = display_.width();
    const int16_t h = display_.height();
    for (uint8_t i = 0; i < DashboardSlotCount; ++i) {
        const Rect r = metricCardRect_(w, h, i);
        drawDashboardSlot_(i, r.x, r.y, r.w, r.h);
    }
}

void TFTModuleS3::drawAlarmsPage_()
{
    const int16_t w = display_.width();
    const int16_t h = display_.height();
    for (uint8_t i = 0; i < AlarmDashboardSlotCount; ++i) {
        const Rect r = metricCardRect_(w, h, i);
        drawAlarmDashboardSlot_(i, r.x, r.y, r.w, r.h);
    }
}

void TFTModuleS3::drawDashboardSlot_(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h)
{
    DashboardSlotRenderState state{};
    readDashboardSlotState_(slot, state);
    drawDashboardSlot_(slot, x, y, w, h, state);
}

void TFTModuleS3::drawDashboardSlot_(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, const DashboardSlotRenderState& state)
{
    (void)slot;
    const uint16_t bg = color_(state.cardBg);
    display_.fillRoundRect(x, y, w, h, 10, bg);
    display_.drawRoundRect(x, y, w, h, 10, color_(kColorCardBorder));

    const uint16_t labelColor = state.enabled ? kColorMuted : kColorOff;
    const uint16_t valueColor = (state.enabled && state.available) ? kColorValue : kColorMuted;
    if (state.enabled && state.label[0] != '\0') {
        drawGfxText_(&FreeSans9pt7b, labelColor, state.cardBg, (int16_t)(x + 12), (int16_t)(y + 18), state.label);
    }

    const int16_t valueBaseY = (int16_t)(y + h - 11);
    if (state.value[0] != '\0') {
        setGfxFont_(&FreeSansBold12pt7b, valueColor, state.cardBg);
        display_.setCursor((int16_t)(x + 12), valueBaseY);
        display_.print(state.value);
    }

    if (state.value[0] != '\0' && state.unit[0] != '\0') {
        setGfxFont_(&FreeSans9pt7b, kColorMuted, state.cardBg);
        if (isDegreeCUnit_(state.unit)) {
            const int16_t cw = gfxTextWidth_("C");
            const int16_t unitX = (int16_t)(x + w - cw - 18);
            display_.drawCircle((int16_t)(unitX - 6), (int16_t)(valueBaseY - 11), 2, color_(kColorMuted));
            display_.setCursor(unitX, valueBaseY);
            display_.print("C");
        } else {
            const int16_t unitW = gfxTextWidth_(state.unit);
            const int16_t unitX = (int16_t)(x + w - unitW - 12);
            display_.setCursor(unitX, valueBaseY);
            display_.print(state.unit);
        }
    }
}

void TFTModuleS3::readDashboardSlotState_(uint8_t slot, DashboardSlotRenderState& state) const
{
    state = DashboardSlotRenderState{};
    if (slot >= DashboardSlotCount) return;

    const DashboardSlotConfig& cfg = dashboardCfg_[slot];
    state.valid = true;
    state.enabled = cfg.enabled;
    state.cardBg = cfg.enabled ? dashboardColor_(cfg.colorId, slot) : kColorCardBg;
    slotLabel_(slot, state.label, sizeof(state.label));

    RuntimeValue rv{};
    state.available = cfg.enabled &&
                      cfg.runtimeUiId != 0U &&
                      runtimeUiAllowed_(cfg.runtimeUiId) &&
                      readRuntimeValue_(cfg.runtimeUiId, rv);
    rv.available = state.available;
    formatRuntimeValue_(cfg.runtimeUiId, rv, state.value, sizeof(state.value), state.unit, sizeof(state.unit));
}

void TFTModuleS3::drawAlarmDashboardSlot_(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h)
{
    AlarmSlotRenderState state{};
    readAlarmSlotState_(slot, state);
    drawAlarmDashboardSlot_(slot, x, y, w, h, state);
}

void TFTModuleS3::drawAlarmDashboardSlot_(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, const AlarmSlotRenderState& state)
{
    (void)slot;
    if (!state.enabled) {
        display_.fillRect(x, y, w, h, color_(kColorBg));
        return;
    }

    const uint16_t bg = color_(state.cardBg);
    display_.fillRoundRect(x, y, w, h, 10, bg);
    display_.drawRoundRect(x, y, w, h, 10, color_(kColorCardBorder));

    const uint16_t labelColor = kColorValue;
    const char* label = state.label[0] != '\0' ? state.label : "Alarme";
    drawGfxText_(&FreeSans9pt7b, labelColor, state.cardBg, (int16_t)(x + 12), (int16_t)(y + 18), label);

    const int16_t iconY = (int16_t)(y + h - 18);
    const int16_t condX = (int16_t)(x + w - 19);
    const int16_t latchX = (int16_t)(condX - 28);
    drawAlarmStateIcon_(latchX, iconY, "L", state.latched, state.available);
    drawAlarmStateIcon_(condX, iconY, "C", state.conditionTrue, state.available && state.conditionKnown);
}

void TFTModuleS3::drawAlarmStateIcon_(int16_t cx, int16_t cy, const char* text, bool on, bool known)
{
    const uint16_t fill = !known ? kColorOff : (on ? kColorAlarmAct : kColorGaugeOk);
    display_.fillCircle(cx, cy, 10, color_(fill));
    display_.drawCircle(cx, cy, 10, color_(kColorCardBorder));

    display_.setFont(nullptr);
    display_.setTextSize(1);
    display_.setTextColor(color_(kColorCheckMark), color_(fill));
    display_.setCursor((int16_t)(cx - 3), (int16_t)(cy - 3));
    display_.print(text ? text : "");
}

void TFTModuleS3::readAlarmSlotState_(uint8_t slot, AlarmSlotRenderState& state) const
{
    state = AlarmSlotRenderState{};
    if (slot >= AlarmDashboardSlotCount) return;

    const AlarmSlotConfig& cfg = alarmDashboardCfg_[slot];
    state.valid = true;
    state.enabled = cfg.enabled;
    state.alarmId = cfg.alarmId;
    state.cardBg = cfg.enabled ? dashboardColor_(cfg.colorId, slot) : kColorCardBg;
    if (!cfg.enabled) return;

    if (cfg.label[0] != '\0') {
        snprintf(state.label, sizeof(state.label), "%s", cfg.label);
    } else {
        snprintf(state.label, sizeof(state.label), "%s", alarmIdLabel_(cfg.alarmId));
    }

    if (cfg.enabled && cfg.alarmId != 0U) {
        (void)readAlarmRuntimeState_(cfg.alarmId, state);
    }
}

void TFTModuleS3::formatIp_(char* out, size_t outLen) const
{
    if (!out || outLen == 0U) return;
    const DataStore* ds = dsSvc_ ? dsSvc_->store : nullptr;
    if (!ds || !networkReady(*ds)) {
        snprintf(out, outLen, "-");
        return;
    }
    const IpV4 ip = networkIp(*ds);
    snprintf(out,
             outLen,
             "%u.%u.%u.%u",
             (unsigned)ip.b[0],
             (unsigned)ip.b[1],
             (unsigned)ip.b[2],
             (unsigned)ip.b[3]);
}

bool TFTModuleS3::readRuntimeValue_(RuntimeUiId runtimeId, char* out, size_t outLen) const
{
    if (!out || outLen == 0U) return false;
    RuntimeValue value{};
    if (!readRuntimeValue_(runtimeId, value)) return false;
    char unit[8] = {0};
    formatRuntimeValue_(runtimeId, value, out, outLen, unit, sizeof(unit));
    if (unit[0] != '\0') {
        const size_t n = strnlen(out, outLen);
        if (n + 1U < outLen) snprintf(out + n, outLen - n, " %s", unit);
    }
    return out[0] != '\0';
}

bool TFTModuleS3::readRuntimeValue_(RuntimeUiId runtimeId, RuntimeValue& out) const
{
    if (!runtimeUiAllowed_(runtimeId)) return false;
    const DataStore* ds = dsSvc_ ? dsSvc_->store : nullptr;
    const ModuleId module = (ModuleId)runtimeUiModuleId(runtimeId);
    const uint8_t valueId = runtimeUiValueId(runtimeId);

    switch (module) {
        case ModuleId::Alarm: {
            uint32_t activeMask = 0U;
            uint32_t resettableMask = 0U;
            uint32_t conditionMask = 0U;
            loadAlarmMasks_(activeMask, resettableMask, conditionMask);
            out.available = true;
            out.wireType = RuntimeUiWireType::UInt32;
            if (valueId == 1U) out.u32Value = activeMask;
            else if (valueId == 2U) out.u32Value = resettableMask;
            else if (valueId == 3U) out.u32Value = conditionMask;
            else return false;
            return true;
        }

        case ModuleId::PoolLogic: {
            bool autoMode = false;
            bool winterMode = false;
            bool phAutoMode = false;
            bool orpAutoMode = false;
            bool swgAutoMode = false;
            if (!loadPoolModeFlags_(autoMode, winterMode, phAutoMode, orpAutoMode, swgAutoMode)) return false;
            (void)swgAutoMode;
            out.available = true;
            out.wireType = RuntimeUiWireType::Bool;
            if (valueId == 1U) out.boolValue = autoMode;
            else if (valueId == 2U) out.boolValue = winterMode;
            else if (valueId == 3U) out.boolValue = phAutoMode;
            else if (valueId == 4U) out.boolValue = orpAutoMode;
            else return false;
            return true;
        }

        case ModuleId::Io:
            switch (valueId) {
                case 1: return readIoValue_(ioIdFromSlot(analogInputSlot(4)), out);
                case 2: return readIoValue_(ioIdFromSlot(analogInputSlot(5)), out);
                case 3: return readIoValue_(ioIdFromSlot(analogInputSlot(1)), out);
                case 4: return readIoValue_(ioIdFromSlot(analogInputSlot(0)), out);
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
                case 5: return readIoValue_(ioIdFromSlot(digitalInputSlot(12)), out);
#else
                case 5: return readIoValue_(ioIdFromSlot(digitalInputSlot(3)), out);
#endif
                case 6: return readIoValue_(ioIdFromSlot(analogInputSlot(2)), out);
                case 7: return readIoBackendValue_(IO_BACKEND_BMP280, 0U, out);
                case 8: return readIoBackendValue_(IO_BACKEND_BME680, 0U, out);
                case 9: return readIoBackendValue_(IO_BACKEND_BMP280, 1U, out);
                case 10: return readIoBackendValue_(IO_BACKEND_SHT40, 0U, out);
                case 11: return readIoBackendValue_(IO_BACKEND_SHT40, 1U, out);
                case 12: return readIoBackendValue_(IO_BACKEND_BME680, 1U, out);
                case 13: return readIoBackendValue_(IO_BACKEND_BME680, 2U, out);
                case 14: return readIoBackendValue_(IO_BACKEND_BME680, 3U, out);
                default: return false;
            }

        case ModuleId::Wifi:
            if (!ds) return false;
            if (valueId == 1U) {
                out.available = true;
                out.wireType = RuntimeUiWireType::Bool;
                out.boolValue = wifiReady(*ds);
                return true;
            }
            if (valueId == 2U) {
                out.available = true;
                out.wireType = RuntimeUiWireType::String;
                formatIp_(out.stringValue, sizeof(out.stringValue));
                return true;
            }
            if (valueId == 3U) {
                if (!WiFi.isConnected()) return false;
                out.available = true;
                out.wireType = RuntimeUiWireType::Int32;
                out.i32Value = (int32_t)WiFi.RSSI();
                return true;
            }
            return false;

        case ModuleId::Mqtt:
            if (!ds) return false;
            if (valueId == 1U) {
                out.available = true;
                out.wireType = RuntimeUiWireType::Bool;
                out.boolValue = mqttReady(*ds);
                return true;
            }
            if (valueId == 3U) {
                out.available = true;
                out.wireType = RuntimeUiWireType::UInt32;
                out.u32Value = mqttRxDrop(*ds);
                return true;
            }
            if (valueId == 4U) {
                out.available = true;
                out.wireType = RuntimeUiWireType::UInt32;
                out.u32Value = mqttParseFail(*ds);
                return true;
            }
            if (valueId == 5U) {
                out.available = true;
                out.wireType = RuntimeUiWireType::UInt32;
                out.u32Value = mqttHandlerFail(*ds);
                return true;
            }
            if (valueId == 6U) {
                out.available = true;
                out.wireType = RuntimeUiWireType::UInt32;
                out.u32Value = mqttOversizeDrop(*ds);
                return true;
            }
            return false;

        case ModuleId::System: {
            SystemStatsSnapshot snap{};
            SystemStats::collect(snap);
            if (valueId == 1U) {
                out.available = true;
                out.wireType = RuntimeUiWireType::String;
                snprintf(out.stringValue, sizeof(out.stringValue), "%s", FirmwareVersion::Full);
                return true;
            }
            out.available = true;
            out.wireType = RuntimeUiWireType::UInt32;
            if (valueId == 2U) {
                out.u32Value = (uint32_t)snap.uptimeMs;
                return true;
            }
            if (valueId == 3U) {
                out.u32Value = snap.heap.freeBytes;
                return true;
            }
            if (valueId == 4U) {
                out.u32Value = snap.heap.minFreeBytes;
                return true;
            }
            return false;
        }

        case ModuleId::PoolDevice: {
            if (!ds) return false;
            uint8_t deviceSlot = 0xFFU;
            if (valueId == 1U) deviceSlot = PoolIds::DeviceFiltrationPump;
            else if (valueId == 2U) deviceSlot = PoolIds::DevicePhPump;
            else if (valueId == 3U) deviceSlot = PoolIds::DeviceChlorinePump;
            else if (valueId == 4U) deviceSlot = PoolIds::DeviceRobot;
            else return false;

            PoolDeviceRuntimeStateEntry state{};
            if (!poolDeviceRuntimeState(*ds, deviceSlot, state)) return false;
            out.available = true;
            out.wireType = RuntimeUiWireType::Bool;
            out.boolValue = state.actualOn;
            return true;
        }

        default:
            return false;
    }
}

void TFTModuleS3::readOverviewStatus_(OverviewStatusRenderState& state) const
{
    state = OverviewStatusRenderState{};
    state.available = loadPoolModeFlags_(state.autoMode,
                                         state.winterMode,
                                         state.phAutoMode,
                                         state.orpAutoMode,
                                         state.swgAutoMode);
}

bool TFTModuleS3::readIoValue_(IoId ioId, char* out, size_t outLen) const
{
    if (!out || outLen == 0U) return false;
    RuntimeValue value{};
    if (!readIoValue_(ioId, value)) return false;
    char unit[8] = {0};
    formatRuntimeValue_(0U, value, out, outLen, unit, sizeof(unit));
    return out[0] != '\0';
}

bool TFTModuleS3::readIoValue_(IoId ioId, RuntimeValue& out) const
{
    if (!ioSvc_ || !ioSvc_->readValue) return false;
    IoValue value{};
    const IoStatus st = ioSvc_->readValue(ioSvc_->ctx, ioId, &value);
    if (st != IO_OK || !value.valid) return false;
    out.available = true;
    if (value.type == IO_VAL_BOOL) {
        out.wireType = RuntimeUiWireType::Bool;
        out.boolValue = value.v.b != 0U;
        return true;
    }
    if (value.type == IO_VAL_INT32) {
        out.wireType = RuntimeUiWireType::Int32;
        out.i32Value = value.v.i32;
        return true;
    }
    out.wireType = RuntimeUiWireType::Float32;
    out.f32Value = value.v.f;
    return true;
}

bool TFTModuleS3::readIoBackendValue_(uint8_t backend, uint8_t channel, char* out, size_t outLen) const
{
    if (!out || outLen == 0U) return false;
    RuntimeValue value{};
    if (!readIoBackendValue_(backend, channel, value)) return false;
    char unit[8] = {0};
    formatRuntimeValue_(0U, value, out, outLen, unit, sizeof(unit));
    return out[0] != '\0';
}

bool TFTModuleS3::readIoBackendValue_(uint8_t backend, uint8_t channel, RuntimeValue& out) const
{
    if (!ioSvc_ || !ioSvc_->count || !ioSvc_->idAt || !ioSvc_->meta) return false;
    const uint8_t count = ioSvc_->count(ioSvc_->ctx);
    for (uint8_t i = 0; i < count; ++i) {
        IoId id = IO_ID_INVALID;
        if (ioSvc_->idAt(ioSvc_->ctx, i, &id) != IO_OK) continue;
        IoEndpointMeta meta{};
        if (ioSvc_->meta(ioSvc_->ctx, id, &meta) != IO_OK) continue;
        if (meta.backend == backend && meta.channel == channel) {
            return readIoValue_(id, out);
        }
    }
    return false;
}

const char* TFTModuleS3::runtimeUnit_(RuntimeUiId runtimeId) const
{
    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(runtimeId);
    return (item && item->unit) ? item->unit : "";
}

uint8_t TFTModuleS3::runtimeDecimals_(RuntimeUiId runtimeId, RuntimeUiWireType wireType) const
{
    if (wireType != RuntimeUiWireType::Float32) return 0U;
    if (runtimeId == makeRuntimeUiId(ModuleId::Io, 3)) return 2U;
    if (runtimeId == makeRuntimeUiId(ModuleId::Io, 6)) return 2U;
    const char* unit = runtimeUnit_(runtimeId);
    if (unit && strcmp(unit, "mV") == 0) return 0U;
    return 1U;
}

void TFTModuleS3::formatRuntimeValue_(RuntimeUiId runtimeId,
                                      const RuntimeValue& value,
                                      char* valueOut,
                                      size_t valueOutLen,
                                      char* unitOut,
                                      size_t unitOutLen) const
{
    if (valueOut && valueOutLen > 0U) valueOut[0] = '\0';
    if (unitOut && unitOutLen > 0U) unitOut[0] = '\0';
    if (!valueOut || valueOutLen == 0U || !value.available) return;

    switch (value.wireType) {
        case RuntimeUiWireType::Bool:
            snprintf(valueOut, valueOutLen, "%s", value.boolValue ? "ON" : "OFF");
            return;
        case RuntimeUiWireType::Int32:
            snprintf(valueOut, valueOutLen, "%ld", (long)value.i32Value);
            break;
        case RuntimeUiWireType::UInt32:
        case RuntimeUiWireType::Enum:
            snprintf(valueOut, valueOutLen, "%lu", (unsigned long)value.u32Value);
            break;
        case RuntimeUiWireType::Float32: {
            const uint8_t decimals = runtimeDecimals_(runtimeId, value.wireType);
            if (decimals > 0U) {
                snprintf(valueOut, valueOutLen, "%.*f", (int)decimals, (double)value.f32Value);
                trimTrailingZeros_(valueOut);
            } else {
                snprintf(valueOut, valueOutLen, "%ld", lroundf(value.f32Value));
            }
            break;
        }
        case RuntimeUiWireType::String:
            snprintf(valueOut, valueOutLen, "%s", value.stringValue);
            return;
        case RuntimeUiWireType::NotFound:
        case RuntimeUiWireType::Unavailable:
        default:
            return;
    }

    const char* unit = runtimeUnit_(runtimeId);
    if (unitOut && unitOutLen > 0U && unit && unit[0] != '\0') {
        snprintf(unitOut, unitOutLen, "%s", unit);
    }
}

void TFTModuleS3::slotLabel_(uint8_t slot, char* out, size_t outLen) const
{
    if (!out || outLen == 0U) return;
    out[0] = '\0';
    if (slot >= DashboardSlotCount) return;
    const DashboardSlotConfig& cfg = dashboardCfg_[slot];
    if (cfg.label[0] != '\0') {
        snprintf(out, outLen, "%s", cfg.label);
        return;
    }

    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(cfg.runtimeUiId);
    const char* key = (item && item->key) ? item->key : "";
    const char* src = strrchr(key, '.');
    src = src ? (src + 1) : key;
    if (!src || src[0] == '\0') {
        snprintf(out, outLen, "Slot %u", (unsigned)slot);
        return;
    }

    bool upperNext = true;
    size_t j = 0U;
    for (size_t i = 0U; src[i] != '\0' && (j + 1U) < outLen; ++i) {
        char ch = src[i];
        if (ch == '_' || ch == '-') {
            out[j++] = ' ';
            upperNext = true;
            continue;
        }
        if (upperNext && ch >= 'a' && ch <= 'z') ch = (char)(ch - ('a' - 'A'));
        out[j++] = ch;
        upperNext = false;
    }
    out[j] = '\0';
}

uint16_t TFTModuleS3::dashboardColor_(uint8_t colorId, uint8_t slot) const
{
    for (size_t i = 0; i < (sizeof(kDashboardColorPresets) / sizeof(kDashboardColorPresets[0])); ++i) {
        if (kDashboardColorPresets[i].id == colorId) return kDashboardColorPresets[i].rgb565;
    }
    if (slot < DashboardSlotCount) {
        const uint8_t fallbackId = kDashboardDefaultColorIds[slot];
        for (size_t i = 0; i < (sizeof(kDashboardColorPresets) / sizeof(kDashboardColorPresets[0])); ++i) {
            if (kDashboardColorPresets[i].id == fallbackId) return kDashboardColorPresets[i].rgb565;
        }
    }
    return rgb565_(238, 247, 255);
}

const char* TFTModuleS3::alarmIdLabel_(uint16_t alarmId) const
{
    switch ((AlarmId)alarmId) {
        case AlarmId::PoolPsiLow: return "PSI bas";
        case AlarmId::PoolPsiHigh: return "PSI haut";
        case AlarmId::PoolPhTankLow: return "pH vide";
        case AlarmId::PoolChlorineTankLow: return "Chlore vide";
        case AlarmId::PoolPhPumpMaxUptime: return "pH uptime";
        case AlarmId::PoolChlorinePumpMaxUptime: return "ORP uptime";
        case AlarmId::PoolWaterLevelLow: return "Eau basse";
        case AlarmId::None:
        default: return "Alarme";
    }
}

bool TFTModuleS3::readAlarmRuntimeState_(uint16_t alarmId, AlarmSlotRenderState& state) const
{
    if (!alarmSvc_ || !alarmSvc_->buildAlarmState || alarmId == 0U) return false;

    char stateJson[144] = {0};
    if (!alarmSvc_->buildAlarmState(alarmSvc_->ctx, (AlarmId)alarmId, stateJson, sizeof(stateJson))) return false;

    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, stateJson)) return false;
    state.available = true;
    state.latched = (doc["a"] | 0U) != 0U;
    state.resettable = (doc["r"] | 0U) != 0U;
    const uint8_t condition = doc["c"] | 2U;
    state.conditionKnown = condition != (uint8_t)AlarmCondState::Unknown;
    state.conditionTrue = condition == (uint8_t)AlarmCondState::True;

    const uint8_t alarmSlot = doc["slot"] | 255U;
    const uint32_t mask = (alarmSlot < 32U) ? (1UL << alarmSlot) : 0U;
    if (state.label[0] == '\0' && mask != 0U) {
        const char* runtimeLabel = runtimeUiAlarmActiveFlagLabel(mask, "fr");
        if (runtimeLabel && runtimeLabel[0] != '\0') {
            snprintf(state.label, sizeof(state.label), "%s", runtimeLabel);
        }
    }
    return true;
}

void TFTModuleS3::loadAlarmMasks_(uint32_t& activeMask, uint32_t& resettableMask, uint32_t& conditionMask) const
{
    activeMask = 0U;
    resettableMask = 0U;
    conditionMask = 0U;
    if (!alarmSvc_ || !alarmSvc_->listIds || !alarmSvc_->buildAlarmState) return;

    AlarmId ids[Limits::Alarm::MaxAlarms] = {};
    const uint8_t count = alarmSvc_->listIds(alarmSvc_->ctx, ids, (uint8_t)Limits::Alarm::MaxAlarms);
    for (uint8_t i = 0U; i < count; ++i) {
        char stateJson[144] = {0};
        if (!alarmSvc_->buildAlarmState(alarmSvc_->ctx, ids[i], stateJson, sizeof(stateJson))) continue;

        StaticJsonDocument<192> doc;
        if (deserializeJson(doc, stateJson)) continue;
        const uint8_t slot = doc["slot"] | 255U;
        if (slot >= 32U) continue;
        const uint32_t bit = (1UL << slot);
        if ((doc["a"] | 0U) != 0U) activeMask |= bit;
        if ((doc["r"] | 0U) != 0U) resettableMask |= bit;
        if ((doc["c"] | 0U) == 1U) conditionMask |= bit;
    }
}

bool TFTModuleS3::loadPoolModeFlags_(bool& autoMode,
                                     bool& winterMode,
                                     bool& phAutoMode,
                                     bool& orpAutoMode,
                                     bool& swgAutoMode) const
{
    autoMode = false;
    winterMode = false;
    phAutoMode = false;
    orpAutoMode = false;
    swgAutoMode = false;
    if (!cfgStore_) return false;

    char moduleJson[320] = {0};
    bool truncated = false;
    if (!cfgStore_->toJsonModule("poollogic/modes", moduleJson, sizeof(moduleJson), &truncated, true) || truncated) return false;

    StaticJsonDocument<384> doc;
    if (deserializeJson(doc, moduleJson)) return false;
    JsonObjectConst root = doc.as<JsonObjectConst>();
    if (root.isNull()) return false;
    autoMode = root["auto_mode"] | false;
    winterMode = root["winter_mode"] | false;
    swgAutoMode = ((root["disinfection_type"] | 0) == 1);

    memset(moduleJson, 0, sizeof(moduleJson));
    truncated = false;
    if (cfgStore_->toJsonModule("poollogic/ph", moduleJson, sizeof(moduleJson), &truncated, true) && !truncated) {
        StaticJsonDocument<128> phDoc;
        if (!deserializeJson(phDoc, moduleJson)) {
            JsonObjectConst phRoot = phDoc.as<JsonObjectConst>();
            if (!phRoot.isNull()) phAutoMode = phRoot["ph_auto_mode"] | false;
        }
    }

    memset(moduleJson, 0, sizeof(moduleJson));
    truncated = false;
    if (cfgStore_->toJsonModule("poollogic/chlorine", moduleJson, sizeof(moduleJson), &truncated, true) && !truncated) {
        StaticJsonDocument<128> disDoc;
        if (!deserializeJson(disDoc, moduleJson)) {
            JsonObjectConst disRoot = disDoc.as<JsonObjectConst>();
            if (!disRoot.isNull()) orpAutoMode = disRoot["dis_auto_mode"] | false;
        }
    }
    return true;
}

uint32_t TFTModuleS3::currentPageCycle_() const
{
    if (!splashHeld_) return 0U;
    return (uint32_t)((millis() - pageCycleStartMs_) / kPageRotateMs);
}

bool TFTModuleS3::dashboardSlotStateEquals_(const DashboardSlotRenderState& a, const DashboardSlotRenderState& b)
{
    return a.valid == b.valid &&
           a.enabled == b.enabled &&
           a.available == b.available &&
           a.cardBg == b.cardBg &&
           strncmp(a.label, b.label, sizeof(a.label)) == 0 &&
           strncmp(a.value, b.value, sizeof(a.value)) == 0 &&
           strncmp(a.unit, b.unit, sizeof(a.unit)) == 0;
}

bool TFTModuleS3::alarmSlotStateEquals_(const AlarmSlotRenderState& a, const AlarmSlotRenderState& b)
{
    return a.valid == b.valid &&
           a.enabled == b.enabled &&
           a.available == b.available &&
           a.latched == b.latched &&
           a.resettable == b.resettable &&
           a.conditionTrue == b.conditionTrue &&
           a.conditionKnown == b.conditionKnown &&
           a.cardBg == b.cardBg &&
           a.alarmId == b.alarmId &&
           strncmp(a.label, b.label, sizeof(a.label)) == 0;
}

bool TFTModuleS3::runtimeUiAllowed_(RuntimeUiId runtimeId)
{
    if (!isValidRuntimeUiId(runtimeId)) return false;
    return findRuntimeUiManifestItem(runtimeId) != nullptr;
}
