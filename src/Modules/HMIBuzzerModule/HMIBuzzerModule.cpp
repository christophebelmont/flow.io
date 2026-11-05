#include "Modules/HMIBuzzerModule/HMIBuzzerModule.h"

#include <Arduino.h>

#include "Core/AlarmIds.h"
#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"

#ifndef BUZZER_ENABLE_ENVELOPE
#define BUZZER_ENABLE_ENVELOPE 1
#endif

namespace {
constexpr uint8_t kLedcResolutionBits = 8U;
constexpr uint32_t kLedcInitialFrequencyHz = 2400U;
constexpr uint8_t kDefaultDutyPercent = 32U;
constexpr uint32_t kAlarmRepeatMs = 8000U;

constexpr HmiBuzzer::SequenceStep kConfigSuccessSequence[] = {
    {2400U, 40U, kDefaultDutyPercent, 5U, 10U, true},
    {0U, 30U, 0U, 0U, 0U, false},
    {3200U, 60U, kDefaultDutyPercent, 5U, 10U, true},
};

constexpr HmiBuzzer::SequenceStep kDeviceOnSequence[] = {
    {2800U, 35U, 30U, 5U, 10U, true},
};

constexpr HmiBuzzer::SequenceStep kDeviceOffSequence[] = {
    {2200U, 35U, 30U, 5U, 10U, true},
};

constexpr HmiBuzzer::SequenceStep kAlarmActiveSequence[] = {
    {1800U, 100U, 34U, 5U, 10U, true},
    {0U, 80U, 0U, 0U, 0U, false},
    {1800U, 100U, 34U, 5U, 10U, true},
};

constexpr HmiBuzzer::SequenceStep kAlarmCriticalSequence[] = {
    {1800U, 120U, 36U, 5U, 10U, true},
    {0U, 80U, 0U, 0U, 0U, false},
    {1800U, 120U, 36U, 5U, 10U, true},
    {0U, 80U, 0U, 0U, 0U, false},
    {1800U, 120U, 36U, 5U, 10U, true},
};

template <typename T, size_t N>
constexpr uint8_t arrayLen_(const T (&)[N])
{
    return (uint8_t)N;
}
}  // namespace

bool HmiBuzzer::begin(uint8_t pin, bool activeHigh)
{
    pin_ = pin;
    activeHigh_ = activeHigh;
    resolutionBits_ = kLedcResolutionBits;
    maxDuty_ = (1UL << resolutionBits_) - 1UL;

    attached_ = ledcAttach(pin_, kLedcInitialFrequencyHz, resolutionBits_);
    if (!attached_) {
        LOGE("HMI buzzer LEDC attach failed gpio=%u freq=%lu res=%u",
             (unsigned)pin_,
             (unsigned long)kLedcInitialFrequencyHz,
             (unsigned)resolutionBits_);
        return false;
    }

    if (!activeHigh_) {
        if (!ledcOutputInvert(pin_, true)) {
            LOGW("HMI buzzer LEDC output invert failed gpio=%u", (unsigned)pin_);
        }
    }

    stop();
    LOGI("HMI buzzer LEDC ready gpio=%u freq=%lu res=%u duty=%u%% envelope=%u",
         (unsigned)pin_,
         (unsigned long)kLedcInitialFrequencyHz,
         (unsigned)resolutionBits_,
         (unsigned)kDefaultDutyPercent,
         (unsigned)(BUZZER_ENABLE_ENVELOPE != 0));
    return true;
}

void HmiBuzzer::play(BuzzerPattern pattern)
{
    if (!attached_) return;
    if (sequence_ && priority_(pattern) < priority_(currentPattern_)) return;

    uint8_t len = 0U;
    const SequenceStep* seq = sequenceFor_(pattern, len);
    if (!seq || len == 0U) return;

    currentPattern_ = pattern;
    sequence_ = seq;
    sequenceLen_ = len;
    stepIndex_ = 0U;
    stepStartedMs_ = 0U;
    stepRunning_ = false;
    outputOn_ = false;
}

void HmiBuzzer::tick(uint32_t nowMs)
{
    if (!attached_ || !sequence_) return;
    if (stepIndex_ >= sequenceLen_) {
        stop();
        return;
    }

    const SequenceStep& step = sequence_[stepIndex_];
    if (!stepRunning_) {
        startStep_(step, nowMs);
        return;
    }

    const uint32_t elapsedMs = nowMs - stepStartedMs_;
    if (elapsedMs >= step.durationMs) {
        advanceStep_(nowMs);
        return;
    }

    if (step.note) {
        (void)ledcWrite(pin_, envelopeDuty_(step, elapsedMs));
    }
}

void HmiBuzzer::stop()
{
    if (attached_) {
        (void)ledcWrite(pin_, 0U);
    }
    sequence_ = nullptr;
    sequenceLen_ = 0U;
    stepIndex_ = 0U;
    stepStartedMs_ = 0U;
    stepRunning_ = false;
    outputOn_ = false;
}

uint8_t HmiBuzzer::priority_(BuzzerPattern pattern)
{
    switch (pattern) {
        case BuzzerPattern::AlarmCritical: return 5U;
        case BuzzerPattern::AlarmActive: return 4U;
        case BuzzerPattern::ConfigSuccess: return 3U;
        case BuzzerPattern::DeviceOn:
        case BuzzerPattern::DeviceOff:
            return 2U;
        default:
            return 0U;
    }
}

const HmiBuzzer::SequenceStep* HmiBuzzer::sequenceFor_(BuzzerPattern pattern, uint8_t& len)
{
    switch (pattern) {
        case BuzzerPattern::ConfigSuccess:
            len = arrayLen_(kConfigSuccessSequence);
            return kConfigSuccessSequence;
        case BuzzerPattern::DeviceOn:
            len = arrayLen_(kDeviceOnSequence);
            return kDeviceOnSequence;
        case BuzzerPattern::DeviceOff:
            len = arrayLen_(kDeviceOffSequence);
            return kDeviceOffSequence;
        case BuzzerPattern::AlarmActive:
            len = arrayLen_(kAlarmActiveSequence);
            return kAlarmActiveSequence;
        case BuzzerPattern::AlarmCritical:
            len = arrayLen_(kAlarmCriticalSequence);
            return kAlarmCriticalSequence;
        default:
            len = 0U;
            return nullptr;
    }
}

uint32_t HmiBuzzer::dutyFromPercent_(uint8_t percent) const
{
    if (percent == 0U) return 0U;
    if (percent >= 100U) return maxDuty_;
    return (maxDuty_ * (uint32_t)percent) / 100UL;
}

uint32_t HmiBuzzer::envelopeDuty_(const SequenceStep& step, uint32_t elapsedMs) const
{
    const uint32_t targetDuty = dutyFromPercent_(step.dutyPercent);
#if BUZZER_ENABLE_ENVELOPE
    // This is PWM duty-cycle shaping, not true analog amplitude control. On many
    // passive buzzer driver stages it softens attack/release enough to avoid clicks.
    if (step.fadeInMs > 0U && elapsedMs < step.fadeInMs) {
        return (targetDuty * elapsedMs) / step.fadeInMs;
    }

    const uint32_t remainingMs = (elapsedMs < step.durationMs) ? (step.durationMs - elapsedMs) : 0U;
    if (step.fadeOutMs > 0U && remainingMs < step.fadeOutMs) {
        return (targetDuty * remainingMs) / step.fadeOutMs;
    }
#else
    (void)elapsedMs;
#endif
    return targetDuty;
}

bool HmiBuzzer::applyFrequency_(uint16_t frequencyHz)
{
    const uint32_t changed = ledcChangeFrequency(pin_, frequencyHz, resolutionBits_);
    if (changed != 0U) return true;

    const uint32_t toneHz = ledcWriteTone(pin_, frequencyHz);
    if (toneHz == 0U) {
        LOGW("HMI buzzer LEDC frequency failed gpio=%u freq=%u", (unsigned)pin_, (unsigned)frequencyHz);
        return false;
    }
    if (!ledcToneFallbackWarned_) {
        ledcToneFallbackWarned_ = true;
        LOGW("HMI buzzer using ledcWriteTone fallback before duty adjustment");
    }
    return true;
}

void HmiBuzzer::startStep_(const SequenceStep& step, uint32_t nowMs)
{
    stepStartedMs_ = nowMs;
    stepRunning_ = true;
    outputOn_ = false;

    if (!step.note || step.frequencyHz == 0U || step.durationMs == 0U) {
        (void)ledcWrite(pin_, 0U);
        return;
    }

    if (!applyFrequency_(step.frequencyHz)) {
        (void)ledcWrite(pin_, 0U);
        return;
    }
    (void)ledcWrite(pin_, envelopeDuty_(step, 0U));
    outputOn_ = true;
}

void HmiBuzzer::advanceStep_(uint32_t nowMs)
{
    (void)ledcWrite(pin_, 0U);
    outputOn_ = false;
    ++stepIndex_;
    stepRunning_ = false;
    stepStartedMs_ = 0U;

    if (stepIndex_ >= sequenceLen_) {
        stop();
        return;
    }
    startStep_(sequence_[stepIndex_], nowMs);
}

HMIBuzzerModule::HMIBuzzerModule(const BoardSpec& board)
{
    const HmiBuzzerSpec* spec = boardHmiBuzzer(board);
    if (!spec) return;
    hardwareAvailable_ = spec->enabled && spec->pin >= 0;
    pin_ = spec->pin;
    activeHigh_ = spec->activeHigh;
}

void HMIBuzzerModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfg.registerVar(enableVar_);

    dsSvc_ = services.get<DataStoreService>(ServiceId::DataStore);
    alarmSvc_ = services.get<AlarmService>(ServiceId::Alarm);
    auto* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    if (eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &HMIBuzzerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::DataChanged, &HMIBuzzerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmRaised, &HMIBuzzerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmCleared, &HMIBuzzerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmConditionChanged, &HMIBuzzerModule::onEventStatic_, this);
    }
}

void HMIBuzzerModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    configureHardware_();
}

void HMIBuzzerModule::loop()
{
    if (!hardwareAvailable_ || !cfgData_.enable) {
        pendingPatterns_.store(0U, std::memory_order_relaxed);
        buzzer_.stop();
        return;
    }

    const uint32_t nowMs = millis();
    tickAlarmReminder_(nowMs);
    playPending_(nowMs);
    buzzer_.tick(nowMs);
}

void HMIBuzzerModule::onEventStatic_(const Event& e, void* user)
{
    auto* self = static_cast<HMIBuzzerModule*>(user);
    if (self) self->onEvent_(e);
}

void HMIBuzzerModule::onEvent_(const Event& e)
{
    if (!cfgData_.enable) return;

    if (e.id == EventId::ConfigChanged) {
        if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;
        requestPattern_(BuzzerPattern::ConfigSuccess);
        return;
    }

    if (e.id == EventId::DataChanged) {
        if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
        handlePoolDeviceStateChanged_(*static_cast<const DataChangedPayload*>(e.payload));
        return;
    }

    if (e.id == EventId::AlarmRaised) {
        handleAlarmRaised_();
        return;
    }

    if (e.id == EventId::AlarmCleared || e.id == EventId::AlarmConditionChanged) {
        lastAlarmPatternMs_ = 0U;
    }
}

void HMIBuzzerModule::configureHardware_()
{
    if (!hardwareAvailable_) {
        LOGI("HMI buzzer unavailable");
        return;
    }

    pinMode((uint8_t)pin_, OUTPUT);
    hardwareAvailable_ = buzzer_.begin((uint8_t)pin_, activeHigh_);
    if (!hardwareAvailable_) return;
}

void HMIBuzzerModule::requestPattern_(BuzzerPattern pattern)
{
    const uint32_t bit = 1UL << (uint8_t)pattern;
    pendingPatterns_.fetch_or(bit, std::memory_order_relaxed);
}

void HMIBuzzerModule::playPending_(uint32_t nowMs)
{
    (void)nowMs;
    const uint32_t mask = pendingPatterns_.exchange(0U, std::memory_order_relaxed);
    if (mask == 0U) return;

    if (mask & (1UL << (uint8_t)BuzzerPattern::AlarmCritical)) {
        buzzer_.play(BuzzerPattern::AlarmCritical);
    } else if (mask & (1UL << (uint8_t)BuzzerPattern::AlarmActive)) {
        buzzer_.play(BuzzerPattern::AlarmActive);
    } else if (mask & (1UL << (uint8_t)BuzzerPattern::ConfigSuccess)) {
        buzzer_.play(BuzzerPattern::ConfigSuccess);
    } else if (mask & (1UL << (uint8_t)BuzzerPattern::DeviceOn)) {
        buzzer_.play(BuzzerPattern::DeviceOn);
    } else if (mask & (1UL << (uint8_t)BuzzerPattern::DeviceOff)) {
        buzzer_.play(BuzzerPattern::DeviceOff);
    }
}

void HMIBuzzerModule::handlePoolDeviceStateChanged_(const DataChangedPayload& payload)
{
    if (!dsSvc_ || !dsSvc_->store) return;
    if (payload.id < DataKeys::PoolDeviceStateBase ||
        payload.id >= DataKeys::PoolDeviceStateEndExclusive) {
        return;
    }

    const uint8_t slot = (uint8_t)(payload.id - DataKeys::PoolDeviceStateBase);
    if (slot >= POOL_DEVICE_MAX) return;

    PoolDeviceRuntimeStateEntry state{};
    if (!poolDeviceRuntimeState(*dsSvc_->store, slot, state)) return;

    if (!poolDeviceActualValid_[slot]) {
        poolDeviceActualValid_[slot] = true;
        poolDeviceActualOn_[slot] = state.actualOn;
        return;
    }

    if (poolDeviceActualOn_[slot] == state.actualOn) return;
    poolDeviceActualOn_[slot] = state.actualOn;
    requestPattern_(state.actualOn ? BuzzerPattern::DeviceOn : BuzzerPattern::DeviceOff);
}

void HMIBuzzerModule::handleAlarmRaised_()
{
    const AlarmSeverity highest = (alarmSvc_ && alarmSvc_->highestSeverity)
        ? alarmSvc_->highestSeverity(alarmSvc_->ctx)
        : AlarmSeverity::Alarm;
    requestPattern_((highest == AlarmSeverity::Critical) ? BuzzerPattern::AlarmCritical : BuzzerPattern::AlarmActive);
    lastAlarmPatternMs_ = millis();
}

void HMIBuzzerModule::tickAlarmReminder_(uint32_t nowMs)
{
    if (!alarmSvc_ || !alarmSvc_->activeCount || !alarmSvc_->highestSeverity) return;
    if (alarmSvc_->activeCount(alarmSvc_->ctx) == 0U) {
        lastAlarmPatternMs_ = 0U;
        return;
    }

    if (lastAlarmPatternMs_ != 0U && (uint32_t)(nowMs - lastAlarmPatternMs_) < kAlarmRepeatMs) return;

    const AlarmSeverity highest = alarmSvc_->highestSeverity(alarmSvc_->ctx);
    requestPattern_((highest == AlarmSeverity::Critical) ? BuzzerPattern::AlarmCritical : BuzzerPattern::AlarmActive);
    lastAlarmPatternMs_ = nowMs;
}
