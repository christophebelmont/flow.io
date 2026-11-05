#pragma once

#include "Modules/PoolDeviceModule/PoolDeviceModuleDataModel.h"

namespace PoolDeviceSlots {

inline constexpr PoolDeviceSlotDescriptor kSlots[] = {
    {"pd0", "pdm/pd0", "pd0en", "pd0dp", "pd0flh", "pd0tc", "pd0ti", "pd0mu", "pd0rt"},
    {"pd1", "pdm/pd1", "pd1en", "pd1dp", "pd1flh", "pd1tc", "pd1ti", "pd1mu", "pd1rt"},
    {"pd2", "pdm/pd2", "pd2en", "pd2dp", "pd2flh", "pd2tc", "pd2ti", "pd2mu", "pd2rt"},
    {"pd3", "pdm/pd3", "pd3en", "pd3dp", "pd3flh", "pd3tc", "pd3ti", "pd3mu", "pd3rt"},
    {"pd4", "pdm/pd4", "pd4en", "pd4dp", "pd4flh", "pd4tc", "pd4ti", "pd4mu", "pd4rt"},
    {"pd5", "pdm/pd5", "pd5en", "pd5dp", "pd5flh", "pd5tc", "pd5ti", "pd5mu", "pd5rt"},
    {"pd6", "pdm/pd6", "pd6en", "pd6dp", "pd6flh", "pd6tc", "pd6ti", "pd6mu", "pd6rt"},
    {"pd7", "pdm/pd7", "pd7en", "pd7dp", "pd7flh", "pd7tc", "pd7ti", "pd7mu", "pd7rt"},
    {"pd8", "pdm/pd8", "pd8en", "pd8dp", "pd8flh", "pd8tc", "pd8ti", "pd8mu", "pd8rt"},
    {"pd9", "pdm/pd9", "pd9en", "pd9dp", "pd9flh", "pd9tc", "pd9ti", "pd9mu", "pd9rt"},
    {"pd10", "pdm/pd10", "pd10en", "pd10dp", "pd10flh", "pd10tc", "pd10ti", "pd10mu", "pd10rt"},
    {"pd11", "pdm/pd11", "pd11en", "pd11dp", "pd11flh", "pd11tc", "pd11ti", "pd11mu", "pd11rt"},
    {"pd12", "pdm/pd12", "pd12en", "pd12dp", "pd12flh", "pd12tc", "pd12ti", "pd12mu", "pd12rt"},
    {"pd13", "pdm/pd13", "pd13en", "pd13dp", "pd13flh", "pd13tc", "pd13ti", "pd13mu", "pd13rt"},
    {"pd14", "pdm/pd14", "pd14en", "pd14dp", "pd14flh", "pd14tc", "pd14ti", "pd14mu", "pd14rt"},
    {"pd15", "pdm/pd15", "pd15en", "pd15dp", "pd15flh", "pd15tc", "pd15ti", "pd15mu", "pd15rt"},
};

static_assert((sizeof(kSlots) / sizeof(kSlots[0])) >= POOL_DEVICE_MAX,
              "PoolDevice fixed slot descriptors must cover POOL_DEVICE_MAX");

}  // namespace PoolDeviceSlots
