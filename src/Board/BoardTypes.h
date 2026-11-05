#pragma once

#include <stddef.h>
#include <stdint.h>

enum class IoCapability : uint8_t {
    None = 0,       // No electrical capability attached to this point.
    DigitalIn = 1,  // Binary input (GPIO/logic-level read).
    DigitalOut = 2, // Binary output (relay, transistor, etc.).
    AnalogIn = 3,   // Analog measurement input.
    OneWireTemp = 4 // 1-Wire temperature probe endpoint.
};

enum class BoardSignal : uint8_t {
    None = 0, // Unmapped signal.
    Relay1,   // Relay output channel 1.
    Relay2,   // Relay output channel 2.
    Relay3,   // Relay output channel 3.
    Relay4,   // Relay output channel 4.
    Relay5,   // Relay output channel 5.
    Relay6,   // Relay output channel 6.
    Relay7,   // Relay output channel 7.
    Relay8,   // Relay output channel 8.
    DigitalIn1, // Digital input channel 1.
    DigitalIn2, // Digital input channel 2.
    DigitalIn3, // Digital input channel 3.
    DigitalIn4, // Digital input channel 4.
    DigitalIn5, // Digital input channel 5.
    DigitalIn6, // Digital input channel 6.
    DigitalIn7, // Digital input channel 7.
    DigitalIn8, // Digital input channel 8.
    AnalogIn1, // Analog input channel 1.
    AnalogIn2, // Analog input channel 2.
    AnalogIn3, // Analog input channel 3.
    AnalogIn4, // Analog input channel 4.
    TempProbe1, // 1-Wire temperature probe 1.
    TempProbe2, // 1-Wire temperature probe 2.
    Tx433,      // 433 MHz transmitter output.
    Buzzer      // Local active buzzer output.
};

struct UartSpec {
    const char* name;  // Logical UART name used by modules (e.g. "log", "hmi").
    uint8_t uartIndex; // ESP32 UART index (0..2).
    int8_t rxPin;      // RX GPIO number (-1 when default/unused).
    int8_t txPin;      // TX GPIO number (-1 when default/unused).
    uint32_t baud;     // Baud rate.
    bool primary;      // True when this UART is the primary instance for its role.
    int8_t enableRxPin; // Optional GPIO used to switch a half-duplex adapter to receive mode.
};

struct I2cBusSpec {
    const char* name;   // Logical bus name.
    int8_t sdaPin;      // SDA GPIO pin (-1 when disabled/unwired).
    int8_t sclPin;      // SCL GPIO pin (-1 when disabled/unwired).
    uint32_t frequencyHz; // I2C clock frequency in Hz.
};

struct OneWireBusSpec {
    const char* name;   // Logical 1-Wire bus name.
    BoardSignal signal; // Signal associated with this bus.
    uint8_t pin;        // GPIO pin carrying the 1-Wire data line.
};

struct IoPointSpec {
    const char* name;       // Logical IO point name.
    IoCapability capability; // Hardware capability (digital/analog/1-Wire).
    BoardSignal signal;     // Signal identifier routed to this point.
    uint8_t pin;            // GPIO pin (or logical channel id for some backends).
    bool momentary;         // True when output is pulse-based (monostable style).
    uint16_t pulseMs;       // Pulse duration in ms for momentary outputs.
};

struct IoCapacitySpec {
    uint8_t analogEndpoints;  // Number of analog runtime endpoints expected for this board/profile.
    uint8_t digitalInputs;    // Number of digital input runtime endpoints expected for this board/profile.
    uint8_t digitalOutputs;   // Number of digital output runtime endpoints expected for this board/profile.
    uint8_t analogConfigSlots = 6;       // Number of analog config slots exposed by config/NVS.
    uint8_t digitalInputConfigSlots = 5; // Number of digital input config slots exposed by config/NVS.
    uint8_t digitalOutputConfigSlots = 8; // Number of digital output config slots exposed by config/NVS.
};

struct MqttCapacitySpec {
    uint16_t taskStackSize = 5712;
    uint8_t rxQueueLen = 8;
    uint8_t maxPublishers = 8;
    uint8_t cfgTopicMax = 48;
    uint8_t maxProducers = 24;
    uint8_t maxInboundHandlers = 16;
    uint8_t maxAckMessages = 2;
    uint8_t maxJobs = 80;
    uint16_t highQueueCap = 80;
    uint16_t normalQueueCap = 80;
    uint16_t lowQueueCap = 60;
};

struct MqttBufferSpec {
    size_t host = 64;
    size_t user = 32;
    size_t pass = 32;
    size_t baseTopic = 15;
    size_t deviceId = 15;
    size_t topic = 70;
    size_t dynamicTopic = 160;
    size_t rxTopic = 128;
    size_t rxPayload = 384;
    size_t ack = 1536;
    size_t reply = 1024;
    size_t stateCfg = 1536;
    size_t publish = 1536;
    size_t cmdName = 64;
    size_t cmdArgs = 320;
    size_t cmdModule = 32;
};

struct HaCapacitySpec {
    uint8_t sensors = 40;
    uint8_t binarySensors = 6;
    uint8_t switches = 14;
    uint8_t numbers = 14;
    uint8_t buttons = 24;
    uint8_t selects = 4;
};

struct St7789DisplaySpec {
    uint16_t resX;          // Horizontal resolution in pixels.
    uint16_t resY;          // Vertical resolution in pixels.
    uint8_t rotation;       // Panel rotation mode.
    int8_t colStart;        // X offset applied by the controller.
    int8_t rowStart;        // Y offset applied by the controller.
    int8_t backlightPin;    // GPIO driving the TFT backlight.
    int8_t csPin;           // SPI chip-select pin.
    int8_t dcPin;           // SPI data/command pin.
    int8_t rstPin;          // Hardware reset pin.
    int8_t misoPin;         // SPI MISO pin.
    int8_t mosiPin;         // SPI MOSI pin.
    int8_t sclkPin;         // SPI clock pin.
    bool swapColorBytes;    // True when RGB565 byte order must be swapped.
    bool invertColors;      // True when panel color inversion is enabled.
    uint32_t spiHz;         // SPI bus speed in Hz.
    uint16_t minRenderGapMs; // Minimum gap between render passes in ms.
};

struct SupervisorInputSpec {
    int8_t pirPin;                 // GPIO pin for PIR motion sensor.
    uint16_t pirDebounceMs;        // Debounce duration for PIR input.
    bool pirActiveHigh;            // PIR polarity (true = active high).
    int8_t factoryResetPin;        // GPIO pin for factory-reset button.
    uint16_t factoryResetDebounceMs; // Debounce duration for factory-reset input.
};

struct SupervisorUpdateSpec {
    int8_t flowIoEnablePin;  // GPIO controlling flow.io EN line.
    int8_t flowIoBootPin;    // GPIO controlling flow.io boot strap line.
    int8_t nextionRebootPin; // GPIO used to reboot the Nextion panel.
    uint32_t nextionUploadBaud; // UART baud used during Nextion upload.
};

struct SupervisorBoardSpec {
    St7789DisplaySpec display;    // TFT hardware wiring and timings.
    SupervisorInputSpec inputs;   // Supervisor local input pins and behavior.
    SupervisorUpdateSpec update;  // Pins/settings for downstream update control.
};

struct ProvisioningPolicySpec {
    bool enabled = false;                  // Enable provisioning-first boot mode for this board.
    bool disableAfterConfigured = false;   // Stop provisioning web/AP once setup is complete.
    bool requireMqttForConfigured = false; // Require MQTT config in addition to Wi-Fi config.
};

struct EthernetW5500Spec {
    bool enabled = false;       // True when this board exposes a W5500 wired Ethernet interface.
    int8_t mosiPin = -1;        // SPI MOSI pin.
    int8_t misoPin = -1;        // SPI MISO pin.
    int8_t sclkPin = -1;        // SPI clock pin.
    int8_t csPin = -1;          // W5500 chip-select pin.
    int8_t intPin = -1;         // W5500 interrupt pin.
    int8_t rstPin = -1;         // W5500 reset pin.
    uint8_t phyAddr = 1;        // W5500 PHY address.
    uint32_t spiClockHz = 20000000U; // SPI clock in Hz.
};

struct HmiBuzzerSpec {
    bool enabled = false;       // True when the board exposes a local HMI buzzer.
    int8_t pin = -1;            // GPIO driving the active buzzer.
    bool activeHigh = true;     // True when HIGH turns the buzzer on.
};

struct BoardSpec {
    const char* name;                  // Board identifier exposed to the app/runtime.
    const char* mdnsHost;              // Default mDNS host name for this board/profile.
    const UartSpec* uarts;             // UART definitions table.
    uint8_t uartCount;                 // Number of UART definitions.
    const I2cBusSpec* i2cBuses;        // I2C bus definitions table.
    uint8_t i2cCount;                  // Number of I2C buses.
    const OneWireBusSpec* oneWireBuses; // 1-Wire bus definitions table.
    uint8_t oneWireCount;              // Number of 1-Wire buses.
    const IoPointSpec* ioPoints;       // IO points mapping table.
    uint8_t ioPointCount;              // Number of IO points.
    IoCapacitySpec ioCapacity{};         // Intended IO runtime capacity for memory sizing.
    MqttCapacitySpec mqttCapacity{};     // Compile-time MQTT capacities for this board/profile.
    MqttBufferSpec mqttBuffers{};        // Compile-time MQTT buffers for this board/profile.
    HaCapacitySpec haCapacity{};         // Compile-time HA entity capacities for this board/profile.
    const SupervisorBoardSpec* supervisor = nullptr; // Optional supervisor-only extension block.
    ProvisioningPolicySpec provisioning{}; // Optional provisioning policy for staged boot.
    const EthernetW5500Spec* ethernetW5500 = nullptr; // Optional W5500 Ethernet wiring block.
    const HmiBuzzerSpec* hmiBuzzer = nullptr; // Optional local HMI feedback buzzer wiring.
};
