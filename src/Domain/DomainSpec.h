#pragma once

#include "Domain/DomainTypes.h"

struct DomainSpec {
    const char* name;
    const DomainSlotPreset* domainSlots;
    uint8_t domainSlotCount;
    const DomainIoSlotBinding* domainIoSlotBindings;
    uint8_t domainIoSlotBindingCount;
    const PoolDevicePreset* poolDevices;
    uint8_t poolDeviceCount;
    const PoolLogicDefaultsSpec* poolLogicDefaults;
    void (*configurationHook)(AppContext&);
};
