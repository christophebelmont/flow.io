#pragma once

#include "Board/BoardSpec.h"

/*
 * Waveshare ESP32-S3 board profile.
 *
 * This file is the hardware description for the Waveshare ESP32-S3 based
 * Waveshare target. It is intentionally kept as a single, editable map of the
 * board: serial ports, I2C buses, 1-Wire probes, IO points, TFT wiring,
 * supervisor inputs, MQTT/Home Assistant sizing, and Ethernet wiring.
 *
 * When adapting the firmware to a modified board, change the values here
 * first. The rest of the application consumes this profile through BoardSpec.
 * Pin values are ESP32 GPIO numbers unless a comment explicitly says the value
 * is a logical channel. A value of -1 means "not connected" or "disabled".
 *
 * NVS behavior:
 *   BoardSpec values are not stored as a board object in NVS. Some modules copy
 *   selected board values into their own persistent configuration as boot
 *   defaults; those cases are called out below. When a value is marked
 *   "not stored in NVS", changing it requires editing this file and rebuilding
 *   the firmware.
 */

namespace BoardProfiles {

/*
 * I2C bus speeds in Hz.
 *
 * kWaveshareESP32S3IoI2cHz:
 *   Clock used by the main IO/configuration I2C bus.
 *
 * kWaveshareESP32S3InterlinkI2cHz:
 *   Clock used by the optional interlink I2C bus.
 *
 * NVS behavior:
 *   These frequency constants are not stored in NVS. The compiled values apply
 *   at every boot.
 */
inline constexpr uint32_t kWaveshareESP32S3IoI2cHz = 400000U;
inline constexpr uint32_t kWaveshareESP32S3InterlinkI2cHz = 400000U;

/*
 * IO capacity used to size runtime/config arrays.
 *
 * Field order:
 *   analogEndpoints, digitalInputs, digitalOutputs,
 *   analogConfigSlots, digitalInputConfigSlots, digitalOutputConfigSlots.
 *
 * Increase these values when you expose more runtime endpoints or config slots
 * than the current Waveshare mapping provides.
 *
 * NVS behavior:
 *   Not stored in NVS. These are compile-time sizing limits, so the compiled
 *   values always apply.
 */
inline constexpr IoCapacitySpec kWaveshareESP32S3IoCapacity{16, 13, 16, 16, 13, 16};

/*
 * MQTT task and queue capacities.
 *
 * Field order:
 *   taskStackSize, rxQueueLen, maxPublishers, cfgTopicMax, maxProducers,
 *   maxInboundHandlers, maxAckMessages, maxJobs, highQueueCap,
 *   normalQueueCap, lowQueueCap.
 *
 * These values affect memory sizing and MQTT throughput. Keep them conservative
 * on small targets, and increase them only when the workload requires it.
 *
 * NVS behavior:
 *   Not stored in NVS. These are compile-time MQTT capacity limits. Runtime MQTT
 *   settings such as broker host, port, credentials, base topic, and enabled
 *   state are separate module config values stored in NVS.
 */
inline constexpr MqttCapacitySpec kWaveshareESP32S3MqttCapacity{7168, 8, 8, 48, 24, 16, 2, 192, 80, 80, 128};

/*
 * MQTT string/payload buffer sizes in bytes.
 *
 * Field order:
 *   host, user, pass, baseTopic, deviceId, topic, dynamicTopic, rxTopic,
 *   rxPayload, ack, reply, stateCfg, publish, cmdName, cmdArgs, cmdModule.
 *
 * Increase the relevant field if MQTT topics, command arguments, JSON payloads,
 * or Home Assistant discovery documents are truncated.
 *
 * NVS behavior:
 *   Not stored in NVS. These buffer sizes are compile-time limits and cannot be
 *   changed at boot.
 */
inline constexpr MqttBufferSpec kWaveshareESP32S3MqttBuffers{
    64, 32, 32, 15, 15, 70, 160, 128, 384, 1536, 1024, 1536, 1536, 64, 320, 32
};

/*
 * Home Assistant discovery/entity capacities.
 *
 * Field order:
 *   sensors, binarySensors, switches, numbers, buttons, selects.
 *
 * Increase these counts when the board/profile publishes more HA entities than
 * the current configuration.
 *
 * NVS behavior:
 *   Not stored in NVS. These are compile-time Home Assistant entity limits.
 *   Home Assistant naming/identity options are handled by separate persistent
 *   module config, not by this capacity block.
 */
inline constexpr HaCapacitySpec kWaveshareESP32S3HaCapacity{48, 16, 16, 30, 24, 6};

/*
 * UART definitions.
 *
 * Each entry uses:
 *   name, uartIndex, rxPin, txPin, baud, primary, enableRxPin.
 *
 * name:
 *   Logical name used by modules. "log" is the USB/default console, "hmi" is
 *   the local HMI link.
 *
 * uartIndex:
 *   ESP32 hardware UART number. UART0 is normally the USB serial console.
 *
 * rxPin / txPin:
 *   GPIO numbers for receive/transmit. Use -1 to keep the platform default or
 *   to mark the signal as unused.
 *
 * primary:
 *   True for the primary UART of that role.
 *
 * enableRxPin:
 *   Optional GPIO for half-duplex adapters that need an explicit receive-enable
 *   signal. Use -1 when no enable pin is wired.
 *
 * NVS behavior:
 *   Not stored in NVS for this Waveshare profile. The compiled UART index,
 *   pins, baud rate, and enable pin always apply.
 */
inline constexpr UartSpec kWaveshareESP32S3Uarts[] = {
    {"log", 0, -1, -1, 115200, true, -1}, // USB serial console (UART0 default pins).
    {"hmi", 2, 44, 43, 115200, false, -1}, // HMI link on UART2 (RX=GPIO44, TX=GPIO43).
};

/*
 * I2C bus definitions.
 *
 * Each entry uses:
 *   name, sdaPin, sclPin, frequencyHz.
 *
 * "io":
 *   Main board bus for IO expanders, sensors, or configuration peripherals.
 *
 * "interlink":
 *   Disabled on the Waveshare profile because this board has no interlink
 *   wiring. Keep SDA/SCL at -1 unless you add a real board-to-board bus.
 *
 * NVS behavior:
 *   The "io" bus SDA/SCL pins are copied into IOModule persistent config as
 *   boot defaults; if NVS already contains io/drivers/bus sda/scl values, those
 *   NVS values override the pins from this file. The I2C frequencies and the
 *   disabled "interlink" entry are not stored in NVS by this board profile.
 */
inline constexpr I2cBusSpec kWaveshareESP32S3I2c[] = {
    {"io", 42, 41, kWaveshareESP32S3IoI2cHz},
    {"interlink", -1, -1, kWaveshareESP32S3InterlinkI2cHz},
};

/*
 * 1-Wire temperature probe buses.
 *
 * Each entry uses:
 *   name, signal, pin.
 *
 * name:
 *   Logical bus name used in logs/configuration.
 *
 * signal:
 *   BoardSignal identifier used to bind the physical bus to a domain role.
 *
 * pin:
 *   GPIO carrying the 1-Wire data line.
 *
 * NVS behavior:
 *   Not stored in NVS. The OneWireBus objects are constructed from these pins
 *   before module config is loaded, so the compiled pin values always apply.
 *   DS18B20 ROM addresses may be stored separately by the IO module, but the
 *   bus GPIO pins are not.
 */
inline constexpr OneWireBusSpec kWaveshareESP32S3OneWire[] = {
    {"temp_probe_1", BoardSignal::TempProbe1, 20}, // Water DS18B20 probe bus on GPIO20.
    {"temp_probe_2", BoardSignal::TempProbe2, 19}, // Air DS18B20 probe bus on GPIO19.
};

/*
 * W5500 Ethernet wiring for the Waveshare board.
 *
 * Field order:
 *   enabled, mosiPin, misoPin, sclkPin, csPin, intPin, rstPin, phyAddr,
 *   spiClockHz.
 *
 * The current Waveshare build uses GPIO13/14/15/16 with INT on GPIO12 and
 * reset on GPIO39.
 *
 * NVS behavior:
 *   The W5500 pin mapping, PHY address, and SPI clock are not stored in NVS.
 *   Only the Ethernet module "enabled" flag is persistent; when enabled, the
 *   compiled wiring from this block is used.
 */
inline constexpr EthernetW5500Spec kWaveshareESP32S3EthernetW5500{
    true,       // enabled: board exposes W5500 Ethernet.
    13,         // mosiPin: ETH_MOSI.
    14,         // misoPin: ETH_MISO.
    15,         // sclkPin: ETH_SCLK.
    16,         // csPin: ETH_CS.
    12,         // intPin: ETH_INT.
    39,         // rstPin: ETH_RST.
    1,          // phyAddr: W5500 PHY address.
    8000000U    // spiClockHz: conservative SPI clock for reliable DHCP bring-up.
};

/*
 * Local active buzzer wiring.
 *
 * Field order:
 *   enabled, pin, activeHigh.
 *
 * The Waveshare ESP32-S3 board exposes an active buzzer on GPIO46. The buzzer
 * is driven directly by HMIBuzzerModule and is intentionally not registered as
 * a generic IO output, so it remains reserved for local HMI feedback.
 *
 * NVS behavior:
 *   The GPIO and polarity are compiled hardware settings. Only the
 *   hmi/buzzer/enable module setting is persistent.
 */
inline constexpr HmiBuzzerSpec kWaveshareESP32S3HmiBuzzer{
    true,
    46,
    true
};

/*
 * IO point mapping.
 *
 * Each entry uses:
 *   name, capability, signal, pin, momentary, pulseMs.
 *
 * name:
 *   Human-readable/logical IO point name.
 *
 * capability:
 *   Electrical capability exposed by this point: DigitalIn, DigitalOut,
 *   AnalogIn, or OneWireTemp.
 *
 * signal:
 *   Stable BoardSignal used by the domain layer to bind this hardware point to
 *   a product role.
 *
 * pin:
 *   GPIO number for direct ESP32 signals. For EXIO outputs, this is the logical
 *   TCA9554 expander bit index (0..7), not an ESP32 GPIO.
 *
 * momentary / pulseMs:
 *   Set momentary=true and pulseMs to a duration when an output should be
 *   driven as a pulse instead of a latched output.
 *
 * NVS behavior:
 *   The board IO point table itself is not stored in NVS. For Waveshare, the IO
 *   module defines logical analog/digital endpoints from profile defaults and
 *   stores endpoint config such as binding_port, names, polarity, modes,
 *   calibration, momentary behavior, and pulse duration in NVS. Those persistent
 *   endpoint configs can override the profile defaults at boot, but they still
 *   refer to the compiled physical ports/channels described by this board map.
 */
inline constexpr IoPointSpec kWaveshareESP32S3IoPoints[] = {
    // flow.io digital outputs are exposed through the TCA9554 expander (EXIO1..EXIO8), not direct GPIO relays.
    {"exio1", IoCapability::DigitalOut, BoardSignal::Relay1, 0, false, 0},
    {"exio2", IoCapability::DigitalOut, BoardSignal::Relay2, 1, false, 0},
    {"exio3", IoCapability::DigitalOut, BoardSignal::Relay3, 2, false, 0},
    {"exio4", IoCapability::DigitalOut, BoardSignal::Relay4, 3, false, 0},
    {"exio5", IoCapability::DigitalOut, BoardSignal::Relay5, 4, false, 0},
    {"exio6", IoCapability::DigitalOut, BoardSignal::Relay6, 5, false, 0},
    {"exio7", IoCapability::DigitalOut, BoardSignal::Relay7, 6, false, 0},
    {"exio8", IoCapability::DigitalOut, BoardSignal::Relay8, 7, false, 0},
    {"digital_in1_water_counter", IoCapability::DigitalIn, BoardSignal::DigitalIn1, 4, false, 0},
    {"digital_in2_disinfectant_level", IoCapability::DigitalIn, BoardSignal::DigitalIn2, 5, false, 0},
    {"digital_in3_pool_level", IoCapability::DigitalIn, BoardSignal::DigitalIn3, 6, false, 0},
    {"digital_in4_unused", IoCapability::DigitalIn, BoardSignal::DigitalIn4, 7, false, 0},
    {"digital_in5_unused", IoCapability::DigitalIn, BoardSignal::DigitalIn5, 8, false, 0},
    {"digital_in6_unused", IoCapability::DigitalIn, BoardSignal::DigitalIn6, 9, false, 0},
    {"digital_in7_unused", IoCapability::DigitalIn, BoardSignal::DigitalIn7, 10, false, 0},
    {"digital_in8_unused", IoCapability::DigitalIn, BoardSignal::DigitalIn8, 11, false, 0},
    {"water_temperature_ds18b20", IoCapability::OneWireTemp, BoardSignal::TempProbe1, 20, false, 0},
    {"air_temperature_ds18b20", IoCapability::OneWireTemp, BoardSignal::TempProbe2, 19, false, 0},
#if !defined(FLOW_ENABLE_TFT_S3) || (FLOW_ENABLE_TFT_S3 == 0)
    {"gpio1_input", IoCapability::DigitalIn, BoardSignal::None, 1, false, 0},
    {"gpio1_output", IoCapability::DigitalOut, BoardSignal::None, 1, false, 0},
    {"gpio2_input", IoCapability::DigitalIn, BoardSignal::None, 2, false, 0},
    {"gpio2_output", IoCapability::DigitalOut, BoardSignal::None, 2, false, 0},
    {"gpio21_input", IoCapability::DigitalIn, BoardSignal::None, 21, false, 0},
    {"gpio21_output", IoCapability::DigitalOut, BoardSignal::None, 21, false, 0},
    {"gpio45_input", IoCapability::DigitalIn, BoardSignal::None, 45, false, 0},
    {"gpio45_output", IoCapability::DigitalOut, BoardSignal::None, 45, false, 0},
    {"gpio47_input", IoCapability::DigitalIn, BoardSignal::None, 47, false, 0},
    {"gpio47_output", IoCapability::DigitalOut, BoardSignal::None, 47, false, 0},
    {"gpio48_input", IoCapability::DigitalIn, BoardSignal::None, 48, false, 0},
    {"gpio48_output", IoCapability::DigitalOut, BoardSignal::None, 48, false, 0},
#endif
};

/*
 * Local ST7789 TFT display wiring and timing.
 *
 * TFT support is enabled by the Waveshare-ESP32-S3 environment. While enabled,
 * these pins are omitted from the generic IO point table above so the display
 * owns them exclusively.
 *
 * Field order:
 *   resX, resY, rotation, colStart, rowStart, backlightPin, csPin, dcPin,
 *   rstPin, misoPin, mosiPin, sclkPin, swapColorBytes, invertColors, spiHz,
 *   minRenderGapMs.
 *
 * resX / resY:
 *   Native display resolution in pixels.
 *
 * rotation:
 *   Panel rotation passed to the display driver.
 *
 * colStart / rowStart:
 *   Controller pixel offsets. Keep 0 unless the rendered image is shifted.
 *
 * backlightPin / csPin / dcPin / rstPin / misoPin / mosiPin / sclkPin:
 *   GPIO wiring for the TFT backlight and SPI bus. Use -1 for an unwired MISO.
 *
 * swapColorBytes / invertColors:
 *   Color-format corrections required by some ST7789 panels.
 *
 * spiHz:
 *   SPI clock used to drive the display.
 *
 * minRenderGapMs:
 *   Minimum delay between render passes to avoid spending too much CPU time on
 *   the local display.
 *
 * NVS behavior:
 *   Not stored in NVS. The display resolution, SPI pins, color flags, SPI clock,
 *   and render gap are compiled hardware settings and always apply.
 */
inline constexpr St7789DisplaySpec kWaveshareESP32S3Display{
    240,       // resX: horizontal pixels.
    320,       // resY: vertical pixels.
    1,         // rotation.
    0,         // colStart.
    0,         // rowStart.
#if defined(FLOW_ENABLE_TFT_S3) && (FLOW_ENABLE_TFT_S3 != 0)
    21,        // backlightPin: TFT_BL.
    45,        // csPin: SPI_CS.
    1,         // dcPin: TFT_DC.
    47,        // rstPin: TFT_RES.
    -1,        // misoPin: not wired for this TFT.
    2,         // mosiPin: SPI_MOSI.
    48,        // sclkPin: SPI_SCLK.
#else
    -1,        // backlightPin: TFT disabled in this environment.
    -1,        // csPin: TFT disabled in this environment.
    -1,        // dcPin: TFT disabled in this environment.
    -1,        // rstPin: TFT disabled in this environment.
    -1,        // misoPin: not wired for this TFT.
    -1,        // mosiPin: TFT disabled in this environment.
    -1,        // sclkPin: TFT disabled in this environment.
#endif
    false,     // swapColorBytes.
    true,      // invertColors.
    40000000U, // spiHz.
    80         // minRenderGapMs.
};

/*
 * Local supervisor input pins.
 *
 * Field order:
 *   pirPin, pirDebounceMs, pirActiveHigh, factoryResetPin,
 *   factoryResetDebounceMs.
 *
 * pirPin:
 *   Optional local motion sensor GPIO. No PIR sensor is assigned on this board.
 *
 * pirDebounceMs / pirActiveHigh:
 *   Debounce time and polarity for the PIR input.
 *
 * factoryResetPin:
 *   Optional GPIO for a local factory-reset button. Use -1 when no button is
 *   wired.
 *
 * factoryResetDebounceMs:
 *   Debounce time for the factory-reset input if it is enabled.
 *
 * NVS behavior:
 *   TFTModuleS3 does not consume these direct-PIR settings; it reads a logical
 *   IOModule input selected by its persistent "motion_io_id" configuration.
 */
inline constexpr SupervisorInputSpec kWaveshareESP32S3Inputs{
    -1,   // pirPin disabled; was GPIO11 motion sensor for TFT wake.
    120,  // pirDebounceMs.
    true, // pirActiveHigh.
    -1,   // factoryResetPin: not assigned on this board.
    40    // factoryResetDebounceMs.
};

/*
 * Downstream firmware-update control pins.
 *
 * Field order:
 *   flowIoEnablePin, flowIoBootPin, nextionRebootPin, nextionUploadBaud.
 *
 * This Waveshare profile is a local Waveshare build and does not control a
 * separate downstream FlowIO target, so the control pins are disabled with -1.
 * Set these GPIOs only if this board is wired to reset/boot another controller
 * or reboot an external Nextion panel.
 *
 * NVS behavior:
 *   Not stored in NVS. These update-control pins and the Nextion upload baud
 *   are compiled values.
 */
inline constexpr SupervisorUpdateSpec kWaveshareESP32S3Update{
    -1,      // flowIoEnablePin: local flow.io build has no downstream flow.io target.
    -1,      // flowIoBootPin.
    -1,      // nextionRebootPin.
    115200U  // nextionUploadBaud.
};

/*
 * Supervisor extension block for this board.
 *
 * Field order:
 *   display, inputs, update.
 *
 * This groups the TFT definition, local input pins, and downstream update pins
 * so BoardSpec can expose them through board.supervisor.
 *
 * NVS behavior:
 *   Not stored in NVS as a block. See the display, inputs, and update comments
 *   above for the only field-level default that is copied into persistent
 *   config.
 */
inline constexpr SupervisorBoardSpec kWaveshareESP32S3Supervisor{
    kWaveshareESP32S3Display,
    kWaveshareESP32S3Inputs,
    kWaveshareESP32S3Update
};

/*
 * Complete board descriptor consumed by BoardCatalog.
 *
 * Field order:
 *   name, mdnsHost, uarts, uartCount, i2cBuses, i2cCount, oneWireBuses,
 *   oneWireCount, ioPoints, ioPointCount, ioCapacity, mqttCapacity,
 *   mqttBuffers, haCapacity, supervisor, provisioning, ethernetW5500, hmiBuzzer.
 *
 * name:
 *   Board identifier exposed to logs/runtime.
 *
 * mdnsHost:
 *   Default mDNS host name for this firmware.
 *
 * table/count pairs:
 *   Each hardware table is passed together with its element count. Keep the
 *   sizeof expressions in sync with the matching table when renaming entries.
 *
 * supervisor:
 *   Points to the local TFT/input/update block defined above.
 *
 * provisioning:
 *   Left as the default empty policy here.
 *
 * ethernetW5500:
 *   Points to the W5500 mapping defined above.
 *
 * NVS behavior:
 *   The BoardSpec descriptor and its table/count pointers are not stored in NVS.
 *   Only selected module-level settings derived from it are persistent, as
 *   documented in the individual sections above.
 */
inline constexpr BoardSpec kWaveshareESP32S3{
    "WaveshareESP32S3",
    "flowio",
    kWaveshareESP32S3Uarts,
    (uint8_t)(sizeof(kWaveshareESP32S3Uarts) / sizeof(kWaveshareESP32S3Uarts[0])),
    kWaveshareESP32S3I2c,
    (uint8_t)(sizeof(kWaveshareESP32S3I2c) / sizeof(kWaveshareESP32S3I2c[0])),
    kWaveshareESP32S3OneWire,
    (uint8_t)(sizeof(kWaveshareESP32S3OneWire) / sizeof(kWaveshareESP32S3OneWire[0])),
    kWaveshareESP32S3IoPoints,
    (uint8_t)(sizeof(kWaveshareESP32S3IoPoints) / sizeof(kWaveshareESP32S3IoPoints[0])),
    kWaveshareESP32S3IoCapacity,
    kWaveshareESP32S3MqttCapacity,
    kWaveshareESP32S3MqttBuffers,
    kWaveshareESP32S3HaCapacity,
    &kWaveshareESP32S3Supervisor,
    {},
    &kWaveshareESP32S3EthernetW5500,
    &kWaveshareESP32S3HmiBuzzer
};

}  // namespace BoardProfiles
