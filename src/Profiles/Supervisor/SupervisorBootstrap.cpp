#include "Profiles/Supervisor/SupervisorProfile.h"

#include <Arduino.h>

#include "App/AppContext.h"
#include "Board/BoardSpec.h"
#include "Core/NvsKeys.h"

namespace Profiles {
namespace Supervisor {

void setupProfile(AppContext& ctx)
{
    ModuleInstances& modules = moduleInstances();

    Serial.begin(ctx.board && ctx.board->uartCount ? ctx.board->uarts[0].baud : 115200);
    delay(50);

    ctx.preferences.begin(NvsKeys::StorageNamespace, false);
    ctx.registry.setPreferences(ctx.preferences);
    ctx.moduleManager.add(&modules.logHubModule);
    ctx.moduleManager.add(&modules.logDispatcherModule);
    ctx.moduleManager.add(&modules.logSerialSinkModule);
    ctx.moduleManager.add(&modules.eventBusModule);

    ctx.moduleManager.add(&modules.configStoreModule);
    ctx.moduleManager.add(&modules.dataStoreModule);
    ctx.moduleManager.add(&modules.commandModule);
    ctx.moduleManager.add(&modules.alarmModule);
    ctx.moduleManager.add(&modules.logAlarmSinkModule);
    ctx.moduleManager.add(&modules.wifiModule);
    ctx.moduleManager.add(&modules.wifiProvisioningModule);
    ctx.moduleManager.add(&modules.timeModule);
    ctx.moduleManager.add(&modules.i2cCfgClientModule);
    ctx.moduleManager.add(&modules.webInterfaceModule);
    ctx.moduleManager.add(&modules.firmwareUpdateModule);
    ctx.moduleManager.add(&modules.supervisorHMIModule);
    ctx.moduleManager.add(&modules.systemModule);

    modules.systemMonitorModule.setModuleManager(&ctx.moduleManager);
    ctx.moduleManager.add(&modules.systemMonitorModule);

    if (!ctx.moduleManager.initAll(ctx.registry, ctx.services)) {
        while (true) delay(1000);
    }
}

void loopProfile(AppContext&)
{
    delay(20);
}

}  // namespace Supervisor
}  // namespace Profiles
