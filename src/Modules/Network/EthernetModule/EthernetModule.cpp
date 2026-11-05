#include "Modules/Network/EthernetModule/EthernetModule.h"

#include "Core/EventBus/EventPayloads.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::EthernetModule)
#include "Core/ModuleLog.h"
#include "Board/BoardSpec.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ctype.h>
#include <esp_err.h>
#include <esp_mac.h>
#include <esp_netif_ip_addr.h>
#include <string.h>

namespace {
const char* stateName(EthernetState s)
{
    switch (s) {
        case EthernetState::Disabled: return "Disabled";
        case EthernetState::Starting: return "Starting";
        case EthernetState::WaitingIp: return "WaitingIp";
        case EthernetState::Connected: return "Connected";
        case EthernetState::ErrorWait: return "ErrorWait";
        default: return "Unknown";
    }
}

EthernetModule* gEthernetInstance = nullptr;

uint8_t toSpiFreqMhz_(uint32_t hz)
{
    if (hz == 0U) return 8U;
    uint32_t mhz = (hz + 500000U) / 1000000U;
    if (mhz < 1U) mhz = 1U;
    if (mhz > 80U) mhz = 80U;
    return (uint8_t)mhz;
}

bool isBlank_(const char* text, size_t maxLen)
{
    if (!text) return true;
    const size_t len = strnlen(text, maxLen);
    if (len == 0U || len >= maxLen) return true;
    for (size_t i = 0; i < len; ++i) {
        if (!isspace((unsigned char)text[i])) return false;
    }
    return true;
}

void formatHostname_(const char* deviceName, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;

    size_t w = 0U;
    if (deviceName) {
        for (size_t i = 0U; deviceName[i] != '\0' && w < (outLen - 1U); ++i) {
            const char c = deviceName[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
                out[w++] = (char)tolower((unsigned char)c);
            } else if (c == ' ' || c == '_' || c == '.') {
                out[w++] = '-';
            }
        }
    }
    out[w] = '\0';

    while (w > 0U && out[0] == '-') {
        memmove(out, out + 1, w);
        --w;
    }
    while (w > 0U && out[w - 1U] == '-') {
        out[--w] = '\0';
    }

    if (out[0] == '\0') {
        snprintf(out, outLen, "flowio");
    }
}

void formatDhcpHostname_(const char* deviceName, char* out, size_t outLen)
{
    formatHostname_(deviceName, out, outLen);
    if (!out || outLen == 0U || strcmp(out, "flowio") != 0) return;

    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return;
    snprintf(out,
             outLen,
             "FlowIO-%02X%02X%02X",
             (unsigned)mac[3],
             (unsigned)mac[4],
             (unsigned)mac[5]);
}
}  // namespace

EthernetModule::EthernetModule(const BoardSpec& board)
{
    const EthernetW5500Spec* eth = boardEthernetW5500(board);
    if (eth) {
        ethCfg_ = *eth;
        hasEthPins_ = ethCfg_.enabled &&
                      ethCfg_.mosiPin >= 0 &&
                      ethCfg_.misoPin >= 0 &&
                      ethCfg_.sclkPin >= 0 &&
                      ethCfg_.csPin >= 0 &&
                      ethCfg_.intPin >= 0 &&
                      ethCfg_.rstPin >= 0;
        spiFreqMhz_ = toSpiFreqMhz_(ethCfg_.spiClockHz);
    }
}

void EthernetModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Ethernet;
    constexpr uint8_t kCfgBranchId = 1U;
    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);

    cfgStore_ = &cfg;
    services_ = &services;
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    const EventBusService* eventBusSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = eventBusSvc ? eventBusSvc->bus : nullptr;
    if (eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &EthernetModule::onEventStatic_, this);
    }

    gEthernetInstance = this;
    cleanupDriver_();
    setState_(EthernetState::Disabled);
    resetRuntimeState_();
}

void EthernetModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    loadSystemDeviceName_();
#if ENABLE_ETHERNET
    const bool ethernetEnabled = cfgData_.enabled;
#else
    const bool ethernetEnabled = false;
#endif
    if (ethernetEnabled) {
        if (!hasEthPins_) {
            LOGE("ethernet enabled in config but board has no valid W5500 pin mapping");
            setState_(EthernetState::ErrorWait);
            return;
        }
        if (!services.has(ServiceId::NetworkAccess)) {
            if (services.add(ServiceId::NetworkAccess, &netAccessSvc_)) {
                serviceRegistered_ = true;
            } else {
                LOGE("service registration failed: %s", toString(ServiceId::NetworkAccess));
            }
        }
        setState_(EthernetState::Starting);
        LOGI("[NET] Starting Ethernet (W5500 DHCP via ETH.begin)");
    } else {
        cleanupDriver_();
        resetRuntimeState_();
        setState_(EthernetState::Disabled);
#if ENABLE_ETHERNET
        LOGI("Ethernet disabled");
#else
        LOGI("Ethernet disabled at compile time (ENABLE_ETHERNET=0)");
#endif
    }
}

void EthernetModule::loop()
{
    if (deviceNameDirty_) {
        deviceNameDirty_ = false;
        loadSystemDeviceName_();
    }

    if (cfgData_.enabled || driverStarted_) {
        syncRuntimeState_();
        handleDeferredDriverActions_();
    }

    switch (state_) {
        case EthernetState::Disabled:
            vTaskDelay(pdMS_TO_TICKS(300));
            break;

        case EthernetState::Starting:
            if (ensureDriverStarted_()) {
                setState_(EthernetState::WaitingIp);
            } else {
                setState_(EthernetState::ErrorWait);
            }
            vTaskDelay(pdMS_TO_TICKS(80));
            break;

        case EthernetState::WaitingIp:
            if (gotIp_) {
                setState_(EthernetState::Connected);
            }
            vTaskDelay(pdMS_TO_TICKS(120));
            break;

        case EthernetState::Connected:
            if (!gotIp_) {
                setState_(EthernetState::WaitingIp);
            }
            vTaskDelay(pdMS_TO_TICKS(150));
            break;

        case EthernetState::ErrorWait:
            if ((millis() - stateTs_) >= kErrorRetryMs) {
                LOGW("Retrying Ethernet start (attempt=%lu, consecutive_failures=%lu, last_stage=%s, last_err=%d:%s)",
                     (unsigned long)startAttempts_,
                     (unsigned long)consecutiveStartFailures_,
                     lastStartFailureStage_ ? lastStartFailureStage_ : "none",
                     lastStartFailureErr_,
                     esp_err_to_name((esp_err_t)lastStartFailureErr_));
                setState_(EthernetState::Starting);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
    }
}

void EthernetModule::onNetworkEventStatic_(arduino_event_t* event)
{
    if (!event) return;
    EthernetModule* self = gEthernetInstance;
    if (!self) return;
    self->onNetworkEvent_(event);
}

void EthernetModule::onNetworkEvent_(arduino_event_t* event)
{
    if (!event) return;

    switch (event->event_id) {
        case ARDUINO_EVENT_ETH_START:
            LOGI("ETH event: started");
            hostnameDirty_ = true;
            break;

        case ARDUINO_EVENT_ETH_CONNECTED:
            linkUp_ = true;
            linkDirty_ = true;
            linkInfoDirty_ = true;
            LOGI("ETH link up");
            break;

        case ARDUINO_EVENT_ETH_GOT_IP: {
            const ip_event_got_ip_t* got = &event->event_info.got_ip;
            ipAddr_ = got->ip_info.ip.addr;
            gotIp_ = true;
            ipDirty_ = true;
            linkUp_ = true;
            linkDirty_ = true;
            LOGI("[NET] Ethernet got IP: " IPSTR, IP2STR(&got->ip_info.ip));
            mdnsStartDirty_ = true;
            break;
        }

        case ARDUINO_EVENT_ETH_LOST_IP:
            gotIp_ = false;
            ipAddr_ = 0U;
            ipDirty_ = true;
            LOGW("ETH lost IP");
            mdnsStopDirty_ = true;
            break;

        case ARDUINO_EVENT_ETH_DISCONNECTED:
            linkUp_ = false;
            gotIp_ = false;
            ipAddr_ = 0U;
            linkDirty_ = true;
            ipDirty_ = true;
            LOGW("ETH link down");
            mdnsStopDirty_ = true;
            break;

        case ARDUINO_EVENT_ETH_STOP:
            linkUp_ = false;
            gotIp_ = false;
            ipAddr_ = 0U;
            linkDirty_ = true;
            ipDirty_ = true;
            LOGI("ETH event: stopped");
            mdnsStopDirty_ = true;
            break;

        default:
            break;
    }
}

void EthernetModule::onEventStatic_(const Event& e, void* user)
{
    EthernetModule* self = static_cast<EthernetModule*>(user);
    if (self) self->onEvent_(e);
}

void EthernetModule::onEvent_(const Event& e)
{
    if (e.id != EventId::ConfigChanged) return;
    if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;

    const ConfigChangedPayload* p = static_cast<const ConfigChangedPayload*>(e.payload);
    if (p->moduleId != (uint8_t)ConfigModuleId::System) return;
    if (strcmp(p->nvsKey, NvsKeys::System::DeviceName) != 0) return;
    deviceNameDirty_ = true;
}

void EthernetModule::setState_(EthernetState next)
{
    if (state_ == next) return;
    state_ = next;
    stateTs_ = millis();
    LOGD("state=%s", stateName(state_));
}

void EthernetModule::resetRuntimeState_()
{
    linkUp_ = false;
    gotIp_ = false;
    ipAddr_ = 0U;
    ipDirty_ = true;
    linkDirty_ = true;
    hostnameDirty_ = false;
    linkInfoDirty_ = false;
    mdnsStartDirty_ = false;
    mdnsStopDirty_ = false;
    if (dataStore_) {
        setNetworkIp(*dataStore_, IpV4{});
        setNetworkReady(*dataStore_, false);
    }
}

bool EthernetModule::installDriver_()
{
    cleanupDriver_();
    ++startAttempts_;

    LOGI("ETH begin attempt=%lu phy=%d phy_addr=%u spi_clk=%luHz mosi=%d miso=%d sclk=%d cs=%d irq=%d rst=%d",
         (unsigned long)startAttempts_,
         (int)ETH_PHY_W5500,
         (unsigned)ethCfg_.phyAddr,
         (unsigned long)ethCfg_.spiClockHz,
         (int)ethCfg_.mosiPin,
         (int)ethCfg_.misoPin,
         (int)ethCfg_.sclkPin,
         (int)ethCfg_.csPin,
         (int)ethCfg_.intPin,
         (int)ethCfg_.rstPin);

    if (networkEventHandle_ == 0) {
        networkEventHandle_ = Network.onEvent(EthernetModule::onNetworkEventStatic_);
    }

    resetPhyHardware_();
    SPI.begin(ethCfg_.sclkPin, ethCfg_.misoPin, ethCfg_.mosiPin);
    spiStarted_ = true;
    ETH.setTaskStackSize(6144);

    const bool ok = ETH.begin(ETH_PHY_W5500,
                              (int32_t)ethCfg_.phyAddr,
                              ethCfg_.csPin,
                              ethCfg_.intPin,
                              ethCfg_.rstPin,
                              SPI,
                              spiFreqMhz_);
    if (!ok) {
        noteStartFailure_("ETH.begin", (int)ESP_FAIL);
        cleanupDriver_();
        return false;
    }

    driverStarted_ = true;
    consecutiveStartFailures_ = 0U;
    lastStartFailureStage_ = "none";
    lastStartFailureErr_ = ESP_OK;

    LOGI("Ethernet driver started freq_mhz=%u", (unsigned)spiFreqMhz_);
    return true;
}

void EthernetModule::resetPhyHardware_()
{
    if (ethCfg_.csPin >= 0) {
        pinMode(ethCfg_.csPin, OUTPUT);
        digitalWrite(ethCfg_.csPin, HIGH);
    }
    if (ethCfg_.rstPin < 0) return;

    pinMode(ethCfg_.rstPin, OUTPUT);
    digitalWrite(ethCfg_.rstPin, LOW);
    vTaskDelay(pdMS_TO_TICKS(25));
    digitalWrite(ethCfg_.rstPin, HIGH);
    vTaskDelay(pdMS_TO_TICKS(150));
    LOGD("W5500 hardware reset pulse rst=%d", (int)ethCfg_.rstPin);
}

bool EthernetModule::ensureDriverStarted_()
{
    if (!hasEthPins_) {
        noteStartFailure_("invalid_board_pin_mapping", (int)ESP_ERR_INVALID_ARG);
        return false;
    }
    if (driverStarted_) return true;
    return installDriver_();
}

void EthernetModule::cleanupDriver_()
{
    stopMdns_();

    if (networkEventHandle_ != 0) {
        Network.removeEvent(networkEventHandle_);
        networkEventHandle_ = 0;
    }

    if (driverStarted_) {
        ETH.end();
    }

    driverStarted_ = false;
    spiStarted_ = false;
    hostnameDirty_ = false;
    linkInfoDirty_ = false;
    mdnsStartDirty_ = false;
    mdnsStopDirty_ = false;
}

void EthernetModule::noteStartFailure_(const char* stage, int err)
{
    ++consecutiveStartFailures_;
    lastStartFailureStage_ = stage ? stage : "unknown";
    lastStartFailureErr_ = err;
    LOGE("ETH start failed stage=%s err=%d:%s consecutive_failures=%lu",
         lastStartFailureStage_,
         lastStartFailureErr_,
         esp_err_to_name((esp_err_t)lastStartFailureErr_),
         (unsigned long)consecutiveStartFailures_);
}

void EthernetModule::logEthLinkInfo_() const
{
    const String mac = ETH.macAddress();
    LOGI("ETH hwaddr=%s", mac.c_str());
    LOGI("ETH speed=%uM", (unsigned)ETH.linkSpeed());
    LOGI("ETH duplex=%s", ETH.fullDuplex() ? "full" : "half");
}

void EthernetModule::loadSystemDeviceName_()
{
    char next[sizeof(deviceName_)] = "flowio";
    if (cfgStore_) {
        char systemJson[128] = {0};
        if (cfgStore_->toJsonModule("system", systemJson, sizeof(systemJson), nullptr, false)) {
            StaticJsonDocument<128> doc;
            if (deserializeJson(doc, systemJson) == DeserializationError::Ok && doc.is<JsonObjectConst>()) {
                const char* configured = doc.as<JsonObjectConst>()["devicename"] | "";
                if (!isBlank_(configured, sizeof(deviceName_))) {
                    snprintf(next, sizeof(next), "%s", configured);
                }
            }
        }
    }

    next[sizeof(next) - 1U] = '\0';
    if (strncmp(deviceName_, next, sizeof(deviceName_)) == 0) return;
    snprintf(deviceName_, sizeof(deviceName_), "%s", next);
    LOGI("System device name loaded: %s", deviceName_);
    hostnameDirty_ = true;
    if (mdnsStarted_) {
        stopMdns_();
        startMdns_();
    }
}

void EthernetModule::handleDeferredDriverActions_()
{
    if (!driverStarted_) return;

    if (hostnameDirty_) {
        hostnameDirty_ = false;
        char hostname[sizeof(deviceName_)] = {0};
        formatDhcpHostname_(deviceName_, hostname, sizeof(hostname));
        if (!ETH.setHostname(hostname)) {
            LOGW("Ethernet DHCP hostname update failed host=%s", hostname);
        } else {
            LOGI("Ethernet DHCP hostname=%s", hostname);
        }
    }

    if (linkInfoDirty_) {
        linkInfoDirty_ = false;
        logEthLinkInfo_();
    }

    if (mdnsStopDirty_) {
        mdnsStopDirty_ = false;
        mdnsStartDirty_ = false;
        stopMdns_();
    }

    if (mdnsStartDirty_) {
        mdnsStartDirty_ = false;
        startMdns_();
    }
}

void EthernetModule::startMdns_()
{
    if (mdnsStarted_) return;
    if (!gotIp_) return;

    char host[sizeof(deviceName_)] = {0};
    formatHostname_(deviceName_, host, sizeof(host));

    if (!MDNS.begin(host)) {
        LOGW("mDNS start failed host=%s on Ethernet", host);
        return;
    }
    mdnsStarted_ = true;
    snprintf(mdnsApplied_, sizeof(mdnsApplied_), "%s", host);
    LOGI("mDNS started host=%s.local (Ethernet)", mdnsApplied_);
}

void EthernetModule::stopMdns_()
{
    if (!mdnsStarted_) return;
    MDNS.end();
    mdnsStarted_ = false;
    mdnsApplied_[0] = '\0';
    LOGI("mDNS stopped (Ethernet)");
}

void EthernetModule::syncRuntimeState_()
{
    if (!dataStore_) return;

    if (ipDirty_) {
        ipDirty_ = false;
        if (gotIp_) {
            IpV4 ip{};
            const esp_ip4_addr_t ip4 = {ipAddr_};
            ip.b[0] = (uint8_t)esp_ip4_addr1_16(&ip4);
            ip.b[1] = (uint8_t)esp_ip4_addr2_16(&ip4);
            ip.b[2] = (uint8_t)esp_ip4_addr3_16(&ip4);
            ip.b[3] = (uint8_t)esp_ip4_addr4_16(&ip4);
            setNetworkIp(*dataStore_, ip);
        } else if (!wifiStaConnected_()) {
            setNetworkIp(*dataStore_, IpV4{});
        }
    }

    if (linkDirty_) {
        linkDirty_ = false;
    }
    setNetworkReady(*dataStore_, gotIp_ || wifiStaConnected_());
}

bool EthernetModule::wifiStaConnected_() const
{
    return WiFi.isConnected();
}

bool EthernetModule::wifiApActive_() const
{
    const wifi_mode_t mode = WiFi.getMode();
    if ((mode & WIFI_MODE_AP) == 0) return false;
    const IPAddress ip = WiFi.softAPIP();
    return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;
}

bool EthernetModule::isWebReachable_() const
{
    return gotIp_ || wifiStaConnected_() || wifiApActive_();
}

NetworkAccessMode EthernetModule::mode_() const
{
    if (gotIp_ || wifiStaConnected_()) return NetworkAccessMode::Station;
    if (wifiApActive_()) return NetworkAccessMode::AccessPoint;
    return NetworkAccessMode::None;
}

bool EthernetModule::getIp_(char* out, size_t len) const
{
    if (!out || len == 0) return false;
    out[0] = '\0';

    if (gotIp_) {
        const esp_ip4_addr_t ip = {ipAddr_};
        snprintf(out, len, IPSTR, IP2STR(&ip));
        return out[0] != '\0';
    }

    if (wifiStaConnected_()) {
        const IPAddress ip = WiFi.localIP();
        snprintf(out, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        return out[0] != '\0';
    }

    if (wifiApActive_()) {
        const IPAddress ip = WiFi.softAPIP();
        snprintf(out, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        return out[0] != '\0';
    }

    return out[0] != '\0';
}

bool EthernetModule::notifyWifiConfigChanged_()
{
    const WifiService* wifi = services_ ? services_->get<WifiService>(ServiceId::Wifi) : nullptr;
    bool ok = false;
    if (wifi && wifi->setStaRetryEnabled) {
        ok = wifi->setStaRetryEnabled(wifi->ctx, true) || ok;
    }
    if (wifi && wifi->requestReconnect) {
        ok = wifi->requestReconnect(wifi->ctx) || ok;
    }
    return ok;
}

bool EthernetModule::notifyShutdownPending_()
{
    if (eventBus_) {
        return eventBus_->post(EventId::NetworkShutdownPending, nullptr, 0, moduleId());
    }
    return true;
}
