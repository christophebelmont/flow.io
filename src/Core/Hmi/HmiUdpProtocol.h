#pragma once
/**
 * @file HmiUdpProtocol.h
 * @brief Compact UDP protocol shared by flow.io and the remote Nextion display.
 */

#include <stddef.h>
#include <stdint.h>

#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"

static constexpr uint8_t HMI_UDP_MAGIC0 = 'F';
static constexpr uint8_t HMI_UDP_MAGIC1 = 'H';
static constexpr uint8_t HMI_UDP_VERSION = 7;

static constexpr uint16_t HMI_UDP_PORT = 42110;
static constexpr size_t HMI_UDP_MAX_PACKET = 512;
static constexpr size_t HMI_UDP_HOME_TEXT_MAX = 32;
static constexpr uint8_t HMI_UDP_CONFIG_SNAPSHOT_ROWS = ConfigMenuModel::RowsPerPage;

static constexpr uint8_t HMI_UDP_FLAG_ACK_REQUIRED = 0x01;
static constexpr uint8_t HMI_UDP_FLAG_IS_ACK = 0x02;

enum class HmiUdpMsgType : uint8_t {
    Hello = 1,
    Welcome = 2,
    Ping = 3,
    Pong = 4,
    Ack = 5,

    HomeText = 10,
    HomeGauge = 11,
    HomeStateBits = 12,
    HomeAlarmBits = 13,
    FullRefresh = 14,
    HomeV2Needles = 15,

    HmiEvent = 20,

    ConfigStart = 30,
    ConfigRow = 31,
    ConfigEnd = 32,
    ConfigValues = 33,
    ConfigViewSnapshot = 34,

    RtcReadRequest = 40,
    RtcReadResponse = 41,
    RtcWrite = 42,

    Error = 250
};

#pragma pack(push, 1)
struct HmiUdpHeader {
    uint8_t magic0;
    uint8_t magic1;
    uint8_t version;
    uint8_t type;
    uint16_t seq;
    uint16_t ack;
    uint8_t flags;
    uint16_t len;
    uint16_t crc;
};

struct HmiUdpHelloPayload {
    uint32_t tokenCrc;
    uint16_t displayFw;
    uint16_t protoVersion;
    char nextionVersion[HMI_DISPLAY_VERSION_TEXT_MAX];
    uint8_t flags;
};

static constexpr uint8_t HMI_UDP_HELLO_FLAG_NEXTION_VERSION_VALID = 0x01;
static constexpr uint8_t HMI_UDP_HELLO_FLAG_NEXTION_SLEEPING = 0x02;

struct HmiUdpWelcomePayload {
    uint16_t flowFw;
    uint16_t protoVersion;
    uint8_t accepted;
};

struct HmiUdpHomeTextPayload {
    uint8_t field;
    char text[HMI_UDP_HOME_TEXT_MAX];
};

struct HmiUdpHomeGaugePayload {
    uint8_t field;
    uint16_t percent;
};

struct HmiUdpHomeV2NeedlesPayload {
    uint8_t flags;
    int8_t phNeedle;
    int8_t orpNeedle;
    uint8_t psiNeedle;
};

static constexpr uint8_t HMI_UDP_V2_NEEDLE_PH = 0x01;
static constexpr uint8_t HMI_UDP_V2_NEEDLE_ORP = 0x02;
static constexpr uint8_t HMI_UDP_V2_NEEDLE_PSI = 0x04;

struct HmiUdpStateBitsPayload {
    uint32_t stateBits;
};

struct HmiUdpAlarmBitsPayload {
    uint32_t alarmBits;
};

struct HmiUdpEventPayload {
    uint8_t type;
    uint8_t command;
    uint32_t contextRef;
    uint8_t pageId;
    uint8_t row;
    uint8_t value;
    int8_t direction;
    float sliderValue;
    char text[48];
};

struct HmiUdpConfigStartPayload {
    uint8_t page;
    uint8_t pageCount;
    uint8_t flags;
    uint32_t contextRef;
    char title[24];
};

struct HmiUdpConfigRowPayload {
    uint8_t row;
    uint8_t widget;
    uint8_t editType;
    uint8_t flags;
    char label[24];
    char value[24];
};

struct HmiUdpConfigEndPayload {
    uint8_t rowCount;
};

struct HmiUdpConfigViewSnapshotPayload {
    uint8_t page;
    uint8_t pageCount;
    uint8_t flags;
    uint8_t rowCount;
    uint8_t mode;
    uint32_t contextRef;
    char title[24];
    HmiUdpConfigRowPayload rows[HMI_UDP_CONFIG_SNAPSHOT_ROWS];
};

struct HmiUdpRtcPayload {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};
#pragma pack(pop)

static constexpr uint8_t HMI_UDP_CONFIG_ROW_VISIBLE = 0x01;
static constexpr uint8_t HMI_UDP_CONFIG_ROW_VALUE_VISIBLE = 0x02;
static constexpr uint8_t HMI_UDP_CONFIG_ROW_EDITABLE = 0x04;
static constexpr uint8_t HMI_UDP_CONFIG_ROW_DIRTY = 0x08;
static constexpr uint8_t HMI_UDP_CONFIG_ROW_CAN_ENTER = 0x10;
static constexpr uint8_t HMI_UDP_CONFIG_ROW_CAN_EDIT = 0x20;
static constexpr uint8_t HMI_UDP_CONFIG_MODE_EDIT = 0x40;

static constexpr uint8_t HMI_UDP_CONFIG_VIEW_CAN_HOME = 0x01;
static constexpr uint8_t HMI_UDP_CONFIG_VIEW_CAN_BACK = 0x02;
static constexpr uint8_t HMI_UDP_CONFIG_VIEW_CAN_VALIDATE = 0x04;
static constexpr uint8_t HMI_UDP_CONFIG_VIEW_IS_HOME = 0x08;

uint16_t hmiUdpCrc16(const uint8_t* data, size_t len, uint16_t seed = 0xFFFFU);
uint32_t hmiUdpTokenCrc(const char* token);
bool hmiUdpBuildPacket(uint8_t* out,
                       size_t outCapacity,
                       size_t& outLen,
                       HmiUdpMsgType type,
                       uint16_t seq,
                       uint16_t ack,
                       uint8_t flags,
                       const void* payload,
                       size_t payloadLen);
bool hmiUdpValidatePacket(const uint8_t* data, size_t len, const HmiUdpHeader*& header, const uint8_t*& payload);
void hmiUdpEventToPayload(const HmiEvent& event, HmiUdpEventPayload& out);
void hmiUdpPayloadToEvent(const HmiUdpEventPayload& payload, HmiEvent& out);
void hmiUdpRtcToPayload(const HmiRtcDateTime& rtc, HmiUdpRtcPayload& out);
void hmiUdpPayloadToRtc(const HmiUdpRtcPayload& payload, HmiRtcDateTime& out);
