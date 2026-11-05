#include "Profiles/FlowConnectDisplay/FlowConnectDisplayProfile.h"

#include <Arduino.h>

#include "App/AppContext.h"
#include "Board/BoardSerialMap.h"
#include "Core/NvsKeys.h"

namespace Profiles {
namespace FlowConnectDisplay {

void setupProfile(AppContext& ctx)
{
    ModuleInstances& modules = moduleInstances();

    Serial.begin(Board::SerialMap::uart0Baud());
    delay(50);

    ctx.preferences.begin(NvsKeys::FlowConnectDisplayStorageNamespace, false);
    ctx.registry.setPreferences(ctx.preferences);
    ctx.moduleManager.add(&modules.logHubModule);
    ctx.moduleManager.add(&modules.logDispatcherModule);
    ctx.moduleManager.add(&modules.logSerialSinkModule);
    ctx.moduleManager.add(&modules.eventBusModule);

    ctx.moduleManager.add(&modules.configStoreModule);
    ctx.moduleManager.add(&modules.dataStoreModule);
    ctx.moduleManager.add(&modules.wifiModule);
    ctx.moduleManager.add(&modules.wifiProvisioningModule);
    ctx.moduleManager.add(&modules.flowConnectDisplayUdpClientModule);

    if (!ctx.moduleManager.initAll(ctx.registry, ctx.services)) {
        while (true) delay(1000);
    }
}

void loopProfile(AppContext&)
{
    delay(20);
}

}  // namespace FlowConnectDisplay
}  // namespace Profiles
