#pragma once

#include "Board/BoardSpec.h"

namespace BoardProfiles {

inline constexpr uint32_t kFlowIODINv1IoI2cHz = 400000U;
inline constexpr uint32_t kFlowIODINv1InterlinkI2cHz = 400000U;
inline constexpr uint16_t kFlowIODINMomentaryPulseMs = 500U;

inline constexpr IoCapacitySpec kFlowIODINIoCapacity{17, 5, 10, 17, 5, 10};
inline constexpr MqttCapacitySpec kFlowIODINMqttCapacity{5712, 8, 8, 48, 24, 16, 2, 192, 80, 80, 128};
inline constexpr MqttBufferSpec kFlowIODINMqttBuffers{
    64, 32, 32, 15, 15, 70, 160, 128, 384, 1536, 1024, 1536, 1536, 64, 320, 32
};
inline constexpr HaCapacitySpec kFlowIODINHaCapacity{48, 6, 16, 30, 24, 6};

inline constexpr UartSpec kFlowIODINv1Uarts[] = {
    // {name, uartIndex, rxPin, txPin, baud, primary, enableRxPin}
    {"log", 0, -1, -1, 115200, true, -1}, // USB serial console (UART0 default pins).
    {"hmi", 2, 16, 17, 115200, false, -1}, // HMI link on UART2 (RX=GPIO16, TX=GPIO17).
};

inline constexpr I2cBusSpec kFlowIODINv1I2c[] = {
    // {name, sdaPin, sclPin, frequencyHz}
    {"io", 21, 22, kFlowIODINv1IoI2cHz}, // Primary IO I2C bus (GPIO21/22, 400 kHz).
    {"interlink", 5, 15, kFlowIODINv1InterlinkI2cHz}, // Supervisor interlink bus defaults (SDA=GPIO5, SCL=GPIO15).
};

inline constexpr I2cBusSpec kFlowIODINv1S3I2c[] = {
    // ESP32-S3 does not expose GPIO22-25; keep I2C on valid and common S3 pins.
    {"io", 8, 9, kFlowIODINv1IoI2cHz},
    {"interlink", 5, 15, kFlowIODINv1InterlinkI2cHz},
};

inline constexpr OneWireBusSpec kFlowIODINv1OneWire[] = {
    // {name, signal, pin}
    {"temp_probe_1", BoardSignal::TempProbe1, 19}, // Water temperature probe bus on GPIO19.
    {"temp_probe_2", BoardSignal::TempProbe2, 18}, // Air temperature probe bus on GPIO18.
};

inline constexpr IoPointSpec kFlowIODINv1IoPoints[] = {
    // {name, capability, signal, pin, momentary, pulseMs}
    {"relay1", IoCapability::DigitalOut, BoardSignal::Relay1, 32, false, 0}, // Filtration pump relay on GPIO32.
    {"relay2", IoCapability::DigitalOut, BoardSignal::Relay2, 25, false, 0}, // pH pump relay on GPIO25.
    {"relay3", IoCapability::DigitalOut, BoardSignal::Relay3, 26, false, 0}, // Chlorine pump relay on GPIO26.
    {"relay4", IoCapability::DigitalOut, BoardSignal::Relay4, 13, true, kFlowIODINMomentaryPulseMs}, // Chlorine generator pulse relay on GPIO13.
    {"relay5", IoCapability::DigitalOut, BoardSignal::Relay5, 33, false, 0}, // Robot relay on GPIO33.
    {"relay6", IoCapability::DigitalOut, BoardSignal::Relay6, 27, false, 0}, // Lights relay on GPIO27.
    {"relay7", IoCapability::DigitalOut, BoardSignal::Relay7, 23, false, 0}, // Fill pump relay on GPIO23.
    {"relay8", IoCapability::DigitalOut, BoardSignal::Relay8, 4, false, 0},  // Water heater relay on GPIO4.
    {"digital_in1", IoCapability::DigitalIn, BoardSignal::DigitalIn1, 34, false, 0}, // Pool level sensor input on GPIO34.
    {"digital_in2", IoCapability::DigitalIn, BoardSignal::DigitalIn2, 36, false, 0}, // pH tank level sensor input on GPIO36.
    {"digital_in3", IoCapability::DigitalIn, BoardSignal::DigitalIn3, 36, false, 0}, // Chlorine tank level sensor input on GPIO39.
    {"digital_in4", IoCapability::DigitalIn, BoardSignal::DigitalIn4, 35, false, 0}, // Water counter pulse input on GPIO35.
    {"analog_in1", IoCapability::AnalogIn, BoardSignal::AnalogIn1, 0, false, 0}, // ADS1115 internal channel 0.
    {"analog_in2", IoCapability::AnalogIn, BoardSignal::AnalogIn2, 1, false, 0}, // ADS1115 internal channel 1.
    {"analog_in3", IoCapability::AnalogIn, BoardSignal::AnalogIn3, 2, false, 0}, // ADS1115 internal channel 2.
    {"analog_in4", IoCapability::AnalogIn, BoardSignal::AnalogIn4, 3, false, 0}, // ADS1115 internal channel 3.
    {"temp_probe_1", IoCapability::OneWireTemp, BoardSignal::TempProbe1, 19, false, 0}, // Water probe on GPIO19.
    {"temp_probe_2", IoCapability::OneWireTemp, BoardSignal::TempProbe2, 18, false, 0}, // Air probe on GPIO18.
};

inline constexpr BoardSpec kFlowIODINv1{
    "flow.io DIN v1",
    "flowio-core",
    kFlowIODINv1Uarts,
    (uint8_t)(sizeof(kFlowIODINv1Uarts) / sizeof(kFlowIODINv1Uarts[0])),
    kFlowIODINv1I2c,
    (uint8_t)(sizeof(kFlowIODINv1I2c) / sizeof(kFlowIODINv1I2c[0])),
    kFlowIODINv1OneWire,
    (uint8_t)(sizeof(kFlowIODINv1OneWire) / sizeof(kFlowIODINv1OneWire[0])),
    kFlowIODINv1IoPoints,
    (uint8_t)(sizeof(kFlowIODINv1IoPoints) / sizeof(kFlowIODINv1IoPoints[0])),
    kFlowIODINIoCapacity,
    kFlowIODINMqttCapacity,
    kFlowIODINMqttBuffers,
    kFlowIODINHaCapacity,
    nullptr
};

inline constexpr auto& kFlowIODINv2Uarts = kFlowIODINv1Uarts;
inline constexpr auto& kFlowIODINv2I2c = kFlowIODINv1I2c;
inline constexpr auto& kFlowIODINv2OneWire = kFlowIODINv1OneWire;
inline constexpr auto& kFlowIODINv2IoPoints = kFlowIODINv1IoPoints;

inline constexpr BoardSpec kFlowIODINv2{
    "flow.io DIN v2",
    "flowio-core",
    kFlowIODINv2Uarts,
    (uint8_t)(sizeof(kFlowIODINv2Uarts) / sizeof(kFlowIODINv2Uarts[0])),
    kFlowIODINv2I2c,
    (uint8_t)(sizeof(kFlowIODINv2I2c) / sizeof(kFlowIODINv2I2c[0])),
    kFlowIODINv2OneWire,
    (uint8_t)(sizeof(kFlowIODINv2OneWire) / sizeof(kFlowIODINv2OneWire[0])),
    kFlowIODINv2IoPoints,
    (uint8_t)(sizeof(kFlowIODINv2IoPoints) / sizeof(kFlowIODINv2IoPoints[0])),
    kFlowIODINIoCapacity,
    kFlowIODINMqttCapacity,
    kFlowIODINMqttBuffers,
    kFlowIODINHaCapacity,
    nullptr
};

inline constexpr BoardSpec kFlowIODINv1S3{
    "flow.io DIN v1 S3",
    "flowio-core",
    kFlowIODINv1Uarts,
    (uint8_t)(sizeof(kFlowIODINv1Uarts) / sizeof(kFlowIODINv1Uarts[0])),
    kFlowIODINv1S3I2c,
    (uint8_t)(sizeof(kFlowIODINv1S3I2c) / sizeof(kFlowIODINv1S3I2c[0])),
    kFlowIODINv1OneWire,
    (uint8_t)(sizeof(kFlowIODINv1OneWire) / sizeof(kFlowIODINv1OneWire[0])),
    kFlowIODINv1IoPoints,
    (uint8_t)(sizeof(kFlowIODINv1IoPoints) / sizeof(kFlowIODINv1IoPoints[0])),
    kFlowIODINIoCapacity,
    kFlowIODINMqttCapacity,
    kFlowIODINMqttBuffers,
    kFlowIODINHaCapacity,
    nullptr
};

}  // namespace BoardProfiles
