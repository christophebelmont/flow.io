#pragma once

#include "Domain/DomainSpec.h"
#include "Domain/Pool/PoolDefaults.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/PoolDeviceModule/PoolDeviceModule.h"

namespace PoolDomain {

inline constexpr DomainSlotPreset kDomainSlots[] = {
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    {PoolIds::SensorOrp, IO_SLOT_ANALOG_INPUT, "ORP", "ORP", 0, true, 0},
    {PoolIds::SensorPh, IO_SLOT_ANALOG_INPUT, "pH", "pH", 1, true, 0},
    {PoolIds::SensorPsi, IO_SLOT_ANALOG_INPUT, "PSI", "PSI", 2, true, 0},
    {PoolIds::SensorSpareAnalog, IO_SLOT_ANALOG_INPUT, "Spare", "Spare", 3, true, 0},
    {PoolIds::SensorWaterTemp, IO_SLOT_ANALOG_INPUT, "Water Temperature", "Water Temperature", 4, true, 0},
    {PoolIds::SensorAirTemp, IO_SLOT_ANALOG_INPUT, "Air Temperature", "Air Temperature", 5, true, 0},
    {PoolIds::SensorCurrent, IO_SLOT_ANALOG_INPUT, "Current", "Current", 6, true, 0},
    {PoolIds::SensorVoltage, IO_SLOT_ANALOG_INPUT, "Voltage", "Voltage", 7, true, 0},
    {PoolIds::SensorPir, IO_SLOT_DIGITAL_INPUT, "PIR", "PIR", 8, true, 0},
    {PoolIds::SensorPhLevel, IO_SLOT_DIGITAL_INPUT, "pH Level", "pH Level", 9, true, 0},
    {PoolIds::SensorChlorineLevel, IO_SLOT_DIGITAL_INPUT, "Chlorine Level", "Chlorine Level", 10, true, 0},
    {PoolIds::SensorPoolLevel, IO_SLOT_DIGITAL_INPUT, "Pool Level", "Pool Level", 11, true, 0},
    {PoolIds::SensorWaterMeter, IO_SLOT_DIGITAL_INPUT, "Water Meter", "Water Meter", 12, true, 0},
    {PoolIds::ActuatorFiltrationPump, IO_SLOT_DIGITAL_OUTPUT, "io_flt_pmp", "Filtration Pump", 0, true, 0},
    {PoolIds::ActuatorPhPump, IO_SLOT_DIGITAL_OUTPUT, "io_ph_pmp", "pH Pump", 1, true, 0},
    {PoolIds::ActuatorChlorinePump, IO_SLOT_DIGITAL_OUTPUT, "io_chl_pmp", "Chlorine Pump", 2, true, 0},
    {PoolIds::ActuatorRobot, IO_SLOT_DIGITAL_OUTPUT, "io_robot", "Robot", 3, true, 0},
    {PoolIds::ActuatorFillPump, IO_SLOT_DIGITAL_OUTPUT, "io_fill_pmp", "Remplissage", 4, true, 0},
    {PoolIds::ActuatorChlorineGenerator, IO_SLOT_DIGITAL_OUTPUT, "io_chl_gen", "Electrolyse", 5, true, 0},
    {PoolIds::ActuatorLights, IO_SLOT_DIGITAL_OUTPUT, "io_lights", "Lights", 6, true, 0},
    {PoolIds::ActuatorWaterHeater, IO_SLOT_DIGITAL_OUTPUT, "io_wat_htr", "Water Heater", 7, true, 0},
#else
    {PoolIds::SensorOrp, IO_SLOT_ANALOG_INPUT, "ORP", "ORP", 0, true, 0},
    {PoolIds::SensorPh, IO_SLOT_ANALOG_INPUT, "pH", "pH", 1, true, 0},
    {PoolIds::SensorPsi, IO_SLOT_ANALOG_INPUT, "PSI", "PSI", 2, true, 0},
    {PoolIds::SensorSpareAnalog, IO_SLOT_ANALOG_INPUT, "Spare", "Spare", 3, true, 0},
    {PoolIds::SensorWaterTemp, IO_SLOT_ANALOG_INPUT, "Water Temperature", "Water Temperature", 4, true, 0},
    {PoolIds::SensorAirTemp, IO_SLOT_ANALOG_INPUT, "Air Temperature", "Air Temperature", 5, true, 0},
    {PoolIds::SensorPoolLevel, IO_SLOT_DIGITAL_INPUT, "Pool Level", "Pool Level", 6, true, 0},
    {PoolIds::SensorPhLevel, IO_SLOT_DIGITAL_INPUT, "pH Level", "pH Level", 7, true, 0},
    {PoolIds::SensorChlorineLevel, IO_SLOT_DIGITAL_INPUT, "Chlorine Level", "Chlorine Level", 8, true, 0},
    {PoolIds::SensorWaterCounter, IO_SLOT_DIGITAL_INPUT, "Water Counter", "Water Counter", 9, true, 0},
    {PoolIds::ActuatorFiltrationPump, IO_SLOT_DIGITAL_OUTPUT, "io_flt_pmp", "Filtration Pump", 0, true, 0},
    {PoolIds::ActuatorPhPump, IO_SLOT_DIGITAL_OUTPUT, "io_ph_pmp", "pH Pump", 1, true, 0},
    {PoolIds::ActuatorChlorinePump, IO_SLOT_DIGITAL_OUTPUT, "io_chl_pmp", "Chlorine Pump", 2, true, 0},
    {PoolIds::ActuatorRobot, IO_SLOT_DIGITAL_OUTPUT, "io_robot", "Robot", 3, true, 0},
    {PoolIds::ActuatorFillPump, IO_SLOT_DIGITAL_OUTPUT, "io_fill_pmp", "Fill Pump", 4, true, 0},
    {PoolIds::ActuatorChlorineGenerator, IO_SLOT_DIGITAL_OUTPUT, "io_chl_gen", "Chlorine Generator", 5, true, 0},
    {PoolIds::ActuatorLights, IO_SLOT_DIGITAL_OUTPUT, "io_lights", "Lights", 6, true, 0},
    {PoolIds::ActuatorWaterHeater, IO_SLOT_DIGITAL_OUTPUT, "io_wat_htr", "Water Heater", 7, true, 0},
#endif
};

inline constexpr DomainIoSlotBinding kDomainIoSlots[] = {
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    {PoolIds::SensorOrp, analogInputSlot(0)},
    {PoolIds::SensorPh, analogInputSlot(1)},
    {PoolIds::SensorPsi, analogInputSlot(2)},
    {PoolIds::SensorSpareAnalog, analogInputSlot(3)},
    {PoolIds::SensorWaterTemp, analogInputSlot(4)},
    {PoolIds::SensorAirTemp, analogInputSlot(5)},
    {PoolIds::SensorCurrent, analogInputSlot(6)},
    {PoolIds::SensorVoltage, analogInputSlot(7)},
    {PoolIds::SensorPir, digitalInputSlot(8)},
    {PoolIds::SensorPhLevel, digitalInputSlot(9)},
    {PoolIds::SensorChlorineLevel, digitalInputSlot(10)},
    {PoolIds::SensorPoolLevel, digitalInputSlot(11)},
    {PoolIds::SensorWaterMeter, digitalInputSlot(12)},
    {PoolIds::ActuatorFiltrationPump, digitalOutputSlot(0)},
    {PoolIds::ActuatorPhPump, digitalOutputSlot(1)},
    {PoolIds::ActuatorChlorinePump, digitalOutputSlot(2)},
    {PoolIds::ActuatorRobot, digitalOutputSlot(3)},
    {PoolIds::ActuatorFillPump, digitalOutputSlot(4)},
    {PoolIds::ActuatorChlorineGenerator, digitalOutputSlot(5)},
    {PoolIds::ActuatorLights, digitalOutputSlot(6)},
    {PoolIds::ActuatorWaterHeater, digitalOutputSlot(7)},
#else
    {PoolIds::SensorOrp, analogInputSlot(0)},
    {PoolIds::SensorPh, analogInputSlot(1)},
    {PoolIds::SensorPsi, analogInputSlot(2)},
    {PoolIds::SensorSpareAnalog, analogInputSlot(3)},
    {PoolIds::SensorWaterTemp, analogInputSlot(4)},
    {PoolIds::SensorAirTemp, analogInputSlot(5)},
    {PoolIds::SensorPoolLevel, digitalInputSlot(0)},
    {PoolIds::SensorPhLevel, digitalInputSlot(1)},
    {PoolIds::SensorChlorineLevel, digitalInputSlot(2)},
    {PoolIds::SensorWaterCounter, digitalInputSlot(3)},
    {PoolIds::ActuatorFiltrationPump, digitalOutputSlot(0)},
    {PoolIds::ActuatorPhPump, digitalOutputSlot(1)},
    {PoolIds::ActuatorChlorinePump, digitalOutputSlot(2)},
    {PoolIds::ActuatorRobot, digitalOutputSlot(3)},
    {PoolIds::ActuatorFillPump, digitalOutputSlot(4)},
    {PoolIds::ActuatorChlorineGenerator, digitalOutputSlot(5)},
    {PoolIds::ActuatorLights, digitalOutputSlot(6)},
    {PoolIds::ActuatorWaterHeater, digitalOutputSlot(7)},
#endif
};

inline constexpr PoolDevicePreset kPoolDevices[] = {
    {PoolIds::DeviceFiltrationPump, PoolIds::ActuatorFiltrationPump, "io_flt_pmp", "Filtration Pump", "mdi:pool", POOL_DEVICE_FILTRATION, 0.0f, 0.0f, 0.0f, POOL_DEVICE_INVALID, 0},
    {PoolIds::DevicePhPump, PoolIds::ActuatorPhPump, "io_ph_pmp", "pH Pump", "mdi:beaker-outline", POOL_DEVICE_PERISTALTIC, PoolDefaults::PeristalticFlowLPerHour,
     PoolDefaults::PeristalticTankCapacityMl, PoolDefaults::PeristalticTankInitialMl, PoolIds::DeviceFiltrationPump,
     PoolDefaults::DosePumpMaxUptimeDaySec},
    {PoolIds::DeviceChlorinePump, PoolIds::ActuatorChlorinePump, "io_chl_pmp", "Chlorine Pump", "mdi:water-outline", POOL_DEVICE_PERISTALTIC, PoolDefaults::PeristalticFlowLPerHour,
     PoolDefaults::PeristalticTankCapacityMl, PoolDefaults::PeristalticTankInitialMl, PoolIds::DeviceFiltrationPump,
     PoolDefaults::DosePumpMaxUptimeDaySec},
    {PoolIds::DeviceRobot, PoolIds::ActuatorRobot, "io_robot", "Robot", "mdi:robot-vacuum", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, PoolIds::DeviceFiltrationPump, 0},
    {PoolIds::DeviceFillPump, PoolIds::ActuatorFillPump, "io_fill_pmp", "Fill Pump", "mdi:waves-arrow-up", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, POOL_DEVICE_INVALID,
     PoolDefaults::FillPumpMaxUptimeDaySec},
    {PoolIds::DeviceChlorineGenerator, PoolIds::ActuatorChlorineGenerator, "io_chl_gen", "Chlorine Generator", "mdi:flash", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     PoolIds::DeviceFiltrationPump, PoolDefaults::ChlorineGeneratorMaxUptimeDaySec},
    {PoolIds::DeviceLights, PoolIds::ActuatorLights, "io_lights", "Lights", "mdi:lightbulb", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, POOL_DEVICE_INVALID, 0},
    {PoolIds::DeviceWaterHeater, PoolIds::ActuatorWaterHeater, "io_wat_htr", "Water Heater", "mdi:water-boiler", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     POOL_DEVICE_INVALID, 0},
};

inline constexpr DomainSpec kPoolDomain{
    "Pool",
    kDomainSlots,
    (uint8_t)(sizeof(kDomainSlots) / sizeof(kDomainSlots[0])),
    kDomainIoSlots,
    (uint8_t)(sizeof(kDomainIoSlots) / sizeof(kDomainIoSlots[0])),
    kPoolDevices,
    (uint8_t)(sizeof(kPoolDevices) / sizeof(kPoolDevices[0])),
    &PoolDefaults::kLogicDefaults,
    nullptr
};

}  // namespace PoolDomain
