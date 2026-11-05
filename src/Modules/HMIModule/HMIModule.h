#pragma once
/**
 * @file HMIModule.h
 * @brief UI orchestration module (menu model + HMI driver).
 */

#include "App/BuildFlags.h"
#include "Core/I2cBus.h"
#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/ServiceBinding.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Services/Services.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/HMIModule/ConfigMenuModel.h"
#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"
#include "Modules/HMIModule/Drivers/NextionDriver.h"
#include "Modules/HMIModule/Drivers/Pcf8574LedPanelDriver.h"
#include "Modules/HMIModule/Drivers/RemoteHmiUdpDriver.h"
#include "Modules/HMIModule/Drivers/TfaVeniceRf433Sink.h"
#include "Modules/HMIModule/Drivers/Ws2812StatusLedDriver.h"
#include "Modules/Network/HmiUdpServerModule/HmiUdpServerModule.h"

struct BoardSpec;

class HMIModule : public Module {
public:
    HMIModule() = default;
    explicit HMIModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::Hmi; }
    const char* taskName() const override { return "HMI"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 6144; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    uint32_t startDelayMs() const override {
#if FLOW_BUILD_IS_WAVESHARE
        return 0U;
#else
        return 5000U;
#endif
    }

    uint8_t dependencyCount() const override { return 10; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::EventBus;
        if (i == 3) return ModuleId::DataStore;
        if (i == 4) return ModuleId::Io;
        if (i == 5) return ModuleId::Alarm;
        if (i == 6) return ModuleId::Command;
        if (i == 7) return ModuleId::Time;
        if (i == 8) return ModuleId::Wifi;
        if (i == 9) return ModuleId::HmiUdpServer;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;
    void setRemoteUdpServer(HmiUdpServerModule* server);

private:
    struct ConfigData {
        bool ledsEnabled = true;
        bool waveshareLedEnabled =
#if FLOW_BUILD_IS_WAVESHARE
            true;
#else
            false;
#endif
        bool nextionEnabled = true;
        IoId nextionMotionIoId =
#if FLOW_BUILD_IS_WAVESHARE
            (IoId)(IO_ID_DI_BASE + 8U);
#else
            IO_ID_INVALID;
#endif
        bool remoteUdpEnabled =
#ifdef FLOW_HMI_REMOTE_UDP
            (FLOW_HMI_REMOTE_UDP != 0);
#else
            false;
#endif
        bool veniceEnabled = false;
        int32_t veniceTxGpio = 14;
    } cfgData_{};

    ConfigVariable<bool,0> ledsEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::LedsEnabled), "enabled", "hmi/leds",
        ConfigType::Bool, &cfgData_.ledsEnabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<bool,0> waveshareLedEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::WaveshareLedEnabled), "waveshare_enabled", "hmi/leds",
        ConfigType::Bool, &cfgData_.waveshareLedEnabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<bool,0> nextionEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::NextionEnabled), "enabled", "hmi/nextion",
        ConfigType::Bool, &cfgData_.nextionEnabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<IoId,0> nextionMotionIoIdVar_{
        NVS_KEY(NvsKeys::Hmi::NextionMotionIoId), "motion_io_id", "hmi/nextion",
        ConfigType::UInt16, &cfgData_.nextionMotionIoId, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<bool,0> remoteUdpEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::FlowConnectUdpEnabled), "enabled", "hmi/nextion_udp",
        ConfigType::Bool, &cfgData_.remoteUdpEnabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<bool,0> veniceEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::VeniceEnabled), "enabled", "hmi/venice",
        ConfigType::Bool, &cfgData_.veniceEnabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t,0> veniceTxGpioVar_{
        NVS_KEY(NvsKeys::Hmi::VeniceTxGpio), "tx_gpio", "hmi/venice",
        ConfigType::Int32, &cfgData_.veniceTxGpio, ConfigPersistence::Persistent, 0
    };
    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const DomainStatusService* domainStatusSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const TimeService* timeSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const LocaleService* localeSvc_ = nullptr;
    I2CBus* i2cBus_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    ServiceRegistry* services_ = nullptr;
    EventBus* eventBus_ = nullptr;

    ConfigMenuModel menu_;
    NextionDriver nextion_;
    RemoteHmiUdpDriver remoteUdp_;
    HmiUdpServerModule* remoteUdpServer_ = nullptr;
    TfaVeniceRf433Sink venice_;
    Pcf8574LedPanelDriver frontLedPanel_;
    Ws2812StatusLedDriver ws2812StatusLed_;
    IHmiDriver* driver_ = nullptr;

    bool driverReady_ = false;
    bool nextionDisabledByVersion_ = false;
    bool homePageVisible_ = false;
    bool menuSessionActive_ = false;
    bool menuPageVisible_ = false;
    bool alarmPageActive_ = false;
    bool viewDirty_ = true;
    bool configMenuReady_ = false;
    bool configMenuActive_ = false;
    uint32_t lastRenderMs_ = 0;
    uint32_t lastConfigValueRefreshMs_ = 0;
    uint32_t lastHmiLedDebugMs_ = 0;
    uint8_t ledPage_ = 1;
    uint8_t ledMaskLast_ = 0;
    bool ledMaskValid_ = false;
    bool ws2812AutoWifiMode_ = true;
    bool ws2812AutoWifiApplied_ = false;
    bool ws2812AutoWifiConnectedLast_ = false;
    bool ws2812AutoWifiMqttLast_ = false;
    bool ws2812AutoWifiApModeLast_ = false;
    bool ws2812AutoWifiNormalLast_ = false;
    bool ws2812AutoWifiAlarmActiveLast_ = false;
    bool ws2812AutoWifiAlarmRedPhaseLast_ = false;
    int8_t frontLedI2cSda_ = -1;
    int8_t frontLedI2cScl_ = -1;
    uint32_t frontLedI2cFrequencyHz_ = 100000U;
    uint32_t homePublishMask_ = 0U;
    portMUX_TYPE homePublishMux_ = portMUX_INITIALIZER_UNLOCKED;
    IoId phIoId_ = ioIdFromSlot(analogInputSlot(1));
    IoId orpIoId_ = ioIdFromSlot(analogInputSlot(0));
    IoId psiIoId_ = ioIdFromSlot(analogInputSlot(2));
    IoId airTempIoId_ = ioIdFromSlot(analogInputSlot(5));
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    IoId poolLevelIoId_ = ioIdFromSlot(digitalInputSlot(11));
    IoId phLevelIoId_ = ioIdFromSlot(digitalInputSlot(9));
    IoId chlorineLevelIoId_ = ioIdFromSlot(digitalInputSlot(10));
#else
    IoId poolLevelIoId_ = ioIdFromSlot(digitalInputSlot(0));
    IoId phLevelIoId_ = ioIdFromSlot(digitalInputSlot(1));
    IoId chlorineLevelIoId_ = ioIdFromSlot(digitalInputSlot(2));
#endif
    IoId waterTempIoId_ = ioIdFromSlot(analogInputSlot(4));
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    IoId waterCounterIoId_ = ioIdFromSlot(digitalInputSlot(12));
#else
    IoId waterCounterIoId_ = ioIdFromSlot(digitalInputSlot(3));
#endif
    uint8_t filtrationDeviceSlot_ = PoolIds::DeviceFiltrationPump;
    uint8_t phPumpDeviceSlot_ = PoolIds::DevicePhPump;
    uint8_t orpPumpDeviceSlot_ = PoolIds::DeviceChlorinePump;
    uint8_t robotDeviceSlot_ = PoolIds::DeviceRobot;
    uint8_t lightsDeviceSlot_ = PoolIds::DeviceLights;
    uint8_t heaterDeviceSlot_ = PoolIds::DeviceWaterHeater;
    uint8_t fillingDeviceSlot_ = PoolIds::DeviceFillPump;
    uint8_t phRuntimeIndex_ = 0xFFU;
    uint8_t orpRuntimeIndex_ = 0xFFU;
    uint8_t psiRuntimeIndex_ = 0xFFU;
    uint8_t waterTempRuntimeIndex_ = 0xFFU;
    uint8_t airTempRuntimeIndex_ = 0xFFU;
    uint8_t poolLevelRuntimeIndex_ = 0xFFU;
    uint8_t phLevelRuntimeIndex_ = 0xFFU;
    uint8_t chlorineLevelRuntimeIndex_ = 0xFFU;
    uint8_t waterCounterRuntimeIndex_ = 0xFFU;
    bool wifiNetworkExpected_ = true;
    bool ethernetNetworkExpected_ = false;
    uint32_t lastLedApplyTryMs_ = 0;
    uint32_t lastLedPageToggleMs_ = 0;
    uint32_t lastClockCheckMs_ = 0;
    uint32_t lastHomePeriodicRefreshMs_ = 0;
    uint32_t lastNextionPageProbeMs_ = 0;
    uint32_t lastNextionMotionWakeAttemptMs_ = 0;
    uint32_t lastDisplayVersionProbeMs_ = 0;
    uint32_t lastClockMinuteStamp_ = 0xFFFFFFFFUL;
    uint32_t lastClockDayStamp_ = 0xFFFFFFFFUL;
    uint32_t lastRtcFallbackAttemptMs_ = 0;
    uint32_t lastRtcPushAttemptMs_ = 0;
    uint32_t lastRtcPushDayStamp_ = 0xFFFFFFFFUL;
    bool rtcFallbackCompleted_ = false;
    bool rtcPushPending_ = false;
    bool nextionVersionDetected_ = false;
    bool nextionMotionInputReady_ = false;
    bool nextionMotionActiveLast_ = false;
    bool nextionMotionReadErrorLogged_ = false;
    char nextionVersion_[HMI_DISPLAY_VERSION_TEXT_MAX]{};
    bool homeBindingsRefreshPending_ = false;
    char homeErrorMessage_[96]{};
    uint32_t activeConfigContextToken_ = 0U;
    uint32_t nextConfigContextToken_ = 1U;
    uint8_t alarmPageIndex_ = 0U;
    uint8_t alarmPageCount_ = 1U;
    uint8_t alarmRowCount_ = 0U;
    AlarmId alarmRowIds_[ConfigMenuModel::RowsPerPage]{};
    bool alarmRowResettable_[ConfigMenuModel::RowsPerPage]{};
    char localeLang_[8] = "fr";
    uint32_t localeGenerationSeen_ = 0U;

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void handleDriverEvent_(const HmiEvent& e);
    bool requestRefresh_();
    bool openConfigHome_();
    bool openConfigModule_(const char* module);
    bool setLedPage_(uint8_t page);
    uint8_t getLedPage_() const;
    bool setStatusLedState_(const HmiStatusLedState* state);
    bool getStatusLedState_(HmiStatusLedState* out) const;
    bool setStatusLedAutoWifiMode_(bool enabled);
    bool isStatusLedAutoWifiMode_() const;
    bool setLedCondition_(HmiLedCondition condition, bool active);
    void clearLedConditions_();
    void setBootComplete_();
    bool setLedEnabled_(bool enabled);
    bool setLedBrightness_(uint8_t brightness);
    bool ensureFrontLedPanelReady_();
    void applyFrontLedPanelConfig_();
    bool getDisplayVersion_(char* out, size_t outLen) const;
    bool readRtcSvc_(HmiRtcDateTime* out, uint16_t timeoutMs);
    bool writeRtcSvc_(const HmiRtcDateTime* value);
    bool refreshCurrentModule_();
    bool render_();
    bool renderAlarmPage_();
    bool refreshConfigMenuValues_();
    bool refreshAlarmPageValues_();
    bool buildMenuJson_(char* out, size_t outLen);
    bool ensureConfigMenuReady_();
    uint32_t cacheCurrentConfigContext_();
    bool restoreConfigContext_(uint32_t token);
    void refreshHomeBindings_();
    bool resolveIoRuntimeIndex_(IoId ioId, uint8_t& outIndex) const;
    bool readPoolLogicModeFlags_(bool& autoMode, bool& winterMode, bool& phAutoMode, bool& orpAutoMode) const;
    bool readPidSetpoints_(float& phSetpoint, float& orpSetpoint) const;
    bool readPoolDeviceActualOn_(uint8_t slot, bool& on) const;
    bool isAlarmActive_(AlarmId id) const;
    bool isWaterLevelLow_() const;
    uint32_t buildHomeStateBits_() const;
    uint32_t buildHomeAlarmBits_() const;
    bool publishHomeText_(HmiHomeTextField field);
    bool publishHomeGaugePercent_(HmiHomeGaugeField field);
    bool publishHomeStateBits_();
    bool publishHomeAlarmBits_();
    bool validateDriverDisplayVersion_(bool requireDetection);
    void serviceRtcBridge_(uint32_t nowMs);
    bool readNextionRtcAndSetTime_();
    bool pushEspTimeToNextionRtc_();
    void resetClockPublishStamps_();
    void queueClockPublishIfDue_(uint32_t nowMs);
    void queueHomePublish_(uint32_t mask);
    void flushHomePublish_();
    bool executeHmiCommand_(HmiCommandId command, uint8_t value);
    bool executeCommandBool_(const char* cmdName, bool value);
    bool executePoolDeviceWrite_(uint8_t slot, bool value);
    bool executePoolLogicModePatch_(const char* key, bool value);
    void setHomeErrorMessage_(const char* message, bool forceStateRefresh);
    void reportCommandError_(const char* operation, const char* reply);
    void refreshLocale_();
    void markAlarmViewDirty_();
    uint8_t collectAlarmIds_(AlarmId* out, uint8_t max) const;
    bool readAlarmState_(AlarmId id, bool& active, bool& resettable, AlarmCondState& condition) const;
    const char* alarmLabelForId_(AlarmId id) const;
    const char* alarmLabelShortForId_(AlarmId id) const;
    bool buildAlarmPageView_(ConfigMenuView& out, bool valueOnly);
    bool resetAlarmRow_(uint8_t rowIndex);
    bool nextAlarmPage_();
    bool prevAlarmPage_();
    bool isAlarmPageId_(uint8_t pageId) const;
    bool isDisplaySleeping_() const;
    void updateNextionMotionWake_(uint32_t nowMs);
    void refreshNetworkExpectations_();
    void applyWs2812AutoWifiProfile_();
    void updateHmiLedConditions_();
    bool hasDomainSlotError_() const;
    bool firstDomainSlotError_(DomainSlotStatus& outStatus) const;
    void logHmiLedDebug_(uint32_t nowMs);
    static const char* hmiLedDisplayStateName_(HmiLedDisplayState state);
    void applyOutputConfig_();
    void applyLedMask_(bool force = false);

    HmiService hmiSvc_{
        ServiceBinding::bind<&HMIModule::requestRefresh_>,
        ServiceBinding::bind<&HMIModule::openConfigHome_>,
        ServiceBinding::bind<&HMIModule::openConfigModule_>,
        ServiceBinding::bind<&HMIModule::buildMenuJson_>,
        ServiceBinding::bind<&HMIModule::setLedPage_>,
        ServiceBinding::bind_or<&HMIModule::getLedPage_, (uint8_t)1U>,
        ServiceBinding::bind<&HMIModule::setStatusLedState_>,
        ServiceBinding::bind<&HMIModule::getStatusLedState_>,
        ServiceBinding::bind<&HMIModule::setStatusLedAutoWifiMode_>,
        ServiceBinding::bind<&HMIModule::isStatusLedAutoWifiMode_>,
        ServiceBinding::bind<&HMIModule::setLedCondition_>,
        ServiceBinding::bind<&HMIModule::clearLedConditions_>,
        ServiceBinding::bind<&HMIModule::setBootComplete_>,
        ServiceBinding::bind<&HMIModule::setLedEnabled_>,
        ServiceBinding::bind<&HMIModule::setLedBrightness_>,
        ServiceBinding::bind<&HMIModule::getDisplayVersion_>,
        ServiceBinding::bind<&HMIModule::readRtcSvc_>,
        ServiceBinding::bind<&HMIModule::writeRtcSvc_>,
        ServiceBinding::bind<&HMIModule::isDisplaySleeping_>,
        this
    };
};
