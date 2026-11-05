#include "Modules/FlowConnectDisplay/FlowConnectDisplayUdpClientModule/FlowConnectDisplayUdpClientModule.h"

#include <stdio.h>
#include <string.h>

#include "Board/BoardSerialMap.h"
#include "Core/FirmwareVersion.h"

#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::FlowConnectDisplayUdpClientModule)
#include "Core/ModuleLog.h"

void FlowConnectDisplayUdpClientModule::init(ConfigStore& cfgStore, ServiceRegistry& services)
{
    cfgStore_ = &cfgStore;
    cfgStore.registerVar(tokenVar_);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);

    NextionDriverConfig cfg{};
    cfg.serial = &Board::SerialMap::hmiSerial();
    cfg.rxPin = Board::SerialMap::hmiRxPin();
    cfg.txPin = Board::SerialMap::hmiTxPin();
    cfg.baud = Board::SerialMap::hmiBaud();
    cfg.homePageId = 1U;
    cfg.configPageId = 2U;
    cfg.alarmPageId = 3U;
    cfg.homePageAliasId = 0U;
    cfg.configPageAliasId = 10U;
    cfg.alarmPageAliasId = 11U;
    nextion_.setConfig(cfg);
    nextion_.setDebugCallback(&FlowConnectDisplayUdpClientModule::nextionDebugCallback_, this);
}

void FlowConnectDisplayUdpClientModule::loop()
{
    tick(millis());
    vTaskDelay(pdMS_TO_TICKS(20));
}

bool FlowConnectDisplayUdpClientModule::begin()
{
    if (!nextion_.begin()) return false;
    if (!flowConnectInitialized_) {
        if (!nextion_.isSleeping()) {
            setFlowConnectionVisible_(false, "boot", true);
        }
        flowConnectInitialized_ = true;
    }
    if (started_) return true;
    probeNextionVersion_(millis(), true);
    if (!wifiConnected_()) return true;

    if (!udp_.begin(HMI_UDP_PORT)) {
        LOGW("FCD UDP begin failed port=%u", (unsigned)HMI_UDP_PORT);
        return false;
    }
    started_ = true;
    LOGD("FCD UDP client listening port=%u", (unsigned)HMI_UDP_PORT);
    return true;
}

void FlowConnectDisplayUdpClientModule::tick(uint32_t nowMs)
{
    serviceWifiFactoryReset_(nowMs);
    if (!begin()) return;
    nextion_.tick(nowMs);
    serviceWifiFactoryReset_(nowMs);

    if (!wifiConnected_()) {
        linked_ = false;
        if (!nextion_.isSleeping()) {
            setFlowConnectionVisible_(false, "wifi-offline");
        }
        return;
    }
    if (!started_) return;

    readUdp_(nowMs);
    servicePendingAck_(nowMs);
    serviceConfigRendering_(nowMs);
    if (inputLocked_ && (int32_t)(nowMs - inputLockedAtMs_) > (int32_t)InputLockMaxMs) {
        LOGW("FCD Nextion touch lock watchdog expired");
        configBatchActive_ = false;
        configRenderPending_ = false;
        configValuesPending_ = false;
        setInputLocked_(false, "watchdog", nowMs);
    }

    const bool nextionSleeping = nextion_.isSleeping();
    if (!linked_) {
        if (nextionSleeping) {
            pollNextion_();
            if (!nextion_.isSleeping()) {
                sendHello_(nowMs, true);
                return;
            }
            if ((uint32_t)(nowMs - lastHelloMs_) >= SleepHelloPeriodMs) {
                sendHello_(nowMs, true);
            }
        } else {
            sendHello_(nowMs);
        }
    } else {
        if (nextionSleeping) {
            pollNextion_();
            serviceEventTx_();
            sendPing_(nowMs, SleepPingPeriodMs);
            if ((uint32_t)(nowMs - lastSeenMs_) > SleepLinkTimeoutMs) {
                handleLinkLost_();
            }
            return;
        }
        const bool versionProbeSafe = !inputLocked_ &&
                                      !configPageActive_ &&
                                      !configRenderPending_ &&
                                      !configValuesPending_;
        if (versionProbeSafe && !nextion_.hasDisplayVersion()) {
            probeNextionVersion_(nowMs);
        }
        if (versionProbeSafe && (uint32_t)(nowMs - lastHelloMs_) >= VersionRecheckPeriodMs) {
            sendHello_(nowMs, true);
        }
        sendPing_(nowMs);
        pollNextion_();
        servicePageProbe_(nowMs);
        serviceEventTx_();
        if ((uint32_t)(nowMs - lastSeenMs_) > LinkTimeoutMs) {
            handleLinkLost_();
        }
    }
}

void FlowConnectDisplayUdpClientModule::sendHello_(uint32_t nowMs, bool force)
{
    if (!force && (uint32_t)(nowMs - lastHelloMs_) < HelloPeriodMs) return;
    lastHelloMs_ = nowMs;
    probeNextionVersion_(nowMs, force);

    HmiUdpHelloPayload payload{};
    payload.tokenCrc = hmiUdpTokenCrc(cfgData_.token);
    payload.displayFw = 1U;
    payload.protoVersion = HMI_UDP_VERSION;
    if (nextion_.hasDisplayVersion()) {
        strncpy(payload.nextionVersion, nextion_.displayVersion(), sizeof(payload.nextionVersion) - 1U);
        payload.nextionVersion[sizeof(payload.nextionVersion) - 1U] = '\0';
        payload.flags |= HMI_UDP_HELLO_FLAG_NEXTION_VERSION_VALID;
    }
    if (nextion_.isSleeping()) {
        payload.flags |= HMI_UDP_HELLO_FLAG_NEXTION_SLEEPING;
    }

    size_t packetLen = 0;
    if (!hmiUdpBuildPacket(txBuf_,
                           sizeof(txBuf_),
                           packetLen,
                           HmiUdpMsgType::Hello,
                           txSeq_++,
                           lastRxSeq_,
                           0U,
                           &payload,
                           sizeof(payload))) {
        return;
    }
    const IPAddress targetIp = linked_ ? flowIp_ : IPAddress(255, 255, 255, 255);
    const uint16_t targetPort = linked_ ? flowPort_ : HMI_UDP_PORT;
    if (!udp_.beginPacket(targetIp, targetPort)) return;
    (void)udp_.write(txBuf_, packetLen);
    (void)udp_.endPacket();
}

void FlowConnectDisplayUdpClientModule::probeNextionVersion_(uint32_t nowMs, bool force)
{
    if (nextion_.isSleeping()) return;
    const bool hadVersion = nextion_.hasDisplayVersion();
    char previousVersion[HMI_DISPLAY_VERSION_TEXT_MAX]{};
    if (hadVersion) {
        strncpy(previousVersion, nextion_.displayVersion(), sizeof(previousVersion) - 1U);
        previousVersion[sizeof(previousVersion) - 1U] = '\0';
    }
    const uint32_t period = hadVersion ? VersionRecheckPeriodMs : VersionProbeRetryMs;
    if (!force && (uint32_t)(nowMs - lastVersionProbeMs_) < period) return;
    lastVersionProbeMs_ = nowMs;

    if (nextion_.detectDisplayVersion(0U, force || hadVersion)) {
        const char* currentVersion = nextion_.displayVersion();
        if (!hadVersion || strcmp(currentVersion, previousVersion) != 0) {
            LOGI("FCD Nextion version=%s", currentVersion);
        }
    } else if (force) {
        LOGD("FCD Nextion version not available yet");
    }
}

void FlowConnectDisplayUdpClientModule::sendPing_(uint32_t nowMs, uint32_t periodMs)
{
    if ((uint32_t)(nowMs - lastPingMs_) < periodMs) return;
    lastPingMs_ = nowMs;
    (void)sendPacket_(HmiUdpMsgType::Ping, nullptr, 0U);
}

void FlowConnectDisplayUdpClientModule::readUdp_(uint32_t nowMs)
{
    int packetSize = udp_.parsePacket();
    while (packetSize > 0) {
        if (packetSize <= (int)sizeof(rxBuf_)) {
            const int len = udp_.read(rxBuf_, sizeof(rxBuf_));
            const HmiUdpHeader* header = nullptr;
            const uint8_t* payload = nullptr;
            if (len > 0 && hmiUdpValidatePacket(rxBuf_, (size_t)len, header, payload)) {
                flowIp_ = udp_.remoteIP();
                flowPort_ = udp_.remotePort();
                handlePacket_(*header, payload, nowMs);
            }
        } else {
            while (udp_.available() > 0) (void)udp_.read();
        }
        packetSize = udp_.parsePacket();
    }
}

void FlowConnectDisplayUdpClientModule::handlePacket_(const HmiUdpHeader& header, const uint8_t* payload, uint32_t nowMs)
{
    lastRxSeq_ = header.seq;
    markSeen_(nowMs);
    const HmiUdpMsgType msgType = (HmiUdpMsgType)header.type;
    const bool nextionSleeping = nextion_.isSleeping();
    LOGD("flow.io -> FCD msg=%s seq=%u ack=%u flags=0x%02X len=%u",
         msgTypeName_(msgType),
         (unsigned)header.seq,
         (unsigned)header.ack,
         (unsigned)header.flags,
         (unsigned)header.len);
    if ((header.flags & HMI_UDP_FLAG_ACK_REQUIRED) != 0U) {
        (void)sendAck_(header.seq);
    }
    if ((header.flags & HMI_UDP_FLAG_IS_ACK) != 0U || msgType == HmiUdpMsgType::Ack) {
        lastAck_ = header.ack;
        if (pendingLen_ > 0 && header.ack == pendingSeq_) {
            LOGD("FCD %s ACK received seq=%u attempts=%u",
                 msgTypeName_(pendingType_),
                 (unsigned)pendingSeq_,
                 (unsigned)pendingAttempts_);
            pendingLen_ = 0;
            pendingType_ = HmiUdpMsgType::Error;
            pendingAttempts_ = 0;
        }
        return;
    }

    if (nextionSleeping &&
        msgType != HmiUdpMsgType::Welcome &&
        msgType != HmiUdpMsgType::Pong &&
        msgType != HmiUdpMsgType::FullRefresh) {
        LOGD("FCD ignore %s while Nextion is sleeping", msgTypeName_(msgType));
        return;
    }

    switch (msgType) {
        case HmiUdpMsgType::Welcome: {
            if (header.len != sizeof(HmiUdpWelcomePayload) || !payload) return;
            const auto* welcome = reinterpret_cast<const HmiUdpWelcomePayload*>(payload);
            const bool wasLinked = linked_;
            linked_ = welcome->accepted != 0U;
            lostShown_ = false;
            if (linked_) {
                if (!nextionSleeping) {
                    setFlowConnectionVisible_(true, "welcome");
                    (void)nextion_.publishHomeText(HmiHomeTextField::ErrorMessage, "");
                }
                if (!wasLinked) {
                    LOGI("FCD linked to flow.io proto=%u fw=%u", (unsigned)welcome->protoVersion, (unsigned)welcome->flowFw);
                    if (!nextionSleeping) {
                        requestFullRefresh_("welcome");
                    }
                }
            } else {
                if (!nextionSleeping) {
                    setFlowConnectionVisible_(false, "welcome-rejected");
                }
            }
            break;
        }
        case HmiUdpMsgType::Pong:
            break;
        case HmiUdpMsgType::HomeText: {
            if (configPageActive_ || nextion_.isConfigPage()) {
                LOGD("FCD ignore HomeText while Config page is active");
                break;
            }
            if (header.len != sizeof(HmiUdpHomeTextPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpHomeTextPayload*>(payload);
            (void)nextion_.publishHomeText((HmiHomeTextField)p->field, p->text);
            break;
        }
        case HmiUdpMsgType::HomeGauge: {
            if (configPageActive_ || nextion_.isConfigPage()) {
                LOGD("FCD ignore HomeGauge while Config page is active");
                break;
            }
            if (header.len != sizeof(HmiUdpHomeGaugePayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpHomeGaugePayload*>(payload);
            (void)nextion_.publishHomeGaugePercent((HmiHomeGaugeField)p->field, p->percent);
            break;
        }
        case HmiUdpMsgType::HomeV2Needles: {
            if (configPageActive_ || nextion_.isConfigPage()) {
                LOGD("FCD ignore HomeV2Needles while Config page is active");
                break;
            }
            if (header.len != sizeof(HmiUdpHomeV2NeedlesPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpHomeV2NeedlesPayload*>(payload);
            NextionV2NeedlePublish publish{};
            publish.ph = (p->flags & HMI_UDP_V2_NEEDLE_PH) != 0U;
            publish.orp = (p->flags & HMI_UDP_V2_NEEDLE_ORP) != 0U;
            publish.psi = (p->flags & HMI_UDP_V2_NEEDLE_PSI) != 0U;
            publish.phNeedle = p->phNeedle;
            publish.orpNeedle = p->orpNeedle;
            publish.psiNeedle = p->psiNeedle;
            (void)nextion_.publishV2Needles(publish);
            break;
        }
        case HmiUdpMsgType::HomeStateBits: {
            if (configPageActive_ || nextion_.isConfigPage()) {
                LOGD("FCD ignore HomeStateBits while Config page is active");
                break;
            }
            if (header.len != sizeof(HmiUdpStateBitsPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpStateBitsPayload*>(payload);
            LOGD("FCD apply HomeStateBits stateBits=0x%08lX", (unsigned long)p->stateBits);
            (void)nextion_.publishHomeStateBits(p->stateBits);
            break;
        }
        case HmiUdpMsgType::HomeAlarmBits: {
            if (configPageActive_ || nextion_.isConfigPage()) {
                LOGD("FCD ignore HomeAlarmBits while Config page is active");
                break;
            }
            if (header.len != sizeof(HmiUdpAlarmBitsPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpAlarmBitsPayload*>(payload);
            (void)nextion_.publishHomeAlarmBits(p->alarmBits);
            break;
        }
        case HmiUdpMsgType::ConfigViewSnapshot: {
            if (header.len != sizeof(HmiUdpConfigViewSnapshotPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpConfigViewSnapshotPayload*>(payload);
            configView_ = ConfigMenuView{};
            configView_.pageIndex = p->page > 0U ? (uint8_t)(p->page - 1U) : 0U;
            configView_.pageCount = p->pageCount;
            configView_.rowCountOnPage = p->rowCount;
            configView_.contextRef = p->contextRef;
            configView_.canHome = (p->flags & HMI_UDP_CONFIG_VIEW_CAN_HOME) != 0U;
            configView_.canBack = (p->flags & HMI_UDP_CONFIG_VIEW_CAN_BACK) != 0U;
            configView_.canValidate = (p->flags & HMI_UDP_CONFIG_VIEW_CAN_VALIDATE) != 0U;
            configView_.isHome = (p->flags & HMI_UDP_CONFIG_VIEW_IS_HOME) != 0U;
            configView_.mode = p->mode == (uint8_t)ConfigMenuMode::Edit ? ConfigMenuMode::Edit : ConfigMenuMode::Browse;
            copyText_(configView_.breadcrumb, sizeof(configView_.breadcrumb), p->title);
            for (uint8_t i = 0; i < HMI_UDP_CONFIG_SNAPSHOT_ROWS; ++i) {
                const HmiUdpConfigRowPayload& src = p->rows[i];
                const uint8_t rowIndex = src.row < ConfigMenuModel::RowsPerPage ? src.row : i;
                if (rowIndex >= ConfigMenuModel::RowsPerPage) continue;
                ConfigMenuRowView& row = configView_.rows[rowIndex];
                row.visible = (src.flags & HMI_UDP_CONFIG_ROW_VISIBLE) != 0U;
                row.valueVisible = (src.flags & HMI_UDP_CONFIG_ROW_VALUE_VISIBLE) != 0U;
                row.editable = (src.flags & HMI_UDP_CONFIG_ROW_EDITABLE) != 0U;
                row.dirty = (src.flags & HMI_UDP_CONFIG_ROW_DIRTY) != 0U;
                row.canEnter = (src.flags & HMI_UDP_CONFIG_ROW_CAN_ENTER) != 0U;
                row.canEdit = (src.flags & HMI_UDP_CONFIG_ROW_CAN_EDIT) != 0U;
                row.widget = (ConfigMenuWidget)src.widget;
                row.editType = src.editType;
                copyText_(row.label, sizeof(row.label), src.label);
                copyText_(row.value, sizeof(row.value), src.value);
            }
            configPageActive_ = true;
            configBatchActive_ = false;
            configRenderPending_ = false;
            configValuesPending_ = false;
            setInputLocked_(true, "config-snapshot", nowMs);
            const bool loadingOk = nextion_.showConfigLoading(configView_.breadcrumb);
            LOGI("FCD config snapshot page=%u/%u ctxRef=%lu flags=0x%02X rows=%u mode=%s loading=%d",
                 (unsigned)p->page,
                 (unsigned)p->pageCount,
                 (unsigned long)p->contextRef,
                 (unsigned)p->flags,
                 (unsigned)p->rowCount,
                 menuModeName_(configView_.mode),
                 loadingOk ? 1 : 0);
            logDisplayState_("config-snapshot", true);
            scheduleConfigRender_(nowMs, "config-snapshot");
            break;
        }
        case HmiUdpMsgType::ConfigStart: {
            if (header.len != sizeof(HmiUdpConfigStartPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpConfigStartPayload*>(payload);
            configView_ = ConfigMenuView{};
            configView_.pageIndex = p->page > 0U ? (uint8_t)(p->page - 1U) : 0U;
            configView_.pageCount = p->pageCount;
            configView_.contextRef = p->contextRef;
            configView_.canHome = (p->flags & HMI_UDP_CONFIG_VIEW_CAN_HOME) != 0U;
            configView_.canBack = (p->flags & HMI_UDP_CONFIG_VIEW_CAN_BACK) != 0U;
            configView_.canValidate = (p->flags & HMI_UDP_CONFIG_VIEW_CAN_VALIDATE) != 0U;
            configView_.isHome = (p->flags & HMI_UDP_CONFIG_VIEW_IS_HOME) != 0U;
            copyText_(configView_.breadcrumb, sizeof(configView_.breadcrumb), p->title);
            configPageActive_ = true;
            configBatchActive_ = true;
            configRenderPending_ = false;
            configValuesPending_ = false;
            setInputLocked_(true, "config-batch", nowMs);
            const bool loadingOk = nextion_.showConfigLoading(configView_.breadcrumb);
            LOGD("FCD config batch start page=%u/%u ctxRef=%lu flags=0x%02X canBack=%u title='%s' loading=%d",
                 (unsigned)p->page,
                 (unsigned)p->pageCount,
                 (unsigned long)p->contextRef,
                 (unsigned)p->flags,
                 configView_.canBack ? 1U : 0U,
                 configView_.breadcrumb,
                 loadingOk ? 1 : 0);
            logDisplayState_("config-start", true);
            break;
        }
        case HmiUdpMsgType::ConfigRow: {
            if (header.len != sizeof(HmiUdpConfigRowPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpConfigRowPayload*>(payload);
            if (p->row >= ConfigMenuModel::RowsPerPage) return;
            ConfigMenuRowView& row = configView_.rows[p->row];
            row.visible = (p->flags & HMI_UDP_CONFIG_ROW_VISIBLE) != 0U;
            row.valueVisible = (p->flags & HMI_UDP_CONFIG_ROW_VALUE_VISIBLE) != 0U;
            row.editable = (p->flags & HMI_UDP_CONFIG_ROW_EDITABLE) != 0U;
            row.dirty = (p->flags & HMI_UDP_CONFIG_ROW_DIRTY) != 0U;
            row.canEnter = (p->flags & HMI_UDP_CONFIG_ROW_CAN_ENTER) != 0U;
            row.canEdit = (p->flags & HMI_UDP_CONFIG_ROW_CAN_EDIT) != 0U;
            configView_.mode = (p->flags & HMI_UDP_CONFIG_MODE_EDIT) != 0U ? ConfigMenuMode::Edit : ConfigMenuMode::Browse;
            row.widget = (ConfigMenuWidget)p->widget;
            row.editType = p->editType;
            copyText_(row.label, sizeof(row.label), p->label);
            copyText_(row.value, sizeof(row.value), p->value);
            LOGD("FCD config row row=%u widget=%s editType=%u flags=0x%02X label='%s' value='%s'",
                 (unsigned)p->row,
                 widgetName_(row.widget),
                 (unsigned)row.editType,
                 (unsigned)p->flags,
                 row.label,
                 row.value);
            if (!configBatchActive_) {
                scheduleConfigValuesRefresh_(nowMs, "row-update");
                logDisplayState_("config-row-update", true);
            }
            break;
        }
        case HmiUdpMsgType::ConfigEnd: {
            if (header.len != sizeof(HmiUdpConfigEndPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpConfigEndPayload*>(payload);
            configView_.rowCountOnPage = p->rowCount;
            configBatchActive_ = false;
            logDisplayState_("config-end", true);
            scheduleConfigRender_(nowMs, "config-end");
            break;
        }
        case HmiUdpMsgType::RtcWrite: {
            if (header.len != sizeof(HmiUdpRtcPayload) || !payload) return;
            HmiRtcDateTime rtc{};
            hmiUdpPayloadToRtc(*reinterpret_cast<const HmiUdpRtcPayload*>(payload), rtc);
            const bool ok = nextion_.writeRtc(rtc);
            LOGD("FCD apply RTC write ok=%d value=%u-%02u-%02u %02u:%02u:%02u",
                 ok ? 1 : 0,
                 (unsigned)rtc.year,
                 (unsigned)rtc.month,
                 (unsigned)rtc.day,
                 (unsigned)rtc.hour,
                 (unsigned)rtc.minute,
                 (unsigned)rtc.second);
            break;
        }
        case HmiUdpMsgType::RtcReadRequest:
            (void)sendRtcReadResponse_();
            break;
        case HmiUdpMsgType::FullRefresh:
            break;
        default:
            break;
    }
}

bool FlowConnectDisplayUdpClientModule::sendPacket_(HmiUdpMsgType type, const void* payload, uint8_t payloadLen, uint8_t flags)
{
    if (!started_ || !wifiConnected_()) return false;
    size_t packetLen = 0;
    if (!hmiUdpBuildPacket(txBuf_, sizeof(txBuf_), packetLen, type, txSeq_++, lastRxSeq_, flags, payload, payloadLen)) {
        return false;
    }
    if (!udp_.beginPacket(flowIp_, flowPort_)) return false;
    const size_t written = udp_.write(txBuf_, packetLen);
    return written == packetLen && udp_.endPacket() == 1;
}

bool FlowConnectDisplayUdpClientModule::sendAck_(uint16_t seq)
{
    size_t packetLen = 0;
    if (!hmiUdpBuildPacket(txBuf_,
                           sizeof(txBuf_),
                           packetLen,
                           HmiUdpMsgType::Ack,
                           txSeq_++,
                           seq,
                           HMI_UDP_FLAG_IS_ACK,
                           nullptr,
                           0U)) {
        return false;
    }
    if (!udp_.beginPacket(flowIp_, flowPort_)) return false;
    const size_t written = udp_.write(txBuf_, packetLen);
    return written == packetLen && udp_.endPacket() == 1;
}

void FlowConnectDisplayUdpClientModule::pollNextion_()
{
    uint8_t drained = 0;
    HmiEvent event{};
    while (drained < 4U && nextion_.pollEvent(event)) {
        logEvent_("Nextion -> FCD", event);
        if (event.type == HmiEventType::DisplaySleep) {
            configPageActive_ = false;
            configBatchActive_ = false;
            configRenderPending_ = false;
            configValuesPending_ = false;
            pendingLen_ = 0;
            pendingAttempts_ = 0;
            inputLocked_ = false;
            if (linked_) {
                (void)enqueueEvent_(event);
            }
            ++drained;
            continue;
        }
        if (event.type == HmiEventType::DisplayWake) {
            lastHomeRefreshRequestMs_ = 0;
            setFlowConnectionVisible_(linked_, "nextion-wake", true);
            if (linked_) {
                requestFullRefresh_("nextion-wake", true);
            }
            if (linked_) {
                (void)enqueueEvent_(event);
            }
            ++drained;
            continue;
        }
        if (handleLocalCommand_(event)) {
            ++drained;
            continue;
        }
        const bool menuEvent = isMenuEvent_(event.type);
        if (menuEvent && event.contextRef == 0U && configView_.contextRef != 0U) {
            event.contextRef = configView_.contextRef;
        }

        const bool actualHomePage = event.type == HmiEventType::Home && nextion_.isHomePage();
        if (actualHomePage) {
            configPageActive_ = false;
            uint8_t page = 0;
            if (nextion_.currentPage(page)) lastFlowConnectPage_ = page;
            setFlowConnectionVisible_(linked_, "nextion-home", true);
        } else if (event.type == HmiEventType::ConfigEnter) {
            configPageActive_ = true;
        } else if (event.type == HmiEventType::ConfigExit) {
            configPageActive_ = false;
        }
        if (actualHomePage ||
            event.type == HmiEventType::ConfigExit ||
            (event.type == HmiEventType::Command && event.command == HmiCommandId::HomeSyncRequest)) {
            requestFullRefresh_("nextion-home", true);
        }
        if (inputLocked_) {
            LOGI("FCD drops Nextion event while input locked event=%s command=%s",
                 eventTypeName_(event.type),
                 commandName_(event.command));
            ++drained;
            continue;
        }
        (void)enqueueEvent_(event);
        ++drained;
    }
}

void FlowConnectDisplayUdpClientModule::serviceEventTx_()
{
    if (pendingLen_ > 0) return;
    HmiEvent event{};
    if (!peekEvent_(event)) return;
    if (sendEvent_(event)) {
        popEvent_();
    }
}

bool FlowConnectDisplayUdpClientModule::enqueueEvent_(const HmiEvent& event)
{
    const uint8_t next = (uint8_t)((eventHead_ + 1U) % NEXTION_EVENT_QUEUE_SIZE);
    if (next == eventTail_) {
        LOGW("FCD Nextion event queue full drop type=%s command=%s",
             eventTypeName_(event.type),
             commandName_(event.command));
        return false;
    }
    eventQueue_[eventHead_] = event;
    eventHead_ = next;
    return true;
}

bool FlowConnectDisplayUdpClientModule::dequeueEvent_(HmiEvent& out)
{
    out = HmiEvent{};
    if (eventHead_ == eventTail_) return false;
    out = eventQueue_[eventTail_];
    eventTail_ = (uint8_t)((eventTail_ + 1U) % NEXTION_EVENT_QUEUE_SIZE);
    return true;
}

bool FlowConnectDisplayUdpClientModule::peekEvent_(HmiEvent& out) const
{
    out = HmiEvent{};
    if (eventHead_ == eventTail_) return false;
    out = eventQueue_[eventTail_];
    return true;
}

void FlowConnectDisplayUdpClientModule::popEvent_()
{
    if (eventHead_ == eventTail_) return;
    eventTail_ = (uint8_t)((eventTail_ + 1U) % NEXTION_EVENT_QUEUE_SIZE);
}

bool FlowConnectDisplayUdpClientModule::sendEvent_(const HmiEvent& event)
{
    HmiUdpEventPayload payload{};
    hmiUdpEventToPayload(event, payload);
    const bool ok = queueReliablePacket_(HmiUdpMsgType::HmiEvent, &payload, sizeof(payload));
    if (ok) {
        logEvent_("FCD -> flow.io", event);
    } else {
        LOGW("FCD failed to queue event for flow.io event=%s command=%s row=%u pending=%s seq=%u",
             eventTypeName_(event.type),
             commandName_(event.command),
             (unsigned)event.row,
             pendingLen_ > 0U ? msgTypeName_(pendingType_) : "none",
             (unsigned)pendingSeq_);
    }
    return ok;
}

bool FlowConnectDisplayUdpClientModule::queueReliablePacket_(HmiUdpMsgType type, const void* payload, uint8_t payloadLen)
{
    if (pendingLen_ > 0) {
        LOGW("FCD reliable TX busy drop refused type=%s pending=%s seq=%u",
             msgTypeName_(type),
             msgTypeName_(pendingType_),
             (unsigned)pendingSeq_);
        return false;
    }
    pendingSeq_ = txSeq_;
    pendingType_ = type;
    size_t packetLen = 0;
    if (!hmiUdpBuildPacket(pendingBuf_,
                           sizeof(pendingBuf_),
                           packetLen,
                           type,
                           txSeq_++,
                           lastRxSeq_,
                           HMI_UDP_FLAG_ACK_REQUIRED,
                           payload,
                           payloadLen)) {
        pendingSeq_ = 0;
        pendingType_ = HmiUdpMsgType::Error;
        return false;
    }
    LOGD("FCD queued %s seq=%u len=%u",
         msgTypeName_(type),
         (unsigned)pendingSeq_,
         (unsigned)packetLen);
    pendingLen_ = packetLen;
    pendingAttempts_ = 0;
    pendingLastSendMs_ = 0;
    servicePendingAck_(millis());
    return pendingLen_ > 0;
}

bool FlowConnectDisplayUdpClientModule::sendRtcReadResponse_()
{
    HmiRtcDateTime rtc{};
    if (!nextion_.readRtc(rtc, 180U)) {
        LOGW("FCD Nextion RTC read failed");
        return false;
    }

    HmiUdpRtcPayload payload{};
    hmiUdpRtcToPayload(rtc, payload);
    const bool ok = queueReliablePacket_(HmiUdpMsgType::RtcReadResponse, &payload, sizeof(payload));
    LOGD("FCD RTC response queued ok=%d value=%u-%02u-%02u %02u:%02u:%02u",
         ok ? 1 : 0,
         (unsigned)rtc.year,
         (unsigned)rtc.month,
         (unsigned)rtc.day,
         (unsigned)rtc.hour,
         (unsigned)rtc.minute,
         (unsigned)rtc.second);
    return ok;
}

bool FlowConnectDisplayUdpClientModule::handleLocalCommand_(const HmiEvent& event)
{
    if (event.type != HmiEventType::Command) return false;
    if (event.command != HmiCommandId::DisplayWifiFactoryReset) return false;

    LOGW("FCD local Nextion command: WiFi factory reset requested");
    (void)requestWifiFactoryReset_();
    return true;
}

bool FlowConnectDisplayUdpClientModule::requestWifiFactoryReset_()
{
    if (wifiFactoryResetPending_) return true;
    if (!cfgStore_) {
        LOGE("FCD WiFi factory reset failed: ConfigStore unavailable");
        (void)nextion_.publishHomeText(HmiHomeTextField::ErrorMessage, "WiFi reset failed");
        return false;
    }

    const bool patchOk = cfgStore_->applyJson("{\"wifi\":{\"enabled\":true,\"ssid\":\"\",\"pass\":\"\"}}");

    WiFi.mode(WIFI_MODE_STA);
    delay(20);
    (void)WiFi.disconnect(false, true);
    esp_err_t restoreErr = esp_wifi_restore();
    if (restoreErr == ESP_ERR_WIFI_NOT_INIT) {
        WiFi.mode(WIFI_MODE_STA);
        delay(20);
        restoreErr = esp_wifi_restore();
    }
    (void)WiFi.disconnect(true, true);

    if (!patchOk) {
        LOGE("FCD WiFi factory reset failed patch=%d wifi_err=%d",
             patchOk ? 1 : 0,
             (int)restoreErr);
        (void)nextion_.publishHomeText(HmiHomeTextField::ErrorMessage, "WiFi reset failed");
        return false;
    }

    linked_ = false;
    started_ = false;
    pendingLen_ = 0;
    pendingAttempts_ = 0;
    wifiFactoryResetPending_ = true;
    wifiFactoryResetAtMs_ = millis() + 900U;
    (void)nextion_.publishHomeText(HmiHomeTextField::ErrorMessage, "WiFi reset...");
    setFlowConnectionVisible_(false, "wifi-factory-reset", true);
    LOGW("FCD WiFi factory reset done patch=%d wifi_err=%d reboot_ms=900",
         patchOk ? 1 : 0,
         (int)restoreErr);
    return true;
}

void FlowConnectDisplayUdpClientModule::serviceWifiFactoryReset_(uint32_t nowMs)
{
    if (!wifiFactoryResetPending_) return;
    if ((int32_t)(nowMs - wifiFactoryResetAtMs_) < 0) return;
    LOGW("FCD reboot after WiFi factory reset");
    delay(20);
    esp_restart();
}

void FlowConnectDisplayUdpClientModule::requestFullRefresh_(const char* reason, bool force)
{
    if (!linked_) return;
    const uint32_t now = millis();
    if (!force &&
        lastHomeRefreshRequestMs_ != 0U &&
        (uint32_t)(now - lastHomeRefreshRequestMs_) < HomeRefreshThrottleMs) {
        return;
    }
    lastHomeRefreshRequestMs_ = now;
    const bool ok = sendPacket_(HmiUdpMsgType::FullRefresh, nullptr, 0U);
    LOGD("FCD requested flow.io full refresh reason=%s ok=%d",
         reason ? reason : "unknown",
         ok ? 1 : 0);
}

void FlowConnectDisplayUdpClientModule::servicePageProbe_(uint32_t nowMs)
{
    if ((uint32_t)(nowMs - lastPageProbeMs_) >= PageProbePeriodMs) {
        lastPageProbeMs_ = nowMs;
        const bool ok = nextion_.requestPageReport();
        if (!ok) {
            LOGW("FCD Nextion page probe send failed");
        }
    }

    uint8_t page = 0;
    if (nextion_.currentPage(page)) {
        logDisplayState_("page-probe");
        const bool pageChanged = page != lastFlowConnectPage_;
        if (pageChanged) {
            lastFlowConnectPage_ = page;
        }
        if (nextion_.isConfigPage()) {
            configPageActive_ = true;
        } else if (nextion_.isHomePage()) {
            configPageActive_ = false;
        }
        if (pageChanged && nextion_.isHomePage()) {
            setFlowConnectionVisible_(linked_, "home-page-probe", true);
            requestFullRefresh_("home-page-probe");
        }
    }
}

void FlowConnectDisplayUdpClientModule::scheduleConfigRender_(uint32_t nowMs, const char* reason)
{
    setInputLocked_(true, reason, nowMs);
    configRenderPending_ = true;
    configRenderRemaining_ = ConfigRenderPasses;
    configRenderDueMs_ = nowMs + ConfigRenderDelayMs;
    LOGD("FCD config render scheduled reason=%s rows=%u passes=%u",
         reason ? reason : "unknown",
         (unsigned)configView_.rowCountOnPage,
         (unsigned)configRenderRemaining_);
    logDisplayState_("config-render-scheduled", true);
}

void FlowConnectDisplayUdpClientModule::scheduleConfigValuesRefresh_(uint32_t nowMs, const char* reason)
{
    configValuesPending_ = true;
    configValuesRemaining_ = ConfigValuesPasses;
    configValuesDueMs_ = nowMs + ConfigValuesDelayMs;
    LOGD("FCD config values refresh scheduled reason=%s passes=%u",
         reason ? reason : "unknown",
         (unsigned)configValuesRemaining_);
}

void FlowConnectDisplayUdpClientModule::serviceConfigRendering_(uint32_t nowMs)
{
    if (configRenderPending_ && (int32_t)(nowMs - configRenderDueMs_) >= 0) {
        const bool ok = nextion_.renderConfigMenu(configView_);
        LOGD("FCD config render pass ok=%d remaining=%u page=%u rows=%u",
             ok ? 1 : 0,
             (unsigned)configRenderRemaining_,
             (unsigned)(configView_.pageIndex + 1U),
             (unsigned)configView_.rowCountOnPage);
        logDisplayState_("config-render-pass");
        if (configRenderRemaining_ > 0U) {
            --configRenderRemaining_;
        }
        if (configRenderRemaining_ == 0U) {
            configRenderPending_ = false;
            setInputLocked_(false, "config-render-complete", nowMs);
            LOGI("FCD config render complete page=%u rows=%u",
                 (unsigned)(configView_.pageIndex + 1U),
                 (unsigned)configView_.rowCountOnPage);
        } else {
            configRenderDueMs_ = nowMs + ConfigRenderRetryMs;
        }
    }

    if (configValuesPending_ && (int32_t)(nowMs - configValuesDueMs_) >= 0) {
        const bool ok = nextion_.refreshConfigMenuValues(configView_);
        LOGD("FCD config values refresh pass ok=%d remaining=%u",
             ok ? 1 : 0,
             (unsigned)configValuesRemaining_);
        if (configValuesRemaining_ > 0U) {
            --configValuesRemaining_;
        }
        if (configValuesRemaining_ == 0U) {
            configValuesPending_ = false;
        } else {
            configValuesDueMs_ = nowMs + ConfigRenderRetryMs;
        }
    }
}

void FlowConnectDisplayUdpClientModule::setInputLocked_(bool locked, const char* reason, uint32_t nowMs)
{
    if (inputLocked_ == locked) {
        if (locked) {
            inputLockedAtMs_ = nowMs;
        }
        return;
    }
    inputLocked_ = locked;
    if (locked) {
        inputLockedAtMs_ = nowMs;
    }
    const bool ok = nextion_.setTouchEnabled(!locked);
    LOGD("FCD Nextion touch %s reason=%s ok=%d",
         locked ? "locked" : "unlocked",
         reason ? reason : "unknown",
         ok ? 1 : 0);
    logDisplayState_(reason ? reason : "touch-lock", true);
}

void FlowConnectDisplayUdpClientModule::logDisplayState_(const char* reason, bool force)
{
    uint8_t nextionPage = 0xFFU;
    bool pageKnown = nextion_.currentPage(nextionPage);
    uint8_t logicalPage = 0xFFU;
    if (pageKnown) {
        if (nextion_.isHomePage()) {
            logicalPage = 0U;
        } else if (nextion_.isConfigPage()) {
            logicalPage = 1U;
        }
    }

    const uint8_t configPage = (uint8_t)(configView_.pageIndex + 1U);
    const bool changed =
        force ||
        nextionPage != lastLoggedNextionPage_ ||
        logicalPage != lastLoggedLogicalPage_ ||
        configPage != lastLoggedConfigPage_ ||
        configView_.pageCount != lastLoggedConfigPageCount_ ||
        configView_.rowCountOnPage != lastLoggedConfigRows_ ||
        configView_.mode != lastLoggedConfigMode_ ||
        strncmp(configView_.breadcrumb, lastLoggedConfigPath_, sizeof(lastLoggedConfigPath_)) != 0;
    if (!changed) return;

    lastLoggedNextionPage_ = nextionPage;
    lastLoggedLogicalPage_ = logicalPage;
    lastLoggedConfigPage_ = configPage;
    lastLoggedConfigPageCount_ = configView_.pageCount;
    lastLoggedConfigRows_ = configView_.rowCountOnPage;
    lastLoggedConfigMode_ = configView_.mode;
    copyText_(lastLoggedConfigPath_, sizeof(lastLoggedConfigPath_), configView_.breadcrumb);

    LOGD("FCD state reason=%s nextionPage=%s%u logicalPage=%u(%s) configPage=%u/%u mode=%s rows=%u submenu='%s' inputLocked=%u batch=%u renderPending=%u valuesPending=%u",
         reason ? reason : "state",
         pageKnown ? "" : "?",
         (unsigned)nextionPage,
         (unsigned)logicalPage,
         logicalPage == 0U ? "Home" : (logicalPage == 1U ? "Config" : "Other"),
         (unsigned)configPage,
         (unsigned)configView_.pageCount,
         menuModeName_(configView_.mode),
         (unsigned)configView_.rowCountOnPage,
         configView_.breadcrumb,
         inputLocked_ ? 1U : 0U,
         configBatchActive_ ? 1U : 0U,
         configRenderPending_ ? 1U : 0U,
         configValuesPending_ ? 1U : 0U);
}

void FlowConnectDisplayUdpClientModule::logEvent_(const char* prefix, const HmiEvent& event) const
{
    if (isMenuEvent_(event.type)) {
        LOGI("%s event=%s command=%s ctxRef=%lu row=%u value=%u dir=%d slider=%.2f text='%s'",
             prefix ? prefix : "HMI",
             eventTypeName_(event.type),
             commandName_(event.command),
             (unsigned long)event.contextRef,
             (unsigned)event.row,
             (unsigned)event.value,
             (int)event.direction,
             (double)event.sliderValue,
             event.text);
        return;
    }
    LOGD("%s event=%s command=%s ctxRef=%lu row=%u value=%u dir=%d slider=%.2f text='%s'",
         prefix ? prefix : "HMI",
         eventTypeName_(event.type),
         commandName_(event.command),
         (unsigned long)event.contextRef,
         (unsigned)event.row,
         (unsigned)event.value,
         (int)event.direction,
         (double)event.sliderValue,
         event.text);
}

bool FlowConnectDisplayUdpClientModule::isMenuEvent_(HmiEventType type)
{
    return type == HmiEventType::Back ||
           type == HmiEventType::Validate ||
           type == HmiEventType::NextPage ||
           type == HmiEventType::PrevPage ||
           type == HmiEventType::RowActivate ||
           type == HmiEventType::RowToggle ||
           type == HmiEventType::RowCycle ||
           type == HmiEventType::RowSetText ||
           type == HmiEventType::RowSetSlider ||
           type == HmiEventType::RowEdit ||
           type == HmiEventType::ConfigEnter ||
           type == HmiEventType::ConfigExit;
}

void FlowConnectDisplayUdpClientModule::servicePendingAck_(uint32_t nowMs)
{
    if (pendingLen_ == 0 || !linked_) return;
    if (pendingAttempts_ >= AckMaxAttempts) {
        LOGW("FCD %s ACK timeout seq=%u attempts=%u; dropping packet and resuming",
             msgTypeName_(pendingType_),
             (unsigned)pendingSeq_,
             (unsigned)pendingAttempts_);
        pendingLen_ = 0;
        pendingType_ = HmiUdpMsgType::Error;
        pendingAttempts_ = 0;
        return;
    }
    if (pendingAttempts_ > 0 && (uint32_t)(nowMs - pendingLastSendMs_) < AckRetryMs) return;
    if (!udp_.beginPacket(flowIp_, flowPort_)) return;
    const size_t written = udp_.write(pendingBuf_, pendingLen_);
    if (written == pendingLen_ && udp_.endPacket() == 1) {
        ++pendingAttempts_;
        pendingLastSendMs_ = nowMs;
        LOGD("FCD sent %s seq=%u attempt=%u len=%u",
             msgTypeName_(pendingType_),
             (unsigned)pendingSeq_,
             (unsigned)pendingAttempts_,
             (unsigned)pendingLen_);
    }
}

void FlowConnectDisplayUdpClientModule::markSeen_(uint32_t nowMs)
{
    lastSeenMs_ = nowMs;
}

void FlowConnectDisplayUdpClientModule::setFlowConnectionVisible_(bool visible, const char* reason, bool force)
{
    if (!force && flowConnectVisible_ == visible) return;
    const bool ok = nextion_.setObjectVisible(FlowConnectionStateObject, visible);
    if (ok) flowConnectVisible_ = visible;
    LOGD("FCD flow.io connection indicator visible=%u reason=%s ok=%d",
         visible ? 1U : 0U,
         reason ? reason : "state",
         ok ? 1 : 0);
}

void FlowConnectDisplayUdpClientModule::handleLinkLost_()
{
    linked_ = false;
    pendingLen_ = 0;
    pendingType_ = HmiUdpMsgType::Error;
    eventHead_ = 0;
    eventTail_ = 0;
    configPageActive_ = false;
    if (!nextion_.isSleeping()) {
        setFlowConnectionVisible_(false, "link-lost");
        setInputLocked_(false, "link-lost", millis());
    } else {
        inputLocked_ = false;
    }
    if (!lostShown_) {
        lostShown_ = true;
        if (!nextion_.isSleeping()) {
            (void)nextion_.publishHomeText(HmiHomeTextField::ErrorMessage, "Connexion flow.io perdue");
        }
        LOGI("FCD lost flow.io link");
    }
}

bool FlowConnectDisplayUdpClientModule::wifiConnected_() const
{
    return wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx);
}

void FlowConnectDisplayUdpClientModule::copyText_(char* out, size_t outLen, const char* in)
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!in) return;
    size_t i = 0;
    while (i + 1 < outLen && in[i] != '\0') {
        out[i] = in[i];
        ++i;
    }
    out[i] = '\0';
}

void FlowConnectDisplayUdpClientModule::nextionDebugCallback_(void*, const char* kind, const uint8_t* data, uint8_t len)
{
    char hex[3U * 64U + 1U]{};
    size_t pos = 0;
    for (uint8_t i = 0; i < len && pos + 3U < sizeof(hex); ++i) {
        const int written = snprintf(hex + pos, sizeof(hex) - pos, "%s%02X", i == 0 ? "" : " ", (unsigned)data[i]);
        if (written <= 0) break;
        pos += (size_t)written;
    }

    if (strcmp(kind ? kind : "", "touch") == 0 && len >= 6U) {
        LOGD("Nextion native touch page=%u component=%u state=%u raw=%s",
             (unsigned)data[0],
             (unsigned)data[1],
             (unsigned)data[2],
             hex);
        return;
    }

    LOGW("Nextion raw %s len=%u data=%s",
         kind ? kind : "?",
         (unsigned)len,
         hex);
}

const char* FlowConnectDisplayUdpClientModule::msgTypeName_(HmiUdpMsgType type)
{
    switch (type) {
        case HmiUdpMsgType::Hello: return "Hello";
        case HmiUdpMsgType::Welcome: return "Welcome";
        case HmiUdpMsgType::Ping: return "Ping";
        case HmiUdpMsgType::Pong: return "Pong";
        case HmiUdpMsgType::Ack: return "Ack";
        case HmiUdpMsgType::HomeText: return "HomeText";
        case HmiUdpMsgType::HomeGauge: return "HomeGauge";
        case HmiUdpMsgType::HomeStateBits: return "HomeStateBits";
        case HmiUdpMsgType::HomeAlarmBits: return "HomeAlarmBits";
        case HmiUdpMsgType::FullRefresh: return "FullRefresh";
        case HmiUdpMsgType::HomeV2Needles: return "HomeV2Needles";
        case HmiUdpMsgType::HmiEvent: return "HmiEvent";
        case HmiUdpMsgType::ConfigStart: return "ConfigStart";
        case HmiUdpMsgType::ConfigRow: return "ConfigRow";
        case HmiUdpMsgType::ConfigEnd: return "ConfigEnd";
        case HmiUdpMsgType::ConfigValues: return "ConfigValues";
        case HmiUdpMsgType::ConfigViewSnapshot: return "ConfigViewSnapshot";
        case HmiUdpMsgType::RtcReadRequest: return "RtcReadRequest";
        case HmiUdpMsgType::RtcReadResponse: return "RtcReadResponse";
        case HmiUdpMsgType::RtcWrite: return "RtcWrite";
        case HmiUdpMsgType::Error: return "Error";
        default: return "?";
    }
}

const char* FlowConnectDisplayUdpClientModule::eventTypeName_(HmiEventType type)
{
    switch (type) {
        case HmiEventType::None: return "None";
        case HmiEventType::Home: return "Home";
        case HmiEventType::Back: return "Back";
        case HmiEventType::Validate: return "Validate";
        case HmiEventType::NextPage: return "NextPage";
        case HmiEventType::PrevPage: return "PrevPage";
        case HmiEventType::RowActivate: return "RowActivate";
        case HmiEventType::RowToggle: return "RowToggle";
        case HmiEventType::RowCycle: return "RowCycle";
        case HmiEventType::RowSetText: return "RowSetText";
        case HmiEventType::RowSetSlider: return "RowSetSlider";
        case HmiEventType::RowEdit: return "RowEdit";
        case HmiEventType::Command: return "Command";
        case HmiEventType::Page: return "Page";
        case HmiEventType::ConfigEnter: return "ConfigEnter";
        case HmiEventType::ConfigExit: return "ConfigExit";
        case HmiEventType::DisplaySleep: return "DisplaySleep";
        case HmiEventType::DisplayWake: return "DisplayWake";
        default: return "?";
    }
}

const char* FlowConnectDisplayUdpClientModule::commandName_(HmiCommandId command)
{
    switch (command) {
        case HmiCommandId::None: return "None";
        case HmiCommandId::HomeFiltrationSet: return "HomeFiltrationSet";
        case HmiCommandId::HomeAutoModeSet: return "HomeAutoModeSet";
        case HmiCommandId::HomeSyncRequest: return "HomeSyncRequest";
        case HmiCommandId::HomeFiltrationToggle: return "HomeFiltrationToggle";
        case HmiCommandId::HomeAutoModeToggle: return "HomeAutoModeToggle";
        case HmiCommandId::HomeOrpAutoModeToggle: return "HomeOrpAutoModeToggle";
        case HmiCommandId::HomePhAutoModeToggle: return "HomePhAutoModeToggle";
        case HmiCommandId::HomeWinterModeToggle: return "HomeWinterModeToggle";
        case HmiCommandId::HomeLightsToggle: return "HomeLightsToggle";
        case HmiCommandId::HomeRobotToggle: return "HomeRobotToggle";
        case HmiCommandId::HomeConfigOpen: return "HomeConfigOpen";
        case HmiCommandId::HomePhPumpSet: return "HomePhPumpSet";
        case HmiCommandId::HomeOrpPumpSet: return "HomeOrpPumpSet";
        case HmiCommandId::HomePhPumpToggle: return "HomePhPumpToggle";
        case HmiCommandId::HomeOrpPumpToggle: return "HomeOrpPumpToggle";
        case HmiCommandId::DisplayWifiFactoryReset: return "DisplayWifiFactoryReset";
        default: return "?";
    }
}

const char* FlowConnectDisplayUdpClientModule::menuModeName_(ConfigMenuMode mode)
{
    switch (mode) {
        case ConfigMenuMode::Browse: return "Browse";
        case ConfigMenuMode::Edit: return "Edit";
        default: return "?";
    }
}

const char* FlowConnectDisplayUdpClientModule::widgetName_(ConfigMenuWidget widget)
{
    switch (widget) {
        case ConfigMenuWidget::Text: return "Text";
        case ConfigMenuWidget::Switch: return "Switch";
        case ConfigMenuWidget::Select: return "Select";
        case ConfigMenuWidget::Slider: return "Slider";
        default: return "?";
    }
}
