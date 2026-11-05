#pragma once
/**
 * @file HmiUdpServerModule.h
 * @brief Lightweight flow.io UDP endpoint for a remote HMI display.
 */

#include <WiFiUdp.h>

#include "Core/Hmi/HmiUdpProtocol.h"
#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"

class HmiUdpServerModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::HmiUdpServer; }
    const char* taskName() const override { return "hmiudp"; }
    uint8_t dependencyCount() const override { return 2; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override {}

    bool begin();
    void tick(uint32_t nowMs);

    bool isDisplayOnline() const { return displayOnline_; }
    bool isDisplaySleeping() const { return displaySleeping_; }
    bool hasDisplayVersion() const { return displayVersionDetected_; }
    const char* displayVersion() const { return displayVersion_; }
    bool isLegacyV2() const;
    bool consumeFullRefreshRequested();

    bool sendHomeText(HmiHomeTextField field, const char* text);
    bool sendHomeGauge(HmiHomeGaugeField field, uint16_t percent);
    bool sendHomeV2Needles(const NextionV2NeedlePublish& publish);
    bool sendHomeStateBits(uint32_t stateBits);
    bool sendHomeAlarmBits(uint32_t alarmBits);

    bool sendConfigStart(const ConfigMenuView& view);
    bool sendConfigRow(uint8_t row, const ConfigMenuRowView& viewRow, ConfigMenuMode mode);
    bool sendConfigEnd(uint8_t rowCount);
    bool sendConfigViewSnapshot(const ConfigMenuView& view);

    bool sendRtcWrite(const HmiRtcDateTime& value);
    bool requestRtcRead();
    bool requestRtcRead(HmiRtcDateTime& out, uint16_t timeoutMs);

    bool pollEvent(HmiEvent& out);

private:
    static constexpr uint8_t HMI_UDP_EVENT_QUEUE_SIZE = 8;
    static constexpr uint8_t HMI_UDP_OUT_QUEUE_SIZE = 12;
    static constexpr uint8_t HMI_UDP_OUT_PAYLOAD_MAX = 64;
    static constexpr size_t HMI_UDP_LARGE_PAYLOAD_MAX = HMI_UDP_MAX_PACKET - sizeof(HmiUdpHeader);
    static constexpr uint32_t OfflineTimeoutMs = 9000U;
    static constexpr uint32_t SleepOfflineTimeoutMs = 120000U;
    static constexpr uint32_t ReliableRetryMs = 150U;
    static constexpr uint8_t ReliableMaxAttempts = 7U;

    struct OutPacket {
        HmiUdpMsgType type = HmiUdpMsgType::Error;
        uint8_t len = 0;
        uint8_t payload[HMI_UDP_OUT_PAYLOAD_MAX]{};
    };

    struct ConfigData {
        char token[33]{};
    } cfgData_{};

    ConfigVariable<char,0> tokenVar_{
        NVS_KEY(NvsKeys::Hmi::FlowConnectUdpToken), "token", "hmi/nextion_udp",
        ConfigType::CharArray, cfgData_.token, ConfigPersistence::Persistent, sizeof(cfgData_.token)
    };

    WiFiUDP udp_{};
    const WifiService* wifiSvc_ = nullptr;
    DataStore* dataStore_ = nullptr;

    uint8_t rxBuf_[HMI_UDP_MAX_PACKET]{};
    uint8_t txBuf_[HMI_UDP_MAX_PACKET]{};

    IPAddress remoteIp_{};
    uint16_t remotePort_ = HMI_UDP_PORT;
    bool started_ = false;
    bool displayOnline_ = false;
    bool displaySleeping_ = false;
    bool fullRefreshRequested_ = false;
    bool displayVersionDetected_ = false;
    char displayVersion_[HMI_DISPLAY_VERSION_TEXT_MAX]{};
    uint16_t txSeq_ = 1;
    uint16_t lastRxSeq_ = 0;
    uint16_t lastEventSeq_ = 0;
    bool hasLastEventSeq_ = false;
    uint32_t lastSeenMs_ = 0;
    bool rtcResponseReady_ = false;
    bool rtcReadInProgress_ = false;
    HmiRtcDateTime rtcResponse_{};

    HmiEvent eventQueue_[HMI_UDP_EVENT_QUEUE_SIZE]{};
    uint8_t eventHead_ = 0;
    uint8_t eventTail_ = 0;

    OutPacket outQueue_[HMI_UDP_OUT_QUEUE_SIZE]{};
    uint8_t outHead_ = 0;
    uint8_t outTail_ = 0;
    uint8_t reliablePendingBuf_[HMI_UDP_MAX_PACKET]{};
    size_t reliablePendingLen_ = 0;
    uint16_t reliablePendingSeq_ = 0;
    HmiUdpMsgType reliablePendingType_ = HmiUdpMsgType::Error;
    uint8_t reliableAttempts_ = 0;
    uint32_t reliableLastSendMs_ = 0;

    bool sendPacket_(HmiUdpMsgType type, const void* payload, uint8_t payloadLen, uint8_t flags = 0);
    bool sendImmediate_(HmiUdpMsgType type, const void* payload, uint8_t payloadLen, uint8_t flags = 0);
    bool enqueueReliable_(HmiUdpMsgType type, const void* payload, uint8_t payloadLen);
    bool sendReliableLarge_(HmiUdpMsgType type, const void* payload, size_t payloadLen);
    bool buildReliablePending_(HmiUdpMsgType type, const void* payload, size_t payloadLen);
    bool loadNextReliable_();
    static void fillConfigRowPayload_(HmiUdpConfigRowPayload& payload,
                                      uint8_t row,
                                      const ConfigMenuRowView& viewRow,
                                      ConfigMenuMode mode);
    void serviceReliableTx_(uint32_t nowMs);
    void clearReliableQueue_(bool clearPending);
    void markDisplayOffline_(const char* reason, bool clearPending);
    bool isConfigMsg_(HmiUdpMsgType type) const;
    bool shouldSuppressUiTx_() const;
    bool sendAck_(uint16_t seq);
    void readUdp_(uint32_t nowMs);
    void handlePacket_(const HmiUdpHeader& header, const uint8_t* payload, uint32_t nowMs);
    void markRemote_(uint32_t nowMs);
    bool pushEvent_(const HmiEvent& event);
    bool wifiConnected_() const;
    bool tokenAccepted_(uint32_t tokenCrc) const;
    static void copyText_(char* out, size_t outLen, const char* in);
};
