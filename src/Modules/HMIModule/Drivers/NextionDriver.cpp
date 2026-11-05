/**
 * @file NextionDriver.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/Drivers/NextionDriver.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

static constexpr uint8_t NEXTION_FF = 0xFF;
static constexpr uint8_t NEXTION_RSP_STRING = 0x70;
static constexpr uint8_t NEXTION_RSP_NUMBER = 0x71;
static constexpr uint8_t NEXTION_RSP_PAGE = 0x66;
static constexpr uint8_t NEXTION_RSP_TOUCH = 0x65;
static constexpr uint8_t NEXTION_RSP_SLEEP = 0x86;
static constexpr uint8_t NEXTION_RSP_WAKE = 0x87;
static constexpr uint8_t NEXTION_CUSTOM_START = '#';
static constexpr uint8_t NEXTION_CMD_PAGE = 0x50;
static constexpr uint8_t NEXTION_CMD_NAV = 0x51;
static constexpr uint8_t NEXTION_CMD_ROW_ACTIVATE = 0x52;
static constexpr uint8_t NEXTION_CMD_ROW_TOGGLE = 0x53;
static constexpr uint8_t NEXTION_CMD_ROW_CYCLE = 0x54;
static constexpr uint8_t NEXTION_CMD_ROW_EDIT = 0x55;
static constexpr uint8_t NEXTION_CMD_ROW_SET_TEXT = 0x56;
static constexpr uint8_t NEXTION_CMD_ROW_SET_SLIDER = 0x57;
static constexpr uint8_t NEXTION_CMD_HOME_ACTION = 0x60;

static constexpr uint8_t NAV_HOME = 1;
static constexpr uint8_t NAV_BACK = 2;
static constexpr uint8_t NAV_VALIDATE = 3;
static constexpr uint8_t NAV_NEXT = 4;
static constexpr uint8_t NAV_PREV = 5;
static constexpr uint8_t NAV_CONFIG_EXIT = 6;
static constexpr uint8_t HOME_ACTION_FILTRATION = 1;
static constexpr uint8_t HOME_ACTION_AUTO_MODE = 2;
static constexpr uint8_t HOME_ACTION_SYNC = 3;
static constexpr uint8_t HOME_ACTION_CONFIG_OPEN = 4;
static constexpr uint8_t HOME_ACTION_PH_PUMP = 5;
static constexpr uint8_t HOME_ACTION_ORP_PUMP = 6;
static constexpr uint8_t HOME_ACTION_PH_PUMP_TOGGLE = 7;
static constexpr uint8_t HOME_ACTION_ORP_PUMP_TOGGLE = 8;
static constexpr uint8_t HOME_ACTION_FILTRATION_TOGGLE = 9;
static constexpr uint8_t HOME_ACTION_AUTO_MODE_TOGGLE = 10;
static constexpr uint8_t HOME_ACTION_ORP_AUTO_MODE_TOGGLE = 11;
static constexpr uint8_t HOME_ACTION_PH_AUTO_MODE_TOGGLE = 12;
static constexpr uint8_t HOME_ACTION_WINTER_MODE_TOGGLE = 13;
static constexpr uint8_t HOME_ACTION_LIGHTS_TOGGLE = 14;
static constexpr uint8_t HOME_ACTION_ROBOT_TOGGLE = 15;
static constexpr uint8_t HOME_ACTION_DISPLAY_WIFI_FACTORY_RESET = 16;

namespace NextionObject {
static constexpr const char* WaterTempText = "globals.vaWaterTemp";
static constexpr const char* AirTempText = "globals.vaAirTemp";
static constexpr const char* PhText = "globals.vaPhValue";
static constexpr const char* OrpText = "globals.vaOrpValue";
static constexpr const char* TimeText = "tTime";
static constexpr const char* DateText = "tDate";
static constexpr const char* ErrorMessageText = "globals.vaErrMsg";
static constexpr const char* DayText = "globals.vaDayText";
static constexpr const char* MonthText = "globals.vaMonthText";
static constexpr const char* PhGaugePercent = "vapHPercent";
static constexpr const char* OrpGaugePercent = "vaOrpPercent";
static constexpr const char* StateBits = "globals.vaStates";
static constexpr const char* AlarmBits = "globals.vaAlarms";
static constexpr const char* DisplayVersionExpr = "globals.vaVersion.txt";
} // namespace NextionObject

static bool versionMajorEquals_(const char* version, uint8_t expected)
{
    if (!version || version[0] < '0' || version[0] > '9') return false;
    uint16_t major = 0U;
    size_t pos = 0U;
    while (version[pos] >= '0' && version[pos] <= '9') {
        major = (uint16_t)(major * 10U + (uint16_t)(version[pos] - '0'));
        if (major > 99U) return false;
        ++pos;
    }
    return pos > 0U && version[pos] == '.' && major == expected;
}

static bool isVersionTextValid_(const char* version)
{
    if (!version) return false;
    uint8_t part = 0U;
    uint8_t digits = 0U;
    for (size_t i = 0U; version[i] != '\0'; ++i) {
        const char c = version[i];
        if (c >= '0' && c <= '9') {
            if (++digits > 2U) return false;
            continue;
        }
        if (c == '.' && digits > 0U && part < 2U) {
            ++part;
            digits = 0U;
            continue;
        }
        return false;
    }
    return part == 2U && digits > 0U;
}

static bool isAlarmRowKey_(const char* key)
{
    return key &&
           key[0] == 'a' &&
           key[1] == 'l' &&
           key[2] == 'a' &&
           key[3] == 'r' &&
           key[4] == 'm' &&
           key[5] == '_';
}

} // namespace

bool NextionDriver::begin()
{
    if (started_) return true;
    if (!cfg_.serial) return false;

    if (cfg_.rxPin >= 0 && cfg_.txPin >= 0) {
        cfg_.serial->begin(cfg_.baud, SERIAL_8N1, cfg_.rxPin, cfg_.txPin);
    } else {
        cfg_.serial->begin(cfg_.baud);
    }
    delay(30);
    started_ = true;
    pageReady_ = false;
    versionDetected_ = false;
    displayVersion_[0] = '\0';
    lastRenderMs_ = 0;
    sleeping_ = false;
    customFrameActive_ = false;
    customExpectedLen_ = 0;
    customLen_ = 0;
    pageResponseActive_ = false;
    pageResponseLen_ = 0;
    touchResponseActive_ = false;
    touchResponseLen_ = 0;
    currentPageKnown_ = false;
    currentPage_ = 0;

    (void)refreshSleepState();
    (void)detectDisplayVersion();
    return true;
}

void NextionDriver::tick(uint32_t)
{
}

bool NextionDriver::sendCmd_(const char* cmd)
{
    if (!started_ || !cfg_.serial || !cmd) return false;
    cfg_.serial->print(cmd);
    cfg_.serial->write(NEXTION_FF);
    cfg_.serial->write(NEXTION_FF);
    cfg_.serial->write(NEXTION_FF);
    return true;
}

bool NextionDriver::requestPageReport()
{
    if (sleeping_) return false;
    return sendCmd_("sendme");
}

bool NextionDriver::currentPage(uint8_t& out) const
{
    if (!currentPageKnown_) return false;
    out = currentPage_;
    return true;
}

bool NextionDriver::isHomePage() const
{
    return currentPageKnown_ && isHomePageId_(currentPage_);
}

bool NextionDriver::isConfigPage() const
{
    return currentPageKnown_ && isMenuPageId_(currentPage_);
}

bool NextionDriver::isAlarmPage() const
{
    return currentPageKnown_ && isAlarmPageId_(currentPage_);
}

bool NextionDriver::setTouchEnabled(bool enabled)
{
    if (sleeping_) return false;
    return sendCmdFmt_("tsw 255,%u", enabled ? 1U : 0U);
}

bool NextionDriver::setObjectVisible(const char* objectName, bool visible)
{
    if (!started_ || !objectName || objectName[0] == '\0') return false;
    if (sleeping_) return false;
    return sendCmdFmt_("vis %s,%u", objectName, visible ? 1U : 0U);
}

bool NextionDriver::showConfigLoading(const char* title)
{
    if (sleeping_) return false;
    if (!started_ || !pageReady_) return false;

    bool ok = true;
    char path[96]{};
    if (title && title[0] != '\0') {
        snprintf(path, sizeof(path), "%s - Chargement...", title);
    } else {
        snprintf(path, sizeof(path), "Chargement...");
    }
    ok = sendText_("tPath", path) && ok;
    ok = sendCmd_("vis bHome,0") && ok;
    ok = sendCmd_("vis bBack,0") && ok;
    ok = sendCmd_("vis bValid,0") && ok;
    ok = sendCmd_("vis bPrev,0") && ok;
    ok = sendCmd_("vis bNext,0") && ok;
    ok = sendCmd_("vaCtxRef.val=0") && ok;
    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        char leftObj[8]{};
        char rightObj[8]{};
        char rowButtonObj[8]{};
        char editTypeObj[16]{};
        snprintf(leftObj, sizeof(leftObj), "tL%u", (unsigned)i);
        snprintf(rightObj, sizeof(rightObj), "tV%u", (unsigned)i);
        snprintf(rowButtonObj, sizeof(rowButtonObj), "bR%u", (unsigned)i);
        snprintf(editTypeObj, sizeof(editTypeObj), "vaEditType%u", (unsigned)i);
        ok = sendCmdFmt_("vis %s,0", leftObj) && ok;
        ok = sendCmdFmt_("vis %s,0", rightObj) && ok;
        ok = sendCmdFmt_("vis %s,0", rowButtonObj) && ok;
    }
    return ok;
}

bool NextionDriver::sendCmdFmt_(const char* fmt, ...)
{
    if (!fmt) return false;
    char cmd[TxBufSize]{};
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return false;
    return sendCmd_(cmd);
}

bool NextionDriver::sendNum_(const char* objectName, uint32_t value)
{
    if (!objectName || objectName[0] == '\0') return false;
    return sendCmdFmt_("%s.val=%lu", objectName, (unsigned long)value);
}

bool NextionDriver::sendInt_(const char* objectName, int32_t value)
{
    if (!objectName || objectName[0] == '\0') return false;
    return sendCmdFmt_("%s.val=%ld", objectName, (long)value);
}

void NextionDriver::sanitizeText_(char* out, size_t outLen, const char* in) const
{
    if (!out || outLen == 0) return;
    if (!in) in = "";

    size_t pos = 0;
    for (size_t i = 0; in[i] != '\0' && pos + 1 < outLen; ++i) {
        const unsigned char uc = (unsigned char)in[i];
        if (uc < 32U) continue;
        const char c = (char)uc;
        if (c == '"' || c == '\\') {
            if (pos + 2 >= outLen) break;
            out[pos++] = '\\';
            out[pos++] = c;
            continue;
        }
        out[pos++] = c;
    }
    out[pos] = '\0';
}

bool NextionDriver::sendText_(const char* objectName, const char* value)
{
    if (!objectName || objectName[0] == '\0') return false;
    char safe[96]{};
    char cmd[TxBufSize]{};
    sanitizeText_(safe, sizeof(safe), value);

    const size_t nameLen = strlen(objectName);
    const size_t safeLen = strlen(safe);
    if (nameLen + safeLen + 8U >= sizeof(cmd)) return false;

    size_t pos = 0U;
    memcpy(cmd + pos, objectName, nameLen);
    pos += nameLen;
    cmd[pos++] = '.';
    cmd[pos++] = 't';
    cmd[pos++] = 'x';
    cmd[pos++] = 't';
    cmd[pos++] = '=';
    cmd[pos++] = '"';
    memcpy(cmd + pos, safe, safeLen);
    pos += safeLen;
    cmd[pos++] = '"';
    cmd[pos] = '\0';
    return sendCmd_(cmd);
}

const char* NextionDriver::homeTextObjectName_(HmiHomeTextField field) const
{
    switch (field) {
        case HmiHomeTextField::WaterTemp:
            return NextionObject::WaterTempText;
        case HmiHomeTextField::AirTemp:
            return NextionObject::AirTempText;
        case HmiHomeTextField::Ph:
            return NextionObject::PhText;
        case HmiHomeTextField::Orp:
            return NextionObject::OrpText;
        case HmiHomeTextField::Time:
            return NextionObject::TimeText;
        case HmiHomeTextField::Date:
            return NextionObject::DateText;
        case HmiHomeTextField::ErrorMessage:
            return NextionObject::ErrorMessageText;
        case HmiHomeTextField::DayText:
            return NextionObject::DayText;
        case HmiHomeTextField::MonthText:
            return NextionObject::MonthText;
        default:
            return nullptr;
    }
}

const char* NextionDriver::homeGaugeObjectName_(HmiHomeGaugeField field) const
{
    switch (field) {
        case HmiHomeGaugeField::PhPercent:
            return NextionObject::PhGaugePercent;
        case HmiHomeGaugeField::OrpPercent:
            return NextionObject::OrpGaugePercent;
        default:
            return nullptr;
    }
}

bool NextionDriver::publishHomeText(HmiHomeTextField field, const char* text)
{
    if (!started_) return false;
    if (sleeping_) return false;
    return sendText_(homeTextObjectName_(field), text);
}

bool NextionDriver::publishHomeGaugePercent(HmiHomeGaugeField field, uint16_t percent)
{
    if (!started_) return false;
    if (sleeping_) return false;
    return sendNum_(homeGaugeObjectName_(field), percent);
}

bool NextionDriver::publishHomeStateBits(uint32_t stateBits)
{
    if (!started_) return false;
    if (sleeping_) return false;
    return sendNum_(NextionObject::StateBits, stateBits);
}

bool NextionDriver::publishHomeAlarmBits(uint32_t alarmBits)
{
    if (!started_) return false;
    if (sleeping_) return false;
    return sendNum_(NextionObject::AlarmBits, alarmBits);
}

bool NextionDriver::isLegacyV2() const
{
    return versionDetected_ && versionMajorEquals_(displayVersion_, 2U);
}

bool NextionDriver::publishV2Needles(const NextionV2NeedlePublish& publish)
{
    if (!started_ || !isLegacyV2()) return false;
    if (sleeping_) return false;

    bool ok = true;
    if (publish.ph) ok = sendInt_("vaPHNiddle", publish.phNeedle) && ok;
    if (publish.orp) ok = sendInt_("vaOrpNiddle", publish.orpNeedle) && ok;
    if (publish.psi) ok = sendNum_("vaPSINiddle", publish.psiNeedle) && ok;
    return ok;
}

bool NextionDriver::readNumberResponse_(uint32_t& value, uint16_t timeoutMs)
{
    value = 0U;
    if (!started_ || !cfg_.serial) return false;

    uint8_t frame[12]{};
    uint8_t len = 0U;
    uint8_t ffCount = 0U;
    const uint32_t start = millis();

    while ((uint32_t)(millis() - start) < (uint32_t)timeoutMs) {
        while (cfg_.serial->available() > 0) {
            const int rb = cfg_.serial->read();
            if (rb < 0) break;
            const uint8_t b = (uint8_t)rb;

            if (b == NEXTION_FF) {
                ++ffCount;
                if (ffCount >= 3U) {
                    if (len >= 5U && frame[0] == NEXTION_RSP_NUMBER) {
                        value = (uint32_t)frame[1] |
                                ((uint32_t)frame[2] << 8) |
                                ((uint32_t)frame[3] << 16) |
                                ((uint32_t)frame[4] << 24);
                        return true;
                    }
                    len = 0U;
                    ffCount = 0U;
                }
                continue;
            }

            while (ffCount > 0U) {
                if (len < sizeof(frame)) frame[len++] = NEXTION_FF;
                --ffCount;
            }

            if (len < sizeof(frame)) {
                frame[len++] = b;
            } else {
                len = 0U;
                ffCount = 0U;
            }
        }
        delay(1);
    }

    return false;
}

bool NextionDriver::readTextResponse_(char* out, size_t outLen, uint16_t timeoutMs)
{
    if (!out || outLen == 0U) return false;
    out[0] = '\0';
    if (!started_ || !cfg_.serial) return false;

    char frame[HMI_DISPLAY_VERSION_TEXT_MAX]{};
    uint8_t len = 0U;
    uint8_t ffCount = 0U;
    bool stringFrame = false;
    const uint32_t start = millis();

    while ((uint32_t)(millis() - start) < (uint32_t)timeoutMs) {
        while (cfg_.serial->available() > 0) {
            const int rb = cfg_.serial->read();
            if (rb < 0) break;
            const uint8_t b = (uint8_t)rb;

            if (b == NEXTION_FF) {
                ++ffCount;
                if (ffCount >= 3U) {
                    if (stringFrame) {
                        frame[len] = '\0';
                        strncpy(out, frame, outLen - 1U);
                        out[outLen - 1U] = '\0';
                        return true;
                    }
                    len = 0U;
                    ffCount = 0U;
                    stringFrame = false;
                }
                continue;
            }

            if (ffCount > 0U) {
                len = 0U;
                ffCount = 0U;
                stringFrame = false;
            }

            if (!stringFrame) {
                if (b == NEXTION_RSP_STRING) {
                    stringFrame = true;
                    len = 0U;
                }
                continue;
            }

            if (len + 1U < sizeof(frame)) {
                frame[len++] = (char)b;
            } else {
                len = 0U;
                ffCount = 0U;
                stringFrame = false;
            }
        }
        delay(1);
    }

    return false;
}

bool NextionDriver::readNumber_(const char* expr, uint32_t& value, uint16_t timeoutMs)
{
    if (!started_ || !cfg_.serial || !expr || expr[0] == '\0') return false;

    while (cfg_.serial->available() > 0) {
        (void)cfg_.serial->read();
    }
    customFrameActive_ = false;
    customExpectedLen_ = 0U;
    customLen_ = 0U;
    pageResponseActive_ = false;
    pageResponseLen_ = 0U;
    touchResponseActive_ = false;
    touchResponseLen_ = 0U;

    if (!sendCmdFmt_("get %s", expr)) return false;
    return readNumberResponse_(value, timeoutMs);
}

bool NextionDriver::readText_(const char* expr, char* out, size_t outLen, uint16_t timeoutMs)
{
    if (!started_ || !cfg_.serial || !expr || expr[0] == '\0' || !out || outLen == 0U) return false;

    while (cfg_.serial->available() > 0) {
        (void)cfg_.serial->read();
    }
    customFrameActive_ = false;
    customExpectedLen_ = 0U;
    customLen_ = 0U;
    pageResponseActive_ = false;
    pageResponseLen_ = 0U;
    touchResponseActive_ = false;
    touchResponseLen_ = 0U;

    if (!sendCmdFmt_("get %s", expr)) return false;
    return readTextResponse_(out, outLen, timeoutMs);
}

bool NextionDriver::detectDisplayVersion(uint16_t timeoutMs, bool force)
{
    if (versionDetected_ && !force) return true;

    char detected[HMI_DISPLAY_VERSION_TEXT_MAX]{};
    const uint16_t effectiveTimeout = timeoutMs != 0U ? timeoutMs : cfg_.displayVersionReadTimeoutMs;
    if (!readText_(NextionObject::DisplayVersionExpr, detected, sizeof(detected), effectiveTimeout)) return false;
    if (!isVersionTextValid_(detected)) return false;

    strncpy(displayVersion_, detected, sizeof(displayVersion_) - 1U);
    displayVersion_[sizeof(displayVersion_) - 1U] = '\0';
    versionDetected_ = true;
    return true;
}

bool NextionDriver::configureSleep(uint16_t noTouchSeconds, bool wakeOnTouch, bool wakeOnSerial)
{
    if (!started_ || sleeping_) return false;
    bool ok = true;
    ok = sendCmdFmt_("thsp=%u", (unsigned)noTouchSeconds) && ok;
    ok = sendCmdFmt_("thup=%u", wakeOnTouch ? 1U : 0U) && ok;
    ok = sendCmdFmt_("usup=%u", wakeOnSerial ? 1U : 0U) && ok;
    ok = sendCmd_("ussp=0") && ok;
    ok = sendCmd_("wup=255") && ok;
    return ok;
}

bool NextionDriver::refreshSleepState(uint16_t timeoutMs)
{
    if (!started_) return false;
    uint32_t value = 0U;
    const uint16_t effectiveTimeout = timeoutMs != 0U ? timeoutMs : cfg_.displayVersionReadTimeoutMs;
    if (!readNumber_("sleep", value, effectiveTimeout)) return false;
    sleeping_ = value != 0U;
    return true;
}

bool NextionDriver::wakeFromSleep()
{
    if (!started_) return false;
    if (!sendCmd_("sleep=0")) return false;
    sleeping_ = false;
    return true;
}

bool NextionDriver::readRtc(HmiRtcDateTime& out, uint16_t timeoutMs)
{
    out = HmiRtcDateTime{};
    if (!started_) return false;

    uint32_t v = 0U;
    if (!readNumber_("rtc0", v, timeoutMs)) return false;
    out.year = (uint16_t)v;
    if (!readNumber_("rtc1", v, timeoutMs)) return false;
    out.month = (uint8_t)v;
    if (!readNumber_("rtc2", v, timeoutMs)) return false;
    out.day = (uint8_t)v;
    if (!readNumber_("rtc3", v, timeoutMs)) return false;
    out.hour = (uint8_t)v;
    if (!readNumber_("rtc4", v, timeoutMs)) return false;
    out.minute = (uint8_t)v;
    if (!readNumber_("rtc5", v, timeoutMs)) return false;
    out.second = (uint8_t)v;
    return true;
}

bool NextionDriver::writeRtc(const HmiRtcDateTime& value)
{
    if (!started_) return false;
    if (sleeping_) return false;

    bool ok = true;
    ok = sendCmdFmt_("rtc0=%u", (unsigned)value.year) && ok;
    ok = sendCmdFmt_("rtc1=%u", (unsigned)value.month) && ok;
    ok = sendCmdFmt_("rtc2=%u", (unsigned)value.day) && ok;
    ok = sendCmdFmt_("rtc3=%u", (unsigned)value.hour) && ok;
    ok = sendCmdFmt_("rtc4=%u", (unsigned)value.minute) && ok;
    ok = sendCmdFmt_("rtc5=%u", (unsigned)value.second) && ok;
    return ok;
}

bool NextionDriver::renderConfigMenu(const ConfigMenuView& view)
{
    if (!started_) return false;
    if (sleeping_) return false;
    if (!pageReady_) return false;
    const uint32_t now = millis();
    if (cfg_.minRenderGapMs > 0 && (uint32_t)(now - lastRenderMs_) < cfg_.minRenderGapMs) {
        return false;
    }

    (void)sendText_("tPath", view.breadcrumb);
    (void)sendCmdFmt_("vis bHome,%u", view.canHome ? 1U : 0U);
    (void)sendCmdFmt_("tsw bHome,%u", view.canHome ? 1U : 0U);
    (void)sendCmdFmt_("vis bBack,%u", view.canBack ? 1U : 0U);
    (void)sendCmdFmt_("tsw bBack,%u", view.canBack ? 1U : 0U);
    (void)sendCmdFmt_("vis bValid,%u", view.canValidate ? 1U : 0U);
    (void)sendCmdFmt_("tsw bValid,%u", view.canValidate ? 1U : 0U);
    const bool canPrev = view.pageIndex > 0U;
    const bool canNext = view.pageIndex + 1U < view.pageCount;
    (void)sendCmdFmt_("vis bPrev,%u", canPrev ? 1U : 0U);
    (void)sendCmdFmt_("tsw bPrev,%u", canPrev ? 1U : 0U);
    (void)sendCmdFmt_("vis bNext,%u", canNext ? 1U : 0U);
    (void)sendCmdFmt_("tsw bNext,%u", canNext ? 1U : 0U);
    (void)sendCmdFmt_("nPage.val=%u", (unsigned)(view.pageIndex + 1U));
    (void)sendCmdFmt_("nPages.val=%u", (unsigned)view.pageCount);
    (void)sendCmdFmt_("vaCtxRef.val=%lu", (unsigned long)view.contextRef);

    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        char leftObj[8]{};
        char rightObj[8]{};
        char rowButtonObj[8]{};
        char editTypeObj[16]{};
        snprintf(leftObj, sizeof(leftObj), "tL%u", (unsigned)i);
        snprintf(rightObj, sizeof(rightObj), "tV%u", (unsigned)i);
        snprintf(rowButtonObj, sizeof(rowButtonObj), "bR%u", (unsigned)i);
        snprintf(editTypeObj, sizeof(editTypeObj), "vaEditType%u", (unsigned)i);

        const ConfigMenuRowView& row = view.rows[i];
        const bool alarmRow = isAlarmRowKey_(row.key);
        const bool alarmCritical = alarmRow && row.visible && strcmp(row.value, "f") == 0;
        const bool alarmLatchOk = alarmRow && row.visible && strcmp(row.value, "o") == 0;
        const bool showSwitch = !alarmRow &&
                                row.visible &&
                                row.valueVisible &&
                                row.widget == ConfigMenuWidget::Switch;
        bool showValue = row.visible && row.valueVisible;
        bool showRowButton = row.visible &&
                             view.mode == ConfigMenuMode::Browse &&
                             (row.canEdit || row.canEnter);
        bool leftTouchable = row.visible && view.mode == ConfigMenuMode::Browse;
        if (alarmRow) {
            showValue = alarmCritical || alarmLatchOk;
            showRowButton = alarmLatchOk;
            leftTouchable = showRowButton;
        }
        uint8_t editType = row.editType;
        if (editType > 3U) editType = 0U;
        if (!row.visible || !row.valueVisible || !row.editable) editType = 0U;

        (void)sendCmdFmt_("vis %s,%u", leftObj, row.visible ? 1U : 0U);
        (void)sendCmdFmt_("tsw %s,%u", leftObj, leftTouchable ? 1U : 0U);
        (void)sendCmdFmt_("vis %s,%u", rightObj, showValue ? 1U : 0U);
        (void)sendCmdFmt_("vis %s,%u", rowButtonObj, showRowButton ? 1U : 0U);
        (void)sendCmdFmt_("tsw %s,%u", rowButtonObj, showRowButton ? 1U : 0U);
        if (alarmRow) {
            const uint32_t leftBco = alarmCritical ? 53925U : (alarmLatchOk ? 64520U : 12710U);
            (void)sendCmdFmt_("%s.bco=%lu", leftObj, (unsigned long)leftBco);
            if (showValue) {
                (void)sendCmdFmt_("%s.bco=%lu", rightObj, (unsigned long)(alarmCritical ? 53925U : 64520U));
            }
            if (alarmLatchOk) {
                (void)sendCmdFmt_("%s.bco=%u", rowButtonObj, 64520U);
            }
        }
        (void)sendCmdFmt_("%s.val=%u", editTypeObj, (unsigned)editType);
        if (!showValue) {
            (void)sendCmdFmt_("tsw %s,0", rightObj);
            (void)sendCmdFmt_("%s.val=0", rightObj);
        }
        if (!row.visible) {
            continue;
        }

        char paddedLabel[64]{};
        snprintf(paddedLabel, sizeof(paddedLabel), " %s", row.label);
        (void)sendText_(leftObj, paddedLabel);
        if (!row.valueVisible) continue;

        if (showSwitch) {
            const bool on = (strcmp(row.value, "ON") == 0);
            (void)sendCmdFmt_("tsw %s,1", rightObj);
            (void)sendCmdFmt_("%s.val=%u", rightObj, on ? 1U : 0U);
            (void)sendText_(rightObj, on ? "ON" : "OFF");
            continue;
        }

        char displayVal[64]{};
        snprintf(displayVal, sizeof(displayVal), "%s%s", row.value, row.dirty ? " *" : "");
        (void)sendCmdFmt_("tsw %s,%u", rightObj, row.editable ? 1U : 0U);
        (void)sendCmdFmt_("%s.val=0", rightObj);
        (void)sendText_(rightObj, displayVal);
    }

    lastRenderMs_ = now;
    return true;
}

bool NextionDriver::refreshConfigMenuValues(const ConfigMenuView& view)
{
    if (!started_ || !pageReady_) return false;
    if (sleeping_) return false;
    (void)sendCmdFmt_("vaCtxRef.val=%lu", (unsigned long)view.contextRef);

    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        const ConfigMenuRowView& row = view.rows[i];
        const bool alarmRow = isAlarmRowKey_(row.key);
        const bool alarmCritical = alarmRow && row.visible && strcmp(row.value, "f") == 0;
        const bool alarmLatchOk = alarmRow && row.visible && strcmp(row.value, "o") == 0;
        bool showValue = row.visible && row.valueVisible;
        bool showRowButton = row.visible &&
                             view.mode == ConfigMenuMode::Browse &&
                             (row.canEdit || row.canEnter);
        if (alarmRow) {
            showValue = alarmCritical || alarmLatchOk;
            showRowButton = alarmLatchOk;
        }

        char leftObj[8]{};
        char rowButtonObj[8]{};
        snprintf(leftObj, sizeof(leftObj), "tL%u", (unsigned)i);
        snprintf(rowButtonObj, sizeof(rowButtonObj), "bR%u", (unsigned)i);

        if (alarmRow) {
            const uint32_t leftBco = alarmCritical ? 53925U : (alarmLatchOk ? 64520U : 12710U);
            (void)sendCmdFmt_("%s.bco=%lu", leftObj, (unsigned long)leftBco);
            (void)sendCmdFmt_("vis %s,%u", rowButtonObj, showRowButton ? 1U : 0U);
            (void)sendCmdFmt_("tsw %s,%u", rowButtonObj, showRowButton ? 1U : 0U);
            if (alarmLatchOk) {
                (void)sendCmdFmt_("%s.bco=%u", rowButtonObj, 64520U);
            }
        }

        char editTypeObj[16]{};
        snprintf(editTypeObj, sizeof(editTypeObj), "vaEditType%u", (unsigned)i);
        uint8_t editType = row.editType;
        if (editType > 3U) editType = 0U;
        if (!row.visible || !row.valueVisible || !row.editable) editType = 0U;
        (void)sendCmdFmt_("%s.val=%u", editTypeObj, (unsigned)editType);
        if (!row.visible || !showValue) {
            char rightObjHidden[8]{};
            snprintf(rightObjHidden, sizeof(rightObjHidden), "tV%u", (unsigned)i);
            (void)sendCmdFmt_("vis %s,0", rightObjHidden);
            (void)sendCmdFmt_("tsw %s,0", rightObjHidden);
            continue;
        }

        char rightObj[8]{};
        snprintf(rightObj, sizeof(rightObj), "tV%u", (unsigned)i);
        (void)sendCmdFmt_("vis %s,1", rightObj);
        if (alarmRow && showValue) {
            (void)sendCmdFmt_("%s.bco=%lu", rightObj, (unsigned long)(alarmCritical ? 53925U : 64520U));
        }

        if (row.widget == ConfigMenuWidget::Switch) {
            const bool on = (strcmp(row.value, "ON") == 0);
            (void)sendCmdFmt_("tsw %s,1", rightObj);
            (void)sendCmdFmt_("%s.val=%u", rightObj, on ? 1U : 0U);
            (void)sendText_(rightObj, on ? "ON" : "OFF");
            continue;
        }

        char displayVal[64]{};
        snprintf(displayVal, sizeof(displayVal), "%s%s", row.value, row.dirty ? " *" : "");
        (void)sendCmdFmt_("tsw %s,%u", rightObj, row.editable ? 1U : 0U);
        (void)sendCmdFmt_("%s.val=0", rightObj);
        (void)sendText_(rightObj, displayVal);
    }

    return true;
}

bool NextionDriver::parseCustomFrame_(const uint8_t* frame, uint8_t len, HmiEvent& out)
{
    if (!frame || len == 0) return false;

    const uint8_t opcode = frame[0];
    const uint8_t* payload = frame + 1;
    const uint8_t payloadLen = (len > 0U) ? (uint8_t)(len - 1U) : 0U;

    switch (opcode) {
        case NEXTION_CMD_PAGE: {
            if (payloadLen < 1U) return false;
            uint32_t contextRef = 0U;
            if (payloadLen >= 5U) {
                contextRef = (uint32_t)payload[1] |
                             ((uint32_t)payload[2] << 8) |
                             ((uint32_t)payload[3] << 16) |
                             ((uint32_t)payload[4] << 24);
            }
            const bool emitted = handlePageId_(payload[0], true, out);
            if (emitted) {
                out.contextRef = contextRef;
                out.pageId = payload[0];
            }
            return emitted;
        }

        case NEXTION_CMD_NAV:
            if (payloadLen < 1U) return false;
            switch (payload[0]) {
                case NAV_HOME:
                    out.type = HmiEventType::Home;
                    return true;
                case NAV_BACK:
                    out.type = HmiEventType::Back;
                    return true;
                case NAV_VALIDATE:
                    out.type = HmiEventType::Validate;
                    return true;
                case NAV_NEXT:
                    out.type = HmiEventType::NextPage;
                    return true;
                case NAV_PREV:
                    out.type = HmiEventType::PrevPage;
                    return true;
                case NAV_CONFIG_EXIT:
                    pageReady_ = false;
                    out.type = HmiEventType::ConfigExit;
                    return true;
                default:
                    return false;
            }

        case NEXTION_CMD_ROW_ACTIVATE:
            if (payloadLen < 1U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowActivate;
            out.row = payload[0];
            return true;

        case NEXTION_CMD_ROW_TOGGLE:
            if (payloadLen < 1U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowToggle;
            out.row = payload[0];
            return true;

        case NEXTION_CMD_ROW_CYCLE:
            if (payloadLen < 2U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowCycle;
            out.row = payload[0];
            out.direction = (payload[1] == 0U || payload[1] == 0xFFU) ? -1 : 1;
            return true;

        case NEXTION_CMD_ROW_EDIT:
            if (payloadLen < 1U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowEdit;
            out.row = payload[0];
            return true;

        case NEXTION_CMD_ROW_SET_TEXT: {
            if (payloadLen < 2U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowSetText;
            out.row = payload[0];
            const uint8_t textLen = (uint8_t)(payloadLen - 1U);
            const uint8_t copyLen = (textLen < sizeof(out.text) - 1U) ? textLen : (uint8_t)(sizeof(out.text) - 1U);
            memcpy(out.text, payload + 1, copyLen);
            out.text[copyLen] = '\0';
            return true;
        }

        case NEXTION_CMD_ROW_SET_SLIDER:
            if (payloadLen < 2U || payload[0] >= ConfigMenuModel::RowsPerPage) return false;
            out.type = HmiEventType::RowSetSlider;
            out.row = payload[0];
            if (payloadLen >= 5U) {
                float value = 0.0f;
                memcpy(&value, payload + 1, sizeof(value));
                out.sliderValue = value;
            } else {
                out.sliderValue = (float)payload[1];
            }
            return true;

        case NEXTION_CMD_HOME_ACTION:
            if (payloadLen < 1U) return false;
            out.type = HmiEventType::Command;
            out.value = (payloadLen >= 2U) ? payload[1] : 1U;
            switch (payload[0]) {
                case HOME_ACTION_FILTRATION:
                    out.command = (payloadLen >= 2U) ? HmiCommandId::HomeFiltrationSet : HmiCommandId::HomeFiltrationToggle;
                    return true;
                case HOME_ACTION_AUTO_MODE:
                    out.command = (payloadLen >= 2U) ? HmiCommandId::HomeAutoModeSet : HmiCommandId::HomeAutoModeToggle;
                    return true;
                case HOME_ACTION_SYNC:
                    out.command = HmiCommandId::HomeSyncRequest;
                    return true;
                case HOME_ACTION_CONFIG_OPEN:
                    out.command = HmiCommandId::HomeConfigOpen;
                    return true;
                case HOME_ACTION_PH_PUMP:
                    out.command = (payloadLen >= 2U) ? HmiCommandId::HomePhPumpSet : HmiCommandId::HomePhPumpToggle;
                    return true;
                case HOME_ACTION_ORP_PUMP:
                    out.command = (payloadLen >= 2U) ? HmiCommandId::HomeOrpPumpSet : HmiCommandId::HomeOrpPumpToggle;
                    return true;
                case HOME_ACTION_PH_PUMP_TOGGLE:
                    out.command = HmiCommandId::HomePhPumpToggle;
                    return true;
                case HOME_ACTION_ORP_PUMP_TOGGLE:
                    out.command = HmiCommandId::HomeOrpPumpToggle;
                    return true;
                case HOME_ACTION_FILTRATION_TOGGLE:
                    out.command = HmiCommandId::HomeFiltrationToggle;
                    return true;
                case HOME_ACTION_AUTO_MODE_TOGGLE:
                    out.command = HmiCommandId::HomeAutoModeToggle;
                    return true;
                case HOME_ACTION_ORP_AUTO_MODE_TOGGLE:
                    out.command = HmiCommandId::HomeOrpAutoModeToggle;
                    return true;
                case HOME_ACTION_PH_AUTO_MODE_TOGGLE:
                    out.command = HmiCommandId::HomePhAutoModeToggle;
                    return true;
                case HOME_ACTION_WINTER_MODE_TOGGLE:
                    out.command = HmiCommandId::HomeWinterModeToggle;
                    return true;
                case HOME_ACTION_LIGHTS_TOGGLE:
                    out.command = HmiCommandId::HomeLightsToggle;
                    return true;
                case HOME_ACTION_ROBOT_TOGGLE:
                    out.command = HmiCommandId::HomeRobotToggle;
                    return true;
                case HOME_ACTION_DISPLAY_WIFI_FACTORY_RESET:
                    out.command = HmiCommandId::DisplayWifiFactoryReset;
                    return true;
                default:
                    return false;
            }

        default:
            return false;
    }
}

bool NextionDriver::handlePageId_(uint8_t pageId, bool emitEvents, HmiEvent& out)
{
    const bool wasKnown = currentPageKnown_;
    const uint8_t prevPage = currentPage_;
    const bool wasConfigPage = pageReady_;

    currentPageKnown_ = true;
    currentPage_ = pageId;
    pageReady_ = isMenuPageId_(pageId);

    if (!emitEvents) return false;
    const bool pageChanged = (!wasKnown || prevPage != pageId);
    if (pageReady_ && !wasConfigPage) {
        out.type = HmiEventType::ConfigEnter;
        out.pageId = pageId;
        return true;
    }
    if (!pageReady_ && wasConfigPage) {
        out.type = HmiEventType::ConfigExit;
        out.pageId = pageId;
        return true;
    }
    if (isHomePageId_(pageId) && pageChanged) {
        out.type = HmiEventType::Home;
        out.pageId = pageId;
        return true;
    }
    if (pageChanged) {
        out.type = HmiEventType::Page;
        out.pageId = pageId;
        return true;
    }
    return false;
}

bool NextionDriver::isHomePageId_(uint8_t pageId) const
{
    return pageId == cfg_.homePageId || pageId == cfg_.homePageAliasId;
}

bool NextionDriver::isConfigPageId_(uint8_t pageId) const
{
    return pageId == cfg_.configPageId || pageId == cfg_.configPageAliasId;
}

bool NextionDriver::isAlarmPageId_(uint8_t pageId) const
{
    return pageId == cfg_.alarmPageId || pageId == cfg_.alarmPageAliasId;
}

bool NextionDriver::isMenuPageId_(uint8_t pageId) const
{
    return isConfigPageId_(pageId) || isAlarmPageId_(pageId);
}

void NextionDriver::emitDebug_(const char* kind, const uint8_t* data, uint8_t len) const
{
    if (debugCallback_) debugCallback_(debugCtx_, kind, data, len);
}

bool NextionDriver::pollEvent(HmiEvent& out)
{
    out = HmiEvent{};
    if (!started_ || !cfg_.serial) return false;

    while (cfg_.serial->available() > 0) {
        const int rb = cfg_.serial->read();
        if (rb < 0) break;
        const uint8_t b = (uint8_t)rb;

        if (pageResponseActive_) {
            pageResponseBuf_[pageResponseLen_++] = b;
            if (pageResponseLen_ >= PageResponseBufSize) {
                const bool valid = pageResponseBuf_[1] == NEXTION_FF &&
                                   pageResponseBuf_[2] == NEXTION_FF &&
                                   pageResponseBuf_[3] == NEXTION_FF;
                const uint8_t pageId = pageResponseBuf_[0];
                pageResponseActive_ = false;
                pageResponseLen_ = 0U;
                if (valid && handlePageId_(pageId, true, out)) {
                    return true;
                }
                if (!valid) emitDebug_("page-drop", pageResponseBuf_, PageResponseBufSize);
            }
            continue;
        }

        if (touchResponseActive_) {
            touchResponseBuf_[touchResponseLen_++] = b;
            if (touchResponseLen_ >= TouchResponseBufSize) {
                const bool valid = touchResponseBuf_[3] == NEXTION_FF &&
                                   touchResponseBuf_[4] == NEXTION_FF &&
                                   touchResponseBuf_[5] == NEXTION_FF;
                touchResponseActive_ = false;
                touchResponseLen_ = 0U;
                if (valid) {
                    emitDebug_("touch", touchResponseBuf_, TouchResponseBufSize);
                } else {
                    emitDebug_("touch-drop", touchResponseBuf_, TouchResponseBufSize);
                }
            }
            continue;
        }

        if (customFrameActive_) {
            if (customExpectedLen_ == 0U) {
                if (b == 0U || b > CustomRxBufSize) {
                    emitDebug_("custom-len-drop", &b, 1U);
                    customFrameActive_ = false;
                    customExpectedLen_ = 0U;
                    customLen_ = 0U;
                    continue;
                }
                customExpectedLen_ = b;
                customLen_ = 0U;
                continue;
            }

            customBuf_[customLen_++] = b;
            if (customLen_ >= customExpectedLen_) {
                const uint8_t frameLen = customExpectedLen_;
                const bool parsed = parseCustomFrame_(customBuf_, customExpectedLen_, out);
                customFrameActive_ = false;
                customExpectedLen_ = 0U;
                customLen_ = 0U;
                if (parsed) return true;
                emitDebug_("custom-drop", customBuf_, frameLen);
            }
            continue;
        }

        if (b == NEXTION_CUSTOM_START) {
            customFrameActive_ = true;
            customExpectedLen_ = 0U;
            customLen_ = 0U;
            continue;
        }

        if (b == NEXTION_RSP_PAGE) {
            pageResponseActive_ = true;
            pageResponseLen_ = 0U;
            continue;
        }

        if (b == NEXTION_RSP_TOUCH) {
            touchResponseActive_ = true;
            touchResponseLen_ = 0U;
            continue;
        }

        if (b == NEXTION_RSP_SLEEP) {
            sleeping_ = true;
            out.type = HmiEventType::DisplaySleep;
            return true;
        }

        if (b == NEXTION_RSP_WAKE) {
            sleeping_ = false;
            out.type = HmiEventType::DisplayWake;
            return true;
        }
    }

    return false;
}
