#include "Modules/HMIModule/Drivers/RemoteHmiUdpDriver.h"

bool RemoteHmiUdpDriver::begin()
{
    return udpServer_ && udpServer_->begin();
}

void RemoteHmiUdpDriver::tick(uint32_t nowMs)
{
    if (udpServer_) udpServer_->tick(nowMs);
}

bool RemoteHmiUdpDriver::pollEvent(HmiEvent& out)
{
    return udpServer_ && udpServer_->pollEvent(out);
}

bool RemoteHmiUdpDriver::publishHomeText(HmiHomeTextField field, const char* text)
{
    if (udpServer_ && !udpServer_->isDisplayOnline()) return true;
    return udpServer_ && udpServer_->sendHomeText(field, text);
}

bool RemoteHmiUdpDriver::publishHomeGaugePercent(HmiHomeGaugeField field, uint16_t percent)
{
    if (udpServer_ && !udpServer_->isDisplayOnline()) return true;
    return udpServer_ && udpServer_->sendHomeGauge(field, percent);
}

bool RemoteHmiUdpDriver::publishHomeStateBits(uint32_t stateBits)
{
    if (udpServer_ && !udpServer_->isDisplayOnline()) return true;
    return udpServer_ && udpServer_->sendHomeStateBits(stateBits);
}

bool RemoteHmiUdpDriver::publishHomeAlarmBits(uint32_t alarmBits)
{
    if (udpServer_ && !udpServer_->isDisplayOnline()) return true;
    return udpServer_ && udpServer_->sendHomeAlarmBits(alarmBits);
}

bool RemoteHmiUdpDriver::hasDisplayVersion() const
{
    return udpServer_ && udpServer_->hasDisplayVersion();
}

const char* RemoteHmiUdpDriver::displayVersion() const
{
    return udpServer_ ? udpServer_->displayVersion() : "";
}

bool RemoteHmiUdpDriver::isLegacyV2() const
{
    return udpServer_ && udpServer_->isLegacyV2();
}

bool RemoteHmiUdpDriver::publishV2Needles(const NextionV2NeedlePublish& publish)
{
    if (udpServer_ && !udpServer_->isDisplayOnline()) return true;
    return udpServer_ && udpServer_->sendHomeV2Needles(publish);
}

bool RemoteHmiUdpDriver::readRtc(HmiRtcDateTime& out, uint16_t timeoutMs)
{
    out = HmiRtcDateTime{};
    return udpServer_ && udpServer_->requestRtcRead(out, timeoutMs);
}

bool RemoteHmiUdpDriver::writeRtc(const HmiRtcDateTime& value)
{
    return udpServer_ && udpServer_->sendRtcWrite(value);
}

bool RemoteHmiUdpDriver::renderConfigMenu(const ConfigMenuView& view)
{
    if (!udpServer_) return false;
    return udpServer_->sendConfigViewSnapshot(view);
}

bool RemoteHmiUdpDriver::refreshConfigMenuValues(const ConfigMenuView& view)
{
    if (!udpServer_) return false;
    bool ok = true;
    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        if (!view.rows[i].visible || !view.rows[i].valueVisible) continue;
        ok = udpServer_->sendConfigRow(i, view.rows[i], view.mode) && ok;
    }
    return ok;
}
