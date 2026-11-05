#pragma once
/**
 * @file BootLogCaptureModule.h
 * @brief Passive boot log capture sink backed by PSRAM.
 */

#ifndef FLOW_ENABLE_BOOT_LOG_CAPTURE
#define FLOW_ENABLE_BOOT_LOG_CAPTURE 0
#endif

#if FLOW_ENABLE_BOOT_LOG_CAPTURE

#include "Core/ModulePassive.h"
#include "Core/Services/ILogger.h"

class BootLogCaptureModule : public ModulePassive {
public:
    ModuleId moduleId() const override { return ModuleId::BootLogCapture; }

    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override {
        return (i == 0) ? ModuleId::LogHub : ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;

private:
    static constexpr uint16_t kCapacity = 768;

    static void sinkWrite_(void* ctx, const LogEntry& e);
    static void serviceMarkComplete_(void* ctx);
    static void serviceGetStats_(void* ctx, BootLogCaptureStats* out);
    static uint16_t serviceReplay_(void* ctx, BootLogCaptureReplayWriter writer, void* writerCtx);
    static uint16_t serviceReadPage_(void* ctx,
                                     uint16_t offset,
                                     uint16_t limit,
                                     BootLogCaptureReplayWriter writer,
                                     void* writerCtx);

    void markComplete_();
    void getStats_(BootLogCaptureStats& out) const;
    uint16_t replay_(BootLogCaptureReplayWriter writer, void* writerCtx) const;
    uint16_t readPage_(uint16_t offset,
                       uint16_t limit,
                       BootLogCaptureReplayWriter writer,
                       void* writerCtx) const;
    void write_(const LogEntry& e);

    LogEntry* entries_ = nullptr;
    uint16_t capacity_ = 0;
    uint16_t head_ = 0;
    uint16_t count_ = 0;
    uint32_t droppedCount_ = 0;
    bool capturing_ = false;
    bool complete_ = false;
    bool inPsram_ = false;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    BootLogCaptureService service_{};
};

#endif
