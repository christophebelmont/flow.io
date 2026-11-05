/**
 * @file PcntCounterDriver.cpp
 * @brief Implementation file.
 */

#include "PcntCounterDriver.h"

#include <esp_err.h>
#include <soc/soc_caps.h>
#if FLOW_PCNT_USE_PULSE_CNT
#include <esp_private/esp_clk.h>
#endif

namespace {
portMUX_TYPE gPcntCounterMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t gPcntUnitsMask = 0;

#if FLOW_PCNT_USE_PULSE_CNT
constexpr uint32_t kPcntMaxGlitchWidthCycles = 1023U;
constexpr uint32_t kFallbackApbMhz = 80U;

uint32_t pcntMaxGlitchFilterNs_()
{
    uint32_t apbMhz = static_cast<uint32_t>(esp_clk_apb_freq() / 1000000);
    if (apbMhz == 0U) apbMhz = kFallbackApbMhz;
    return (kPcntMaxGlitchWidthCycles * 1000U) / apbMhz;
}

uint32_t pcntHardwareFilterNs_(uint32_t debounceUs)
{
    if (debounceUs == 0U) return 0U;
    const uint64_t requestedNs = static_cast<uint64_t>(debounceUs) * 1000ULL;
    const uint32_t maxNs = pcntMaxGlitchFilterNs_();
    if (requestedNs > static_cast<uint64_t>(maxNs)) return maxNs;
    return static_cast<uint32_t>(requestedNs);
}
#endif
}

PcntCounterDriver::PcntCounterDriver(const char* driverId,
                                     uint8_t pin,
                                     bool activeHigh,
                                     uint8_t inputPullMode,
                                     uint8_t edgeMode,
                                     uint32_t counterDebounceUs)
    : driverId_(driverId),
      pin_(pin),
      activeHigh_(activeHigh),
      inputPullMode_(inputPullMode),
      edgeMode_(edgeMode),
      counterDebounceUs_(counterDebounceUs)
{
}

#if FLOW_PCNT_USE_PULSE_CNT
void PcntCounterDriver::configureEdgeModes_(pcnt_channel_edge_action_t& posMode, pcnt_channel_edge_action_t& negMode) const
{
    posMode = PCNT_CHANNEL_EDGE_ACTION_HOLD;
    negMode = PCNT_CHANNEL_EDGE_ACTION_HOLD;

    if (edgeMode_ == 2U) {
        posMode = PCNT_CHANNEL_EDGE_ACTION_INCREASE;
        negMode = PCNT_CHANNEL_EDGE_ACTION_INCREASE;
        return;
    }

    const bool logicalRising = (edgeMode_ == 1U);
    if (activeHigh_) {
        posMode = logicalRising ? PCNT_CHANNEL_EDGE_ACTION_INCREASE : PCNT_CHANNEL_EDGE_ACTION_HOLD;
        negMode = logicalRising ? PCNT_CHANNEL_EDGE_ACTION_HOLD : PCNT_CHANNEL_EDGE_ACTION_INCREASE;
    } else {
        posMode = logicalRising ? PCNT_CHANNEL_EDGE_ACTION_HOLD : PCNT_CHANNEL_EDGE_ACTION_INCREASE;
        negMode = logicalRising ? PCNT_CHANNEL_EDGE_ACTION_INCREASE : PCNT_CHANNEL_EDGE_ACTION_HOLD;
    }
}

void PcntCounterDriver::releaseCounter_()
{
    if (unit_) {
        (void)pcnt_unit_stop(unit_);
        (void)pcnt_unit_disable(unit_);
    }
    if (channel_) {
        (void)pcnt_del_channel(channel_);
        channel_ = nullptr;
    }
    if (unit_) {
        (void)pcnt_del_unit(unit_);
        unit_ = nullptr;
    }
}
#else
pcnt_unit_t PcntCounterDriver::allocUnit_()
{
    portENTER_CRITICAL(&gPcntCounterMux);
    for (int unit = 0; unit < (int)PCNT_UNIT_MAX; ++unit) {
        const uint32_t bit = (1UL << unit);
        if ((gPcntUnitsMask & bit) != 0U) continue;
        gPcntUnitsMask |= bit;
        portEXIT_CRITICAL(&gPcntCounterMux);
        return static_cast<pcnt_unit_t>(unit);
    }
    portEXIT_CRITICAL(&gPcntCounterMux);
    return PCNT_UNIT_MAX;
}

void PcntCounterDriver::releaseUnit_(pcnt_unit_t unit)
{
    if (unit >= PCNT_UNIT_MAX) return;
    portENTER_CRITICAL(&gPcntCounterMux);
    gPcntUnitsMask &= ~(1UL << static_cast<unsigned>(unit));
    portEXIT_CRITICAL(&gPcntCounterMux);
}

void PcntCounterDriver::configureEdgeModes_(pcnt_count_mode_t& posMode, pcnt_count_mode_t& negMode) const
{
    posMode = PCNT_COUNT_DIS;
    negMode = PCNT_COUNT_DIS;

    if (edgeMode_ == 2U) {
        posMode = PCNT_COUNT_INC;
        negMode = PCNT_COUNT_INC;
        return;
    }

    const bool logicalRising = (edgeMode_ == 1U);
    if (activeHigh_) {
        posMode = logicalRising ? PCNT_COUNT_INC : PCNT_COUNT_DIS;
        negMode = logicalRising ? PCNT_COUNT_DIS : PCNT_COUNT_INC;
    } else {
        posMode = logicalRising ? PCNT_COUNT_DIS : PCNT_COUNT_INC;
        negMode = logicalRising ? PCNT_COUNT_INC : PCNT_COUNT_DIS;
    }
}
#endif

uint32_t PcntCounterDriver::debounceWindowMs_() const
{
    if (counterDebounceUs_ == 0U) return 0U;
    return (counterDebounceUs_ + 999U) / 1000U;
}

bool PcntCounterDriver::begin()
{
#if FLOW_PCNT_USE_PULSE_CNT
    if (unit_) releaseCounter_();
#else
    unit_ = allocUnit_();
    if (unit_ == PCNT_UNIT_MAX) return false;
#endif

    if (inputPullMode_ == 1U) pinMode(pin_, INPUT_PULLUP);
    else if (inputPullMode_ == 2U) pinMode(pin_, INPUT_PULLDOWN);
    else pinMode(pin_, INPUT);

#if FLOW_PCNT_USE_PULSE_CNT
    pcnt_unit_config_t unitCfg{};
    unitCfg.low_limit = kCounterLowLimit;
    unitCfg.high_limit = kCounterHighLimit;
    if (pcnt_new_unit(&unitCfg, &unit_) != ESP_OK) {
        unit_ = nullptr;
        return false;
    }

    if (counterDebounceUs_ > 0U) {
        pcnt_glitch_filter_config_t filterCfg{};
        filterCfg.max_glitch_ns = pcntHardwareFilterNs_(counterDebounceUs_);
        if (pcnt_unit_set_glitch_filter(unit_, &filterCfg) != ESP_OK) {
            releaseCounter_();
            return false;
        }
    }

    pcnt_chan_config_t chanCfg{};
    chanCfg.edge_gpio_num = pin_;
    chanCfg.level_gpio_num = -1;
    if (pcnt_new_channel(unit_, &chanCfg, &channel_) != ESP_OK) {
        releaseCounter_();
        return false;
    }

    pcnt_channel_edge_action_t posMode = PCNT_CHANNEL_EDGE_ACTION_HOLD;
    pcnt_channel_edge_action_t negMode = PCNT_CHANNEL_EDGE_ACTION_HOLD;
    configureEdgeModes_(posMode, negMode);
    if (pcnt_channel_set_edge_action(channel_, posMode, negMode) != ESP_OK) {
        releaseCounter_();
        return false;
    }

    if (pcnt_unit_enable(unit_) != ESP_OK ||
        pcnt_unit_clear_count(unit_) != ESP_OK ||
        pcnt_unit_start(unit_) != ESP_OK) {
        releaseCounter_();
        return false;
    }
#else
    pcnt_count_mode_t posMode = PCNT_COUNT_DIS;
    pcnt_count_mode_t negMode = PCNT_COUNT_DIS;
    configureEdgeModes_(posMode, negMode);

    pcnt_config_t cfg{};
    cfg.pulse_gpio_num = pin_;
    cfg.ctrl_gpio_num = PCNT_PIN_NOT_USED;
    cfg.lctrl_mode = PCNT_MODE_KEEP;
    cfg.hctrl_mode = PCNT_MODE_KEEP;
    cfg.pos_mode = posMode;
    cfg.neg_mode = negMode;
    cfg.counter_h_lim = kCounterHighLimit;
    cfg.counter_l_lim = kCounterLowLimit;
    cfg.unit = unit_;
    cfg.channel = PCNT_CHANNEL_0;

    if (pcnt_unit_config(&cfg) != ESP_OK) {
        releaseUnit_(unit_);
        unit_ = PCNT_UNIT_MAX;
        return false;
    }

    if (counterDebounceUs_ > 0U) {
        const uint64_t cycles64 = static_cast<uint64_t>(counterDebounceUs_) * 80ULL;
        const uint16_t cycles = static_cast<uint16_t>((cycles64 > 1023ULL) ? 1023ULL : cycles64);
        if (cycles > 0U) {
            (void)pcnt_set_filter_value(unit_, cycles);
            (void)pcnt_filter_enable(unit_);
        }
    } else {
        (void)pcnt_filter_disable(unit_);
    }

    (void)pcnt_counter_pause(unit_);
    (void)pcnt_counter_clear(unit_);
    (void)pcnt_counter_resume(unit_);
#endif

    portENTER_CRITICAL(&gPcntCounterMux);
    state_ = RuntimeState{};
    state_.started = true;
    portEXIT_CRITICAL(&gPcntCounterMux);
    return true;
}

void PcntCounterDriver::tick(uint32_t nowMs)
{
    (void)syncCounter_(nowMs);
}

bool PcntCounterDriver::read(bool& on) const
{
    const int level = digitalRead(pin_);
    on = activeHigh_ ? (level == HIGH) : (level == LOW);
    return true;
}

bool PcntCounterDriver::syncCounter_(uint32_t nowMs) const
{
#if FLOW_PCNT_USE_PULSE_CNT
    if (!unit_ || !state_.started) return false;

    int hwCount = 0;
    if (pcnt_unit_get_count(unit_, &hwCount) != ESP_OK) return false;
#else
    if (unit_ == PCNT_UNIT_MAX || !state_.started) return false;

    int16_t hwCount = 0;
    if (pcnt_get_counter_value(unit_, &hwCount) != ESP_OK) return false;
#endif
    bool logicalOn = false;
    (void)read(logicalOn);

    const bool needFold = (hwCount >= kFoldThreshold) || (hwCount <= -kFoldThreshold);
    if (needFold) {
#if FLOW_PCNT_USE_PULSE_CNT
        (void)pcnt_unit_stop(unit_);
        if (pcnt_unit_get_count(unit_, &hwCount) != ESP_OK) {
            (void)pcnt_unit_start(unit_);
            return false;
        }
#else
        (void)pcnt_counter_pause(unit_);
        if (pcnt_get_counter_value(unit_, &hwCount) != ESP_OK) {
            (void)pcnt_counter_resume(unit_);
            return false;
        }
#endif
    }

    portENTER_CRITICAL(&gPcntCounterMux);
    RuntimeState& s = state_;
    const int32_t delta = static_cast<int32_t>(hwCount) - static_cast<int32_t>(s.lastHardwareCount);
    s.sampleCount++;
    s.lastSampleMs = nowMs;
    const uint32_t debounceMs = debounceWindowMs_();
    if (!logicalOn) {
        if (s.idleSinceMs == 0U) s.idleSinceMs = nowMs;
        if (!s.gateArmed && (debounceMs == 0U || (uint32_t)(nowMs - s.idleSinceMs) >= debounceMs)) {
            s.gateArmed = true;
        }
    } else {
        s.idleSinceMs = 0U;
    }
    if (delta > 0) {
        s.rawPulseCount += delta;
        if (debounceMs == 0U) {
            s.pulseCount += delta;
            s.lastAcceptedMs = nowMs;
        } else {
            if (s.gateArmed) {
                ++s.pulseCount;
                s.lastAcceptedMs = nowMs;
                s.gateArmed = false;
                s.idleSinceMs = 0U;
                if (delta > 1) {
                    s.ignoredDebounceCount += static_cast<uint32_t>(delta - 1);
                }
            } else {
                s.ignoredDebounceCount += static_cast<uint32_t>(delta);
            }
        }
    }
    s.lastHardwareCount = hwCount;
    portEXIT_CRITICAL(&gPcntCounterMux);

    if (needFold) {
#if FLOW_PCNT_USE_PULSE_CNT
        (void)pcnt_unit_clear_count(unit_);
#else
        (void)pcnt_counter_clear(unit_);
#endif
        portENTER_CRITICAL(&gPcntCounterMux);
        state_.lastHardwareCount = 0;
        state_.foldCount++;
        portEXIT_CRITICAL(&gPcntCounterMux);
#if FLOW_PCNT_USE_PULSE_CNT
        (void)pcnt_unit_start(unit_);
#else
        (void)pcnt_counter_resume(unit_);
#endif
    }

    return true;
}

bool PcntCounterDriver::readCount(int32_t& count) const
{
    (void)syncCounter_(millis());
    portENTER_CRITICAL(&gPcntCounterMux);
    count = state_.pulseCount;
    portEXIT_CRITICAL(&gPcntCounterMux);
    return true;
}

bool PcntCounterDriver::readDebugStats(IODigitalCounterDebugStats& out) const
{
    bool logicalState = false;
    (void)read(logicalState);
    (void)syncCounter_(millis());

    portENTER_CRITICAL(&gPcntCounterMux);
    out.pin = pin_;
    out.edgeMode = edgeMode_;
    out.activeHigh = activeHigh_;
    out.logicalState = logicalState;
    out.pulseCount = state_.pulseCount;
    out.irqCalls = state_.rawPulseCount;
    out.transitions = state_.sampleCount;
    out.ignoredSameState = 0;
    out.ignoredWrongEdge = 0;
    out.ignoredDebounce = state_.ignoredDebounceCount;
    out.lastPulseUs = state_.lastAcceptedMs * 1000UL;
    portEXIT_CRITICAL(&gPcntCounterMux);
    return true;
}
