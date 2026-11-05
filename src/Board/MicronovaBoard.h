#pragma once

#include "Board/BoardSpec.h"

namespace BoardProfiles {

inline constexpr uint32_t kMicronovaBoardRev1UartBaud = 1200U;
inline constexpr IoCapacitySpec kMicronovaIoCapacity{1, 1, 1, 6, 5, 8};
inline constexpr MqttCapacitySpec kMicronovaMqttCapacity{5712, 2, 2, 4, 20, 12, 1, 64, 32, 32, 32};
inline constexpr MqttBufferSpec kMicronovaMqttBuffers{
    64, 32, 32, 15, 15, 160, 160, 160, 96, 512, 384, 512, 2048, 32, 128, 32
};
inline constexpr HaCapacitySpec kMicronovaHaCapacity{24, 4, 8, 12, 16, 4};

inline constexpr UartSpec kMicronovaBoardRev1Uarts[] = {
    // {name, uartIndex, rxPin, txPin, baud, primary, enableRxPin}
    {"log", 0, -1, -1, 115200, true, -1}, // USB serial console (UART0 default pins).
    {"micronova", 1, 17, 16, kMicronovaBoardRev1UartBaud, false, 21}, // Micronova UART: RX=GPIO17, TX=GPIO16, EN_RX=GPIO21.
};

inline constexpr I2cBusSpec kMicronovaBoardRev1I2c[] = {
    // {name, sdaPin, sclPin, frequencyHz}
    {"io", 22, 23, 100000U}, // Optional local I2C bus on GPIO22/GPIO23.
};

inline constexpr OneWireBusSpec kMicronovaBoardRev1OneWire[] = {
    // {name, signal, pin}
    {"temperature", BoardSignal::TempProbe1, 18}, // Local DS18B20 temperature probe on GPIO18.
};

inline constexpr IoPointSpec kMicronovaBoardRev1IoPoints[] = {
    // {name, capability, signal, pin, momentary, pulseMs}
    {"aux_output", IoCapability::DigitalOut, BoardSignal::Relay1, 19, false, 0}, // Auxiliary digital output on GPIO19.
    {"temperature", IoCapability::OneWireTemp, BoardSignal::TempProbe1, 18, false, 0}, // Local DS18B20 probe on GPIO18.
};

inline constexpr BoardSpec kMicronovaBoardRev1{
    "MicronovaBoardRev1",
    "micronova",
    kMicronovaBoardRev1Uarts,
    (uint8_t)(sizeof(kMicronovaBoardRev1Uarts) / sizeof(kMicronovaBoardRev1Uarts[0])),
    kMicronovaBoardRev1I2c,
    (uint8_t)(sizeof(kMicronovaBoardRev1I2c) / sizeof(kMicronovaBoardRev1I2c[0])),
    kMicronovaBoardRev1OneWire,
    (uint8_t)(sizeof(kMicronovaBoardRev1OneWire) / sizeof(kMicronovaBoardRev1OneWire[0])),
    kMicronovaBoardRev1IoPoints,
    (uint8_t)(sizeof(kMicronovaBoardRev1IoPoints) / sizeof(kMicronovaBoardRev1IoPoints[0])),
    kMicronovaIoCapacity,
    kMicronovaMqttCapacity,
    kMicronovaMqttBuffers,
    kMicronovaHaCapacity,
    nullptr,
    {true, true, true}
};

}  // namespace BoardProfiles
