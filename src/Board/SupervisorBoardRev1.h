#pragma once

#include "Board/BoardSpec.h"

namespace BoardProfiles {

inline constexpr uint32_t kSupervisorBoardRev1UartBaud = 115200U;
inline constexpr uint32_t kSupervisorBoardRev1InterlinkI2cHz = 400000U;
inline constexpr IoCapacitySpec kSupervisorBoardRev1IoCapacity{1, 1, 1, 6, 5, 8};
inline constexpr MqttCapacitySpec kSupervisorBoardRev1MqttCapacity{5712, 8, 8, 48, 24, 16, 2, 80, 80, 80, 60};
inline constexpr MqttBufferSpec kSupervisorBoardRev1MqttBuffers{
    64, 32, 32, 15, 15, 70, 160, 128, 384, 1536, 1024, 1536, 1536, 64, 320, 32
};
inline constexpr HaCapacitySpec kSupervisorBoardRev1HaCapacity{48, 6, 14, 22, 24, 6};

inline constexpr UartSpec kSupervisorBoardRev1Uarts[] = {
    // {name, uartIndex, rxPin, txPin, baud, primary, enableRxPin}
    {"log", 0, -1, -1, kSupervisorBoardRev1UartBaud, true, -1}, // USB serial logs (UART0 default pins).
    {"bridge", 2, 16, 17, kSupervisorBoardRev1UartBaud, false, -1}, // flow.io bridge UART (RX=GPIO16, TX=GPIO17).
    {"panel", 2, 33, 32, kSupervisorBoardRev1UartBaud, false, -1}, // Nextion panel UART (RX=GPIO33, TX=GPIO32).
};

inline constexpr I2cBusSpec kSupervisorBoardRev1I2c[] = {
    // {name, sdaPin, sclPin, frequencyHz}
    {"interlink", 27, 13, kSupervisorBoardRev1InterlinkI2cHz}, // flow.io interlink bus (SDA=GPIO27, SCL=GPIO13).
};

inline constexpr St7789DisplaySpec kSupervisorBoardRev1Display{
    240,      // resX: horizontal pixels.
    320,      // resY: vertical pixels.
    1,        // rotation: default landscape/portrait orientation index.
    0,        // colStart: panel X offset.
    0,        // rowStart: panel Y offset.
    14,       // backlightPin: TFT backlight GPIO.
    15,       // csPin: SPI chip-select GPIO.
    4,        // dcPin: SPI data/command GPIO.
    5,        // rstPin: TFT hardware reset GPIO.
    35,       // misoPin: SPI MISO GPIO.
    18,       // mosiPin: SPI MOSI GPIO.
    19,       // sclkPin: SPI clock GPIO.
    false,    // swapColorBytes: RGB565 byte order unchanged.
    true,     // invertColors: panel color inversion enabled.
    8000000U, // spiHz: SPI clock frequency.
    80        // minRenderGapMs: minimum gap between render passes.
};

inline constexpr SupervisorInputSpec kSupervisorBoardRev1Inputs{
    36,   // pirPin: PIR sensor GPIO.
    120,  // pirDebounceMs: PIR debounce time.
    true, // pirActiveHigh: PIR signal polarity.
    23,   // factoryResetPin: long-press reset button GPIO.
    40    // factoryResetDebounceMs: reset button debounce time.
};

inline constexpr SupervisorUpdateSpec kSupervisorBoardRev1Update{
    25,                       // flowIoEnablePin: controls target flow.io EN line.
    26,                       // flowIoBootPin: controls target flow.io boot strap.
    12,                       // nextionRebootPin: hardware reboot pin for Nextion panel.
    kSupervisorBoardRev1UartBaud // nextionUploadBaud: upload UART baud rate.
};

inline constexpr SupervisorBoardSpec kSupervisorBoardRev1Supervisor{
    kSupervisorBoardRev1Display,
    kSupervisorBoardRev1Inputs,
    kSupervisorBoardRev1Update
};

inline constexpr BoardSpec kSupervisorBoardRev1{
    "SupervisorBoardRev1",
    "flowio",
    kSupervisorBoardRev1Uarts,
    (uint8_t)(sizeof(kSupervisorBoardRev1Uarts) / sizeof(kSupervisorBoardRev1Uarts[0])),
    kSupervisorBoardRev1I2c,
    (uint8_t)(sizeof(kSupervisorBoardRev1I2c) / sizeof(kSupervisorBoardRev1I2c[0])),
    nullptr,
    0,
    nullptr,
    0,
    kSupervisorBoardRev1IoCapacity,
    kSupervisorBoardRev1MqttCapacity,
    kSupervisorBoardRev1MqttBuffers,
    kSupervisorBoardRev1HaCapacity,
    &kSupervisorBoardRev1Supervisor
};

}  // namespace BoardProfiles
