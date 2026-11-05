#include "Profiles/Waveshare/WaveshareProfile.h"

#include "Board/BoardCatalog.h"
#include "Core/FirmwareVersion.h"
#include "Domain/DomainCatalog.h"

namespace Profiles {
namespace Waveshare {

const FirmwareProfile& profile()
{
    static const FirmwareProfile kProfile{
        "flow.io",
        &BoardCatalog::activeBoard(),
        &DomainCatalog::pool(),
        {
            "flow.io",
            "waveshare",
            FirmwareVersion::Full,
            "rt"
        },
        nullptr,
        setupProfile,
        loopProfile
    };
    return kProfile;
}

}  // namespace Waveshare
}  // namespace Profiles
