#pragma once

#include "Domain/Pool/PoolIds.h"
#include "Modules/IOModule/IODrivers/Ads1115Driver.h"
#include "Modules/IOModule/IOModuleTypes.h"

namespace Profiles {
namespace Waveshare {
namespace IoLayout {

enum : IOExpanderId {
    ExpanderTca9554 = 0,
    ExpanderMcp23017 = 1,
};

inline constexpr IOExpanderSpec kExpanders[] = {
    {ExpanderTca9554, IO_EXPANDER_KIND_TCA9554, true, 0x20, 0x00, false},
    {ExpanderMcp23017, IO_EXPANDER_KIND_MCP23017, true, 0x21, 0x00, false},
};

enum : PhysicalPortId {
    PortAdsInternal0 = 100, // ADS1115 interne, entree single-ended A0.
    PortAdsInternal1 = 101, // ADS1115 interne, entree single-ended A1.
    PortAdsInternal2 = 102, // ADS1115 interne, entree single-ended A2.
    PortAdsInternal3 = 103, // ADS1115 interne, entree single-ended A3.
    PortAdsExternal0 = 110, // ADS1115 externe, paire differentielle 0.
    PortAdsExternal1 = 111, // ADS1115 externe, paire differentielle 1.
    PortOneWireWater = 120, // DS18B20 eau sur GPIO20.
    PortOneWireAir = 121, // DS18B20 air sur GPIO19.
    PortSht40Temp = 130, // SHT40: temperature.
    PortSht40Humidity = 131, // SHT40: humidite.
    PortBmp280Temp = 132, // BMP280: temperature.
    PortBmp280Pressure = 133, // BMP280: pression.
    PortBme688Temp = 134, // BME688: temperature.
    PortBme688Humidity = 135, // BME688: humidite.
    PortBme688Pressure = 136, // BME688: pression.
    PortBme688Gas = 137, // BME688: resistance gaz.
    PortIna226ShuntMv = 138, // INA226: tension shunt (mV).
    PortIna226BusV = 139, // INA226: tension bus (V).
    PortIna226CurrentMa = 140, // INA226: courant (mA).
    PortIna226PowerMw = 141, // INA226: puissance (mW).
    PortIna226LoadV = 142, // INA226: tension charge (V).
    PortDin0 = 200, // DIN0.
    PortDin1 = 201, // DIN1.
    PortDin2 = 202, // DIN2.
    PortDin3 = 203, // DIN3.
    PortDin4 = 204, // DIN4.
    PortDin5 = 205, // DIN5.
    PortDin6 = 206, // DIN6.
    PortDin7 = 207, // DIN7.
    PortExio1 = 300, // TCA9554 sortie bit 0.
    PortExio2 = 301, // TCA9554 sortie bit 1.
    PortExio3 = 302, // TCA9554 sortie bit 2.
    PortExio4 = 303, // TCA9554 sortie bit 3.
    PortExio5 = 304, // TCA9554 sortie bit 4.
    PortExio6 = 305, // TCA9554 sortie bit 5.
    PortExio7 = 306, // TCA9554 sortie bit 6.
    PortExio8 = 307, // TCA9554 sortie bit 7.
    PortMcpInGpa0 = 220, // MCP23017 entree GPA0.
    PortMcpInGpa1 = 221, // MCP23017 entree GPA1, libre.
    PortMcpInGpa2 = 222, // MCP23017 entree GPA2, libre.
    PortMcpInGpa3 = 223, // MCP23017 entree GPA3.
    PortMcpInGpa4 = 224, // MCP23017 entree GPA4.
    PortMcpInGpa5 = 225, // MCP23017 entree GPA5.
    PortMcpInGpa6 = 226, // MCP23017 entree GPA6.
    PortGpio1Input = 240,
    PortGpio2Input = 241,
    PortGpio21Input = 242,
    PortGpio45Input = 243,
    PortGpio47Input = 244,
    PortGpio48Input = 245,
    PortMcpOutGpb0 = 320, // MCP23017 sortie GPB0.
    PortMcpOutGpb1 = 321, // MCP23017 sortie GPB1.
    PortMcpOutGpb2 = 322, // MCP23017 sortie GPB2.
    PortMcpOutGpb3 = 323, // MCP23017 sortie GPB3.
    PortMcpOutGpb4 = 324, // MCP23017 sortie GPB4.
    PortMcpOutGpb5 = 325, // MCP23017 sortie GPB5.
    PortMcpOutGpb6 = 326, // MCP23017 sortie GPB6.
    PortMcpOutGpb7 = 327, // MCP23017 sortie GPB7.
    PortGpio1Output = 340,
    PortGpio2Output = 341,
    PortGpio21Output = 342,
    PortGpio45Output = 343,
    PortGpio47Output = 344,
    PortGpio48Output = 345,
};

inline constexpr IOBindingPortSpec kBindingPorts[] = {
    // {portId, kind, channel, expanderId, boardLabel}
    {PortAdsInternal0, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 0, 0, "ADS INT A0"},
    {PortAdsInternal1, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 1, 0, "ADS INT A1"},
    {PortAdsInternal2, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 2, 0, "ADS INT A2"},
    {PortAdsInternal3, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 3, 0, "ADS INT A3"},
    {PortAdsExternal0, IO_PORT_KIND_ADS_EXTERNAL_DIFF, 0, 0, "ADS EXT A0-A1"},
    {PortAdsExternal1, IO_PORT_KIND_ADS_EXTERNAL_DIFF, 1, 0, "ADS EXT A2-A3"},
    {PortOneWireWater, IO_PORT_KIND_DS18_WATER, 20, 0, "GPIO20 (OneWire)"},
    {PortOneWireAir, IO_PORT_KIND_DS18_AIR, 19, 0, "GPIO19 (OneWire)"},
    {PortSht40Temp, IO_PORT_KIND_SHT40, 0, 0, "SHT40 Temperature"},
    {PortSht40Humidity, IO_PORT_KIND_SHT40, 1, 0, "SHT40 Humidity"},
    {PortBmp280Temp, IO_PORT_KIND_BMP280, 0, 0, "BMP280 Temperature"},
    {PortBmp280Pressure, IO_PORT_KIND_BMP280, 1, 0, "BMP280 Pressure"},
    {PortBme688Temp, IO_PORT_KIND_BME680, 0, 0, "BME688 Temperature"},
    {PortBme688Humidity, IO_PORT_KIND_BME680, 1, 0, "BME688 Humidity"},
    {PortBme688Pressure, IO_PORT_KIND_BME680, 2, 0, "BME688 Pressure"},
    {PortBme688Gas, IO_PORT_KIND_BME680, 3, 0, "BME688 Gas"},
    {PortIna226ShuntMv, IO_PORT_KIND_INA226, 0, 0, "INA226 Shunt"},
    {PortIna226BusV, IO_PORT_KIND_INA226, 1, 0, "INA226 Bus Voltage"},
    {PortIna226CurrentMa, IO_PORT_KIND_INA226, 2, 0, "INA226 Current"},
    {PortIna226PowerMw, IO_PORT_KIND_INA226, 3, 0, "INA226 Power"},
    {PortIna226LoadV, IO_PORT_KIND_INA226, 4, 0, "INA226 Load Voltage"},
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    {PortDin0, IO_PORT_KIND_GPIO_INPUT, 4, 0, "GPIO04"},
    {PortDin1, IO_PORT_KIND_GPIO_INPUT, 5, 0, "GPIO05"},
    {PortDin2, IO_PORT_KIND_GPIO_INPUT, 6, 0, "GPIO06"},
    {PortDin3, IO_PORT_KIND_GPIO_INPUT, 7, 0, "GPIO07"},
    {PortDin4, IO_PORT_KIND_GPIO_INPUT, 8, 0, "GPIO08"},
    {PortDin5, IO_PORT_KIND_GPIO_INPUT, 9, 0, "GPIO09"},
    {PortDin6, IO_PORT_KIND_GPIO_INPUT, 10, 0, "GPIO10"},
    {PortDin7, IO_PORT_KIND_GPIO_INPUT, 11, 0, "GPIO11"},
#else
    {PortDin0, IO_PORT_KIND_GPIO_INPUT, 4, 0, "GPIO04"},
    {PortDin1, IO_PORT_KIND_GPIO_INPUT, 5, 0, "GPIO05"},
    {PortDin2, IO_PORT_KIND_GPIO_INPUT, 6, 0, "GPIO06"},
    {PortDin3, IO_PORT_KIND_GPIO_INPUT, 7, 0, "GPIO07"},
    {PortDin4, IO_PORT_KIND_GPIO_INPUT, 8, 0, "GPIO08"},
    {PortDin5, IO_PORT_KIND_GPIO_INPUT, 9, 0, "GPIO09"},
    {PortDin6, IO_PORT_KIND_GPIO_INPUT, 10, 0, "GPIO10"},
    {PortDin7, IO_PORT_KIND_GPIO_INPUT, 11, 0, "GPIO11"},
#endif
    {PortMcpInGpa0, IO_PORT_KIND_MCP23017_INPUT, 0, ExpanderMcp23017, "GPA0"},
    {PortMcpInGpa1, IO_PORT_KIND_MCP23017_INPUT, 1, ExpanderMcp23017, "GPA1"},
    {PortMcpInGpa2, IO_PORT_KIND_MCP23017_INPUT, 2, ExpanderMcp23017, "GPA2"},
    {PortMcpInGpa3, IO_PORT_KIND_MCP23017_INPUT, 3, ExpanderMcp23017, "GPA3"},
    {PortMcpInGpa4, IO_PORT_KIND_MCP23017_INPUT, 4, ExpanderMcp23017, "GPA4"},
    {PortMcpInGpa5, IO_PORT_KIND_MCP23017_INPUT, 5, ExpanderMcp23017, "GPA5"},
    {PortMcpInGpa6, IO_PORT_KIND_MCP23017_INPUT, 6, ExpanderMcp23017, "GPA6"},
#if !defined(FLOW_ENABLE_TFT_S3) || (FLOW_ENABLE_TFT_S3 == 0)
    {PortGpio1Input, IO_PORT_KIND_GPIO_INPUT, 1, 0, "GPIO01"},
    {PortGpio2Input, IO_PORT_KIND_GPIO_INPUT, 2, 0, "GPIO02"},
    {PortGpio21Input, IO_PORT_KIND_GPIO_INPUT, 21, 0, "GPIO21"},
    {PortGpio45Input, IO_PORT_KIND_GPIO_INPUT, 45, 0, "GPIO45"},
    {PortGpio47Input, IO_PORT_KIND_GPIO_INPUT, 47, 0, "GPIO47"},
    {PortGpio48Input, IO_PORT_KIND_GPIO_INPUT, 48, 0, "GPIO48"},
#endif
    {PortExio1, IO_PORT_KIND_TCA9554_OUTPUT, 0, ExpanderTca9554, "EXIO1"},
    {PortExio2, IO_PORT_KIND_TCA9554_OUTPUT, 1, ExpanderTca9554, "EXIO2"},
    {PortExio3, IO_PORT_KIND_TCA9554_OUTPUT, 2, ExpanderTca9554, "EXIO3"},
    {PortExio4, IO_PORT_KIND_TCA9554_OUTPUT, 3, ExpanderTca9554, "EXIO4"},
    {PortExio5, IO_PORT_KIND_TCA9554_OUTPUT, 4, ExpanderTca9554, "EXIO5"},
    {PortExio6, IO_PORT_KIND_TCA9554_OUTPUT, 5, ExpanderTca9554, "EXIO6"},
    {PortExio7, IO_PORT_KIND_TCA9554_OUTPUT, 6, ExpanderTca9554, "EXIO7"},
    {PortExio8, IO_PORT_KIND_TCA9554_OUTPUT, 7, ExpanderTca9554, "EXIO8"},
    {PortMcpOutGpb0, IO_PORT_KIND_MCP23017_OUTPUT, 8, ExpanderMcp23017, "GPB0"},
    {PortMcpOutGpb1, IO_PORT_KIND_MCP23017_OUTPUT, 9, ExpanderMcp23017, "GPB1"},
    {PortMcpOutGpb2, IO_PORT_KIND_MCP23017_OUTPUT, 10, ExpanderMcp23017, "GPB2"},
    {PortMcpOutGpb3, IO_PORT_KIND_MCP23017_OUTPUT, 11, ExpanderMcp23017, "GPB3"},
    {PortMcpOutGpb4, IO_PORT_KIND_MCP23017_OUTPUT, 12, ExpanderMcp23017, "GPB4"},
    {PortMcpOutGpb5, IO_PORT_KIND_MCP23017_OUTPUT, 13, ExpanderMcp23017, "GPB5"},
    {PortMcpOutGpb6, IO_PORT_KIND_MCP23017_OUTPUT, 14, ExpanderMcp23017, "GPB6"},
    {PortMcpOutGpb7, IO_PORT_KIND_MCP23017_OUTPUT, 15, ExpanderMcp23017, "GPB7"},
#if !defined(FLOW_ENABLE_TFT_S3) || (FLOW_ENABLE_TFT_S3 == 0)
    {PortGpio1Output, IO_PORT_KIND_GPIO_OUTPUT, 1, 0, "GPIO01"},
    {PortGpio2Output, IO_PORT_KIND_GPIO_OUTPUT, 2, 0, "GPIO02"},
    {PortGpio21Output, IO_PORT_KIND_GPIO_OUTPUT, 21, 0, "GPIO21"},
    {PortGpio45Output, IO_PORT_KIND_GPIO_OUTPUT, 45, 0, "GPIO45"},
    {PortGpio47Output, IO_PORT_KIND_GPIO_OUTPUT, 47, 0, "GPIO47"},
    {PortGpio48Output, IO_PORT_KIND_GPIO_OUTPUT, 48, 0, "GPIO48"},
#endif
};

static_assert((sizeof(kExpanders) / sizeof(kExpanders[0])) <= IO_MAX_EXPANDERS,
              "Waveshare expander topology exceeds IOModule capacity");

struct AnalogRoleDefault {
    DomainSlotId domainSlot; // Besoin fonctionnel de la sonde.
    PhysicalPortId bindingPort; // Port physique associe.
    float c0; // Coefficient multiplicateur de calibration.
    float c1; // Offset de calibration.
    int32_t precision; // Precision d'affichage (nb de decimales).
};

inline constexpr AnalogRoleDefault kAnalogRoleDefaults[] = {
    // {domainSlot, bindingPort, c0, c1, precision}
    {PoolIds::SensorOrp, PortAdsInternal0, FLOW_WIRDEF_IO_A00, FLOW_WIRDEF_IO_A01, FLOW_WIRDEF_IO_A0P},
    {PoolIds::SensorPh, PortAdsInternal1, FLOW_WIRDEF_IO_A10, FLOW_WIRDEF_IO_A11, FLOW_WIRDEF_IO_A1P},
    {PoolIds::SensorPsi, PortAdsInternal2, FLOW_WIRDEF_IO_A20, FLOW_WIRDEF_IO_A21, FLOW_WIRDEF_IO_A2P},
    {PoolIds::SensorSpareAnalog, PortAdsInternal3, FLOW_WIRDEF_IO_A30, FLOW_WIRDEF_IO_A31, FLOW_WIRDEF_IO_A3P},
    {PoolIds::SensorWaterTemp, PortOneWireWater, FLOW_WIRDEF_IO_A40, FLOW_WIRDEF_IO_A41, FLOW_WIRDEF_IO_A4P},
    {PoolIds::SensorAirTemp, PortOneWireAir, FLOW_WIRDEF_IO_A50, FLOW_WIRDEF_IO_A51, FLOW_WIRDEF_IO_A5P},
    {PoolIds::SensorCurrent, PortIna226CurrentMa, 1.0f, 0.0f, 2},
    {PoolIds::SensorVoltage, PortIna226BusV, 1.0f, 0.0f, 2},
};

struct DigitalInputRoleDefault {
    DomainSlotId domainSlot; // Besoin fonctionnel de l'entree.
    PhysicalPortId bindingPort; // Port physique associe.
    uint8_t mode; // Mode de lecture (etat/counter).
    uint8_t edgeMode; // Type de front pris en compte.
    uint32_t debounceUs; // Debounce en microsecondes.
};

inline constexpr DigitalInputRoleDefault kDigitalInputRoleDefaults[] = {
    // {role, bindingPort, mode, edgeMode, debounceUs}
    {PoolIds::SensorPir, PortMcpInGpa0, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U},
    {PoolIds::SensorPhLevel, PortMcpInGpa3, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U},
    {PoolIds::SensorChlorineLevel, PortMcpInGpa4, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U},
    {PoolIds::SensorPoolLevel, PortMcpInGpa5, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U},
    {PoolIds::SensorWaterMeter, PortDin0, IO_DIGITAL_INPUT_COUNTER, IO_EDGE_RISING, 100000U},
};

struct DigitalOutputRoleDefault {
    DomainSlotId domainSlot; // Besoin fonctionnel de la sortie.
    PhysicalPortId bindingPort; // Port physique associe.
    bool activeHigh; // Polarite de commande logique.
    bool retainOnWarmReboot; // Conserve le latch expander sur reboot ESP32 chaud.
    bool momentary; // True si sortie impulsionnelle.
    uint16_t pulseMs; // Duree d'impulsion en ms.
};

inline constexpr DigitalOutputRoleDefault kDigitalOutputRoleDefaults[] = {
    // {domainSlot, bindingPort, activeHigh, retainOnWarmReboot, momentary, pulseMs}
    {PoolIds::ActuatorFiltrationPump, PortExio1, true, true, false, 0U}, // Pompe filtration.
    {PoolIds::ActuatorPhPump, PortExio2, true, false, false, 0U}, // Pompe pH.
    {PoolIds::ActuatorChlorinePump, PortExio3, true, false, false, 0U}, // Pompe chlore.
    {PoolIds::ActuatorRobot, PortExio4, true, false, false, 0U}, // Robot.
    {PoolIds::ActuatorFillPump, PortExio5, true, false, false, 0U}, // Pompe de remplissage.
    {PoolIds::ActuatorChlorineGenerator, PortExio6, true, false, false, 0U}, // Electrolyseur.
    {PoolIds::ActuatorLights, PortExio7, true, false, false, 0U}, // Eclairage.
    {PoolIds::ActuatorWaterHeater, PortExio8, true, false, false, 0U}, // Chauffage.
};

inline constexpr const AnalogRoleDefault* analogDefaultForDomainSlot(DomainSlotId domainSlot)
{
    for (const AnalogRoleDefault& entry : kAnalogRoleDefaults) {
        if (entry.domainSlot == domainSlot) return &entry;
    }
    return nullptr;
}

inline constexpr const DigitalInputRoleDefault* digitalInputDefaultForDomainSlot(DomainSlotId domainSlot)
{
    for (const DigitalInputRoleDefault& entry : kDigitalInputRoleDefaults) {
        if (entry.domainSlot == domainSlot) return &entry;
    }
    return nullptr;
}

inline constexpr bool digitalInputPortUsedByDomainRole(PhysicalPortId bindingPort)
{
    for (const DigitalInputRoleDefault& entry : kDigitalInputRoleDefaults) {
        if (entry.bindingPort == bindingPort) return true;
    }
    return false;
}

inline constexpr const DigitalOutputRoleDefault* digitalOutputDefaultForDomainSlot(DomainSlotId domainSlot)
{
    for (const DigitalOutputRoleDefault& entry : kDigitalOutputRoleDefaults) {
        if (entry.domainSlot == domainSlot) return &entry;
    }
    return nullptr;
}

inline constexpr bool bindingPortExists(PhysicalPortId bindingPort)
{
    if (bindingPort == IO_PORT_INVALID) return false;
    for (const IOBindingPortSpec& spec : kBindingPorts) {
        if (spec.portId == bindingPort) return true;
    }
    return false;
}

}  // namespace IoLayout
}  // namespace Waveshare
}  // namespace Profiles
