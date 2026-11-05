#pragma once

#include "Domain/Pool/PoolDomain.h"

namespace PoolBehaviors {

inline const DomainSlotPreset* domainSlotById(DomainSlotId id)
{
    for (uint8_t i = 0; i < PoolDomain::kPoolDomain.domainSlotCount; ++i) {
        const DomainSlotPreset& preset = PoolDomain::kPoolDomain.domainSlots[i];
        if (preset.id == id) return &preset;
    }
    return nullptr;
}

inline IoSlotId ioSlotForDomainSlot(DomainSlotId id)
{
    for (uint8_t i = 0; i < PoolDomain::kPoolDomain.domainIoSlotBindingCount; ++i) {
        const DomainIoSlotBinding& binding = PoolDomain::kPoolDomain.domainIoSlotBindings[i];
        if (binding.domainSlot == id) return binding.ioSlot;
    }
    return IO_SLOT_INVALID;
}

inline const PoolDevicePreset* poolDeviceById(PoolDeviceId id)
{
    for (uint8_t i = 0; i < PoolDomain::kPoolDomain.poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = PoolDomain::kPoolDomain.poolDevices[i];
        if (preset.id == id) return &preset;
    }
    return nullptr;
}

inline IoId ioIdForDomainSlot(DomainSlotId id)
{
    return ioIdFromSlot(ioSlotForDomainSlot(id));
}

}  // namespace PoolBehaviors
