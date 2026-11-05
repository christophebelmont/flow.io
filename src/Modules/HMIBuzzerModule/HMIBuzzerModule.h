#pragma once

#include "Board/BoardSpec.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Modules/PoolDeviceModule/PoolDeviceModuleDataModel.h"

#include <atomic>
#include <stddef.h>

enum class BuzzerPattern : uint8_t {
    ConfigSuccess = 0,
    DeviceOn,
    DeviceOff,
    AlarmActive,
    AlarmCritical,
};

class HmiBuzzer {
public:
    struct SequenceStep {
        uint16_t frequencyHz;
        uint16_t durationMs;
        uint8_t dutyPercent;
        uint8_t fadeInMs;
        uint8_t fadeOutMs;
        bool note;
    };

    bool begin(uint8_t pin, bool activeHigh);
    void play(BuzzerPattern pattern);
    void tick(uint32_t nowMs);
    void stop();
    bool ready() const { return attached_; }

private:
    uint8_t pin_ = 0xFFU;
    uint8_t resolutionBits_ = 8U;
    uint32_t maxDuty_ = 255U;
    bool attached_ = false;
    bool activeHigh_ = true;
    bool outputOn_ = false;
    bool ledcToneFallbackWarned_ = false;
    BuzzerPattern currentPattern_ = BuzzerPattern::ConfigSuccess;
    const SequenceStep* sequence_ = nullptr;
    uint8_t sequenceLen_ = 0;
    uint8_t stepIndex_ = 0;
    uint32_t stepStartedMs_ = 0;
    bool stepRunning_ = false;

    static uint8_t priority_(BuzzerPattern pattern);
    static const SequenceStep* sequenceFor_(BuzzerPattern pattern, uint8_t& len);
    uint32_t dutyFromPercent_(uint8_t percent) const;
    uint32_t envelopeDuty_(const SequenceStep& step, uint32_t elapsedMs) const;
    bool applyFrequency_(uint16_t frequencyHz);
    void startStep_(const SequenceStep& step, uint32_t nowMs);
    void advanceStep_(uint32_t nowMs);
};

class HMIBuzzerModule : public Module {
public:
    HMIBuzzerModule() = default;
    explicit HMIBuzzerModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::HmiBuzzer; }
    const char* taskName() const override { return "hmi.buzzer"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 2048; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 5; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::EventBus;
        if (i == 3) return ModuleId::DataStore;
        if (i == 4) return ModuleId::Alarm;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    struct ConfigData {
        bool enable = true;
    } cfgData_{};

    ConfigVariable<bool,0> enableVar_{
        NVS_KEY(NvsKeys::Hmi::BuzzerEnable), "enable", "hmi/buzzer",
        ConfigType::Bool, &cfgData_.enable, ConfigPersistence::Persistent, 0
    };

    int8_t pin_ = -1;
    bool activeHigh_ = true;
    bool hardwareAvailable_ = false;
    HmiBuzzer buzzer_{};
    std::atomic<uint32_t> pendingPatterns_{0U};
    EventBus* eventBus_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    bool poolDeviceActualValid_[POOL_DEVICE_MAX]{};
    bool poolDeviceActualOn_[POOL_DEVICE_MAX]{};
    uint32_t lastAlarmPatternMs_ = 0U;

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void configureHardware_();
    void requestPattern_(BuzzerPattern pattern);
    void playPending_(uint32_t nowMs);
    void handlePoolDeviceStateChanged_(const DataChangedPayload& payload);
    void handleAlarmRaised_();
    void tickAlarmReminder_(uint32_t nowMs);
};
