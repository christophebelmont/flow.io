#pragma once
/**
 * @file RemoteHmiUdpDriver.h
 * @brief IHmiDriver implementation backed by the lightweight HMI UDP server.
 */

#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"
#include "Modules/Network/HmiUdpServerModule/HmiUdpServerModule.h"

class RemoteHmiUdpDriver final : public IHmiDriver {
public:
    void setUdpServer(HmiUdpServerModule* server) { udpServer_ = server; }

    const char* driverId() const override { return "remote_udp"; }
    bool begin() override;
    void tick(uint32_t nowMs) override;
    bool pollEvent(HmiEvent& out) override;
    bool publishHomeText(HmiHomeTextField field, const char* text) override;
    bool publishHomeGaugePercent(HmiHomeGaugeField field, uint16_t percent) override;
    bool publishHomeStateBits(uint32_t stateBits) override;
    bool publishHomeAlarmBits(uint32_t alarmBits) override;
    bool hasDisplayVersion() const override;
    const char* displayVersion() const override;
    bool isLegacyV2() const override;
    bool publishV2Needles(const NextionV2NeedlePublish& publish) override;
    bool readRtc(HmiRtcDateTime& out, uint16_t timeoutMs) override;
    bool writeRtc(const HmiRtcDateTime& value) override;
    bool renderConfigMenu(const ConfigMenuView& view) override;
    bool refreshConfigMenuValues(const ConfigMenuView& view) override;

private:
    HmiUdpServerModule* udpServer_ = nullptr;
};
