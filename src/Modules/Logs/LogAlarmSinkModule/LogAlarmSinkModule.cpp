/**
 * @file LogAlarmSinkModule.cpp
 * @brief Implementation file.
 */

#include "LogAlarmSinkModule.h"
#include <Arduino.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::LogSinkAlarm)
#include "Core/ModuleLog.h"

bool LogAlarmSinkModule::ignoredModule_(LogModuleId moduleId)
{
    // Avoid self-feedback loops from alarm engine/sink internals.
    return moduleId == (LogModuleId)LogModuleIdValue::AlarmModule ||
           moduleId == (LogModuleId)LogModuleIdValue::LogSinkAlarm;
}

void LogAlarmSinkModule::sinkWrite_(void* ctx, const LogEntry& e)
{
    SinkState* st = static_cast<SinkState*>(ctx);
    if (!st) return;
    if (ignoredModule_(e.moduleId)) return;

    const uint32_t nowMs = millis();
    if (e.lvl == LogLevel::Warn) {
        portENTER_CRITICAL(&st->mux);
        st->lastWarnMs = nowMs;
        portEXIT_CRITICAL(&st->mux);
    } else if (e.lvl == LogLevel::Error) {
        portENTER_CRITICAL(&st->mux);
        st->lastErrorMs = nowMs;
        portEXIT_CRITICAL(&st->mux);
    }
}

AlarmCondState LogAlarmSinkModule::condWarn_(void* ctx, uint32_t nowMs)
{
    SinkState* st = static_cast<SinkState*>(ctx);
    if (!st) return AlarmCondState::Unknown;

    uint32_t lastMs = 0;
    portENTER_CRITICAL(&st->mux);
    lastMs = st->lastWarnMs;
    portEXIT_CRITICAL(&st->mux);

    if (lastMs == 0U) return AlarmCondState::False;
    return ((uint32_t)(nowMs - lastMs) <= WARN_HOLD_MS) ? AlarmCondState::True : AlarmCondState::False;
}

AlarmCondState LogAlarmSinkModule::condError_(void* ctx, uint32_t nowMs)
{
    SinkState* st = static_cast<SinkState*>(ctx);
    if (!st) return AlarmCondState::Unknown;

    uint32_t lastMs = 0;
    portENTER_CRITICAL(&st->mux);
    lastMs = st->lastErrorMs;
    portEXIT_CRITICAL(&st->mux);

    if (lastMs == 0U) return AlarmCondState::False;
    return ((uint32_t)(nowMs - lastMs) <= ERROR_HOLD_MS) ? AlarmCondState::True : AlarmCondState::False;
}

void LogAlarmSinkModule::init(ConfigStore&, ServiceRegistry& services)
{
    const LogSinkRegistryService* sinks = services.get<LogSinkRegistryService>(ServiceId::LogSinks);
    const AlarmService* alarmSvc = services.get<AlarmService>(ServiceId::Alarm);
    if (!sinks || !sinks->add || !alarmSvc || !alarmSvc->registerAlarm) {
        return;
    }

    const AlarmRegistration warnAlarm{
        AlarmId::LogWarningSeen,
        AlarmSeverity::Warning,
        false,
        0,
        1000,
        10000,
        "log_warning",
        "Warning log detected",
        "log.sink",
        false
    };
    (void)alarmSvc->registerAlarm(alarmSvc->ctx, &warnAlarm, &LogAlarmSinkModule::condWarn_, &state_);

    const AlarmRegistration errAlarm{
        AlarmId::LogErrorSeen,
        AlarmSeverity::Alarm,
        false,
        0,
        1000,
        10000,
        "log_error",
        "Error log detected",
        "log.sink",
        false
    };
    (void)alarmSvc->registerAlarm(alarmSvc->ctx, &errAlarm, &LogAlarmSinkModule::condError_, &state_);

    LogSinkService sink{};
    sink.write = &LogAlarmSinkModule::sinkWrite_;
    sink.ctx = &state_;
    (void)sinks->add(sinks->ctx, sink);

    LOGI("Log alarm sink registered");
}
