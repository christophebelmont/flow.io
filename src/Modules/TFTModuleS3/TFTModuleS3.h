#pragma once
/**
 * @file TFTModuleS3.h
 * @brief Local TFT module for Waveshare ESP32-S3 builds.
 */

#include <Adafruit_ST7789.h>
#include <SPI.h>

#include "Board/BoardTypes.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Module.h"
#include "Core/RuntimeUi.h"
#include "Core/Services/Services.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"

struct BoardSpec;

class TFTModuleS3Display : public Adafruit_ST7789 {
public:
    using Adafruit_ST7789::Adafruit_ST7789;
    using Adafruit_ST77xx::setColRowStart;
};

class TFTModuleS3 : public Module {
public:
    static constexpr uint8_t DashboardSlotCount = 8;
    static constexpr uint8_t AlarmDashboardSlotCount = 8;

    explicit TFTModuleS3(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::TftS3; }
    const char* taskName() const override { return "tft.s3"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 5120; }
    UBaseType_t taskStackCaps() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
        return Module::taskStackCaps();
#endif
    }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 8; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::EventBus;
        if (i == 3) return ModuleId::DataStore;
        if (i == 4) return ModuleId::Wifi;
        if (i == 5) return ModuleId::Mqtt;
        if (i == 6) return ModuleId::Alarm;
        if (i == 7) return ModuleId::Io;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry& services) override;
    void loop() override;

private:
    struct ConfigData {
        bool enabled = true;
        bool autoOff60s = true;
        IoId motionIoId = (IoId)(IO_ID_DI_BASE + 8U);
    };

    enum class Page : uint8_t {
        Overview = 0,
        Metrics = 1,
        Alarms = 2,
    };

    struct RuntimeValue {
        bool available = false;
        RuntimeUiWireType wireType = RuntimeUiWireType::Unavailable;
        bool boolValue = false;
        int32_t i32Value = 0;
        uint32_t u32Value = 0U;
        float f32Value = 0.0f;
        char stringValue[64]{};
    };

    struct OverviewRenderCache {
        bool valid = false;
        char timeText[16]{};
        char ip[20]{};
        uint8_t wifiBars = 0U;
        bool apMode = false;
        bool statusAvailable = false;
        bool autoMode = false;
        bool phAutoMode = false;
        bool orpAutoMode = false;
        bool swgAutoMode = false;
        bool winterMode = false;
    };

    struct OverviewStatusRenderState {
        bool available = false;
        bool autoMode = false;
        bool phAutoMode = false;
        bool orpAutoMode = false;
        bool swgAutoMode = false;
        bool winterMode = false;
    };

    struct DashboardSlotRenderState {
        bool valid = false;
        bool enabled = false;
        bool available = false;
        uint16_t cardBg = 0U;
        char label[24]{};
        char value[24]{};
        char unit[8]{};
    };

    struct AlarmSlotRenderState {
        bool valid = false;
        bool enabled = false;
        bool available = false;
        bool latched = false;
        bool resettable = false;
        bool conditionTrue = false;
        bool conditionKnown = false;
        uint16_t cardBg = 0U;
        uint16_t alarmId = 0U;
        char label[24]{};
    };

    static St7789DisplaySpec displaySpecFromBoard_(const BoardSpec& board);
    static void onEventStatic_(const Event& e, void* user);

    void onEvent_(const Event& e);
    bool beginDisplay_();
    void applyBacklight_(bool on);
    void resetMotionInput_();
    void updateBacklight_();
    void render_(bool force);
    void invalidateRenderCache_();
    void drawBootLogo_();
    void drawStaticLayout_(Page page);
    void drawHeader_(const char* timeText, uint8_t wifiBars, bool apMode);
    void drawOverviewPage_();
    void drawStatusCard_(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t startSlot, uint8_t count, const OverviewStatusRenderState& state);
    void drawStatusIndicator_(int16_t cx, int16_t cy, bool on, bool known);
    void readOverviewStatus_(OverviewStatusRenderState& state) const;
    void drawAlarmCard_(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t startSlot);
    void drawAlarmIndicator_(int16_t cx, int16_t cy, uint16_t fill, bool active);
    void drawCenteredTextCard_(int16_t x, int16_t y, int16_t w, int16_t h, const char* text, bool alignLeft);
    void drawMetricsPage_();
    void drawAlarmsPage_();
    uint16_t color_(uint16_t rgb565) const;
    void setGfxFont_(const GFXfont* font, uint16_t fg, uint16_t bg);
    int16_t gfxTextWidth_(const char* text);
    int16_t gfxBaselineCenteredInBox_(const char* text, int16_t boxY, int16_t boxH);
    void drawGfxText_(const GFXfont* font, uint16_t fg, uint16_t bg, int16_t x, int16_t baselineY, const char* text);
    void drawGfxTextCenteredY_(const GFXfont* font, uint16_t fg, uint16_t bg, int16_t x, int16_t boxY, int16_t boxH, const char* text);
    void drawWifiBars_(int16_t x, int16_t y, uint8_t bars);
    void drawBrandWordmark_(int16_t x, int16_t y, int16_t w, int16_t h, const GFXfont* font, uint16_t bg);
    void drawDashboardSlot_(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h);
    void drawDashboardSlot_(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, const DashboardSlotRenderState& state);
    void readDashboardSlotState_(uint8_t slot, DashboardSlotRenderState& state) const;
    void drawAlarmDashboardSlot_(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h);
    void drawAlarmDashboardSlot_(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, const AlarmSlotRenderState& state);
    void drawAlarmStateIcon_(int16_t cx, int16_t cy, const char* text, bool on, bool known);
    void readAlarmSlotState_(uint8_t slot, AlarmSlotRenderState& state) const;
    void formatIp_(char* out, size_t outLen) const;
    bool readRuntimeValue_(RuntimeUiId runtimeId, char* out, size_t outLen) const;
    bool readRuntimeValue_(RuntimeUiId runtimeId, RuntimeValue& out) const;
    bool readIoValue_(IoId ioId, char* out, size_t outLen) const;
    bool readIoValue_(IoId ioId, RuntimeValue& out) const;
    bool readIoBackendValue_(uint8_t backend, uint8_t channel, char* out, size_t outLen) const;
    bool readIoBackendValue_(uint8_t backend, uint8_t channel, RuntimeValue& out) const;
    const char* runtimeUnit_(RuntimeUiId runtimeId) const;
    uint8_t runtimeDecimals_(RuntimeUiId runtimeId, RuntimeUiWireType wireType) const;
    void formatRuntimeValue_(RuntimeUiId runtimeId, const RuntimeValue& value, char* valueOut, size_t valueOutLen, char* unitOut, size_t unitOutLen) const;
    void slotLabel_(uint8_t slot, char* out, size_t outLen) const;
    uint16_t dashboardColor_(uint8_t colorId, uint8_t slot) const;
    const char* alarmIdLabel_(uint16_t alarmId) const;
    bool readAlarmRuntimeState_(uint16_t alarmId, AlarmSlotRenderState& state) const;
    void loadAlarmMasks_(uint32_t& activeMask, uint32_t& resettableMask, uint32_t& conditionMask) const;
    bool loadPoolModeFlags_(bool& autoMode, bool& winterMode, bool& phAutoMode, bool& orpAutoMode, bool& swgAutoMode) const;
    uint32_t currentPageCycle_() const;
    static bool dashboardSlotStateEquals_(const DashboardSlotRenderState& a, const DashboardSlotRenderState& b);
    static bool alarmSlotStateEquals_(const AlarmSlotRenderState& a, const AlarmSlotRenderState& b);
    static bool runtimeUiAllowed_(RuntimeUiId runtimeId);

    St7789DisplaySpec displayCfg_{};
#if defined(VSPI)
    SPIClass spiBus_{VSPI};
#else
    SPIClass spiBus_{HSPI};
#endif
    TFTModuleS3Display display_;
    ConfigData cfgData_{};
    ConfigVariable<bool, 0> enabledVar_{
        NVS_KEY("tfts3en"), "enabled", "tft/s3",
        ConfigType::Bool, &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<bool, 0> autoOffVar_{
        NVS_KEY("tfts3auto"), "auto_off_60s", "tft/s3",
        ConfigType::Bool, &cfgData_.autoOff60s, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<IoId, 0> motionIoIdVar_{
        NVS_KEY("tfts3io"), "motion_io_id", "tft/s3",
        ConfigType::UInt16, &cfgData_.motionIoId, ConfigPersistence::Persistent, 0
    };

    struct DashboardSlotConfig {
        bool enabled = true;
        uint16_t runtimeUiId = 0U;
        char label[24]{};
        uint8_t colorId = 0U;
    };

    struct AlarmSlotConfig {
        bool enabled = true;
        uint16_t alarmId = 0U;
        char label[24]{};
        uint8_t colorId = 0U;
    };

    DashboardSlotConfig dashboardCfg_[DashboardSlotCount]{};
    ConfigVariable<bool, 0> dashboardEnabledVars_[DashboardSlotCount]{};
    ConfigVariable<uint16_t, 0> dashboardRuntimeIdVars_[DashboardSlotCount]{};
    ConfigVariable<char, 0> dashboardLabelVars_[DashboardSlotCount]{};
    ConfigVariable<uint8_t, 0> dashboardColorIdVars_[DashboardSlotCount]{};

    AlarmSlotConfig alarmDashboardCfg_[AlarmDashboardSlotCount]{};
    ConfigVariable<bool, 0> alarmEnabledVars_[AlarmDashboardSlotCount]{};
    ConfigVariable<uint16_t, 0> alarmIdVars_[AlarmDashboardSlotCount]{};
    ConfigVariable<char, 0> alarmLabelVars_[AlarmDashboardSlotCount]{};
    ConfigVariable<uint8_t, 0> alarmColorIdVars_[AlarmDashboardSlotCount]{};

    ConfigStore* cfgStore_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    EventBus* eventBus_ = nullptr;
    MqttConfigRouteProducer cfgMqttPub_{};
    bool cfgMqttPubConfigured_ = false;
    bool displayReady_ = false;
    bool layoutDrawn_ = false;
    bool redrawRequested_ = true;
    bool splashHeld_ = false;
    bool backlightOn_ = false;
    bool motionInputReady_ = false;
    bool motionReadErrorLogged_ = false;
    uint32_t lastMotionMs_ = 0;
    uint32_t lastRenderMs_ = 0;
    uint32_t splashHoldUntilMs_ = 0;
    uint32_t pageCycleStartMs_ = 0;
    uint32_t lastRenderedPageCycle_ = 0xFFFFFFFFU;
    uint8_t lastPage_ = 0xFFU;
    OverviewRenderCache overviewCache_{};
    DashboardSlotRenderState dashboardSlotCache_[DashboardSlotCount]{};
    AlarmSlotRenderState alarmSlotCache_[AlarmDashboardSlotCount]{};
};
