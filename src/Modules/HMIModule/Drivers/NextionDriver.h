#pragma once
/**
 * @file NextionDriver.h
 * @brief Nextion HMI driver implementation.
 */

#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"

#include <Arduino.h>

struct NextionDriverConfig {
#ifdef FLOW_BOARD_WAVESHARE_ESP32_S3
    HardwareSerial* serial = &Serial2;
    int8_t rxPin = 44;
    int8_t txPin = 43;
#else
    HardwareSerial* serial = &Serial2;
    int8_t rxPin = 16;
    int8_t txPin = 17;
#endif
    uint32_t baud = 115200;
    uint32_t minRenderGapMs = 120;
    uint16_t displayVersionReadTimeoutMs = 180U;
    uint8_t homePageId = 0U;
    uint8_t configPageId = 10U;
    uint8_t alarmPageId = 11U;
    uint8_t homePageAliasId = 0xFFU;
    uint8_t configPageAliasId = 0xFFU;
    uint8_t alarmPageAliasId = 0xFFU;
};

using NextionDebugCallback = void (*)(void* ctx, const char* kind, const uint8_t* data, uint8_t len);

class NextionDriver final : public IHmiDriver {
public:
    NextionDriver() = default;

    void setConfig(const NextionDriverConfig& cfg) { cfg_ = cfg; }
    void setDebugCallback(NextionDebugCallback callback, void* ctx) {
        debugCallback_ = callback;
        debugCtx_ = ctx;
    }

    const char* driverId() const override { return "nextion"; }
    bool begin() override;
    void tick(uint32_t nowMs) override;
    bool pollEvent(HmiEvent& out) override;
    bool publishHomeText(HmiHomeTextField field, const char* text) override;
    bool publishHomeGaugePercent(HmiHomeGaugeField field, uint16_t percent) override;
    bool publishHomeStateBits(uint32_t stateBits) override;
    bool publishHomeAlarmBits(uint32_t alarmBits) override;
    bool readRtc(HmiRtcDateTime& out, uint16_t timeoutMs) override;
    bool writeRtc(const HmiRtcDateTime& value) override;
    bool renderConfigMenu(const ConfigMenuView& view) override;
    bool refreshConfigMenuValues(const ConfigMenuView& view) override;
    bool hasDisplayVersion() const override { return versionDetected_; }
    const char* displayVersion() const override { return displayVersion_; }
    bool isLegacyV2() const override;
    bool detectDisplayVersion(uint16_t timeoutMs = 0U, bool force = false);
    bool configureSleep(uint16_t noTouchSeconds, bool wakeOnTouch, bool wakeOnSerial);
    bool refreshSleepState(uint16_t timeoutMs = 0U);
    bool isSleeping() const { return sleeping_; }
    bool wakeFromSleep();
    bool requestPageReport();
    bool currentPage(uint8_t& out) const;
    bool isHomePage() const;
    bool isConfigPage() const;
    bool isAlarmPage() const;
    bool setTouchEnabled(bool enabled);
    bool setObjectVisible(const char* objectName, bool visible);
    bool showConfigLoading(const char* title);
    bool publishV2Needles(const NextionV2NeedlePublish& publish) override;

private:
    static constexpr uint8_t CustomRxBufSize = 64;
    static constexpr size_t TxBufSize = 160U;
    static constexpr uint8_t PageResponseBufSize = 4U;
    static constexpr uint8_t TouchResponseBufSize = 6U;

    NextionDriverConfig cfg_{};
    NextionDebugCallback debugCallback_ = nullptr;
    void* debugCtx_ = nullptr;
    bool started_ = false;
    bool pageReady_ = false;
    bool versionDetected_ = false;
    char displayVersion_[HMI_DISPLAY_VERSION_TEXT_MAX]{};
    uint32_t lastRenderMs_ = 0;
    bool sleeping_ = false;

    bool customFrameActive_ = false;
    uint8_t customExpectedLen_ = 0;
    uint8_t customLen_ = 0;
    uint8_t customBuf_[CustomRxBufSize]{};
    bool pageResponseActive_ = false;
    uint8_t pageResponseLen_ = 0;
    uint8_t pageResponseBuf_[PageResponseBufSize]{};
    bool touchResponseActive_ = false;
    uint8_t touchResponseLen_ = 0;
    uint8_t touchResponseBuf_[TouchResponseBufSize]{};
    bool currentPageKnown_ = false;
    uint8_t currentPage_ = 0;

    bool parseCustomFrame_(const uint8_t* frame, uint8_t len, HmiEvent& out);
    bool handlePageId_(uint8_t pageId, bool emitEvents, HmiEvent& out);
    bool isHomePageId_(uint8_t pageId) const;
    bool isConfigPageId_(uint8_t pageId) const;
    bool isAlarmPageId_(uint8_t pageId) const;
    bool isMenuPageId_(uint8_t pageId) const;
    void emitDebug_(const char* kind, const uint8_t* data, uint8_t len) const;

    bool sendCmd_(const char* cmd);
    bool sendCmdFmt_(const char* fmt, ...);
    bool sendNum_(const char* objectName, uint32_t value);
    bool sendInt_(const char* objectName, int32_t value);
    bool sendText_(const char* objectName, const char* value);
    bool readNumber_(const char* expr, uint32_t& value, uint16_t timeoutMs);
    bool readNumberResponse_(uint32_t& value, uint16_t timeoutMs);
    bool readText_(const char* expr, char* out, size_t outLen, uint16_t timeoutMs);
    bool readTextResponse_(char* out, size_t outLen, uint16_t timeoutMs);
    const char* homeTextObjectName_(HmiHomeTextField field) const;
    const char* homeGaugeObjectName_(HmiHomeGaugeField field) const;
    void sanitizeText_(char* out, size_t outLen, const char* in) const;
};
