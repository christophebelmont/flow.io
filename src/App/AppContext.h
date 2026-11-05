#pragma once

#include <Preferences.h>

#include "Core/ConfigStore.h"
#include "Core/I2cBus.h"
#include "Core/ModuleManager.h"
#include "Core/ServiceRegistry.h"
#include "Core/Services/II2cBus.h"

struct BoardSpec;
struct DomainSpec;
struct FirmwareProfile;
struct ProductIdentity;
struct SupervisorRuntimeOptions;

struct AppContext {
    Preferences preferences{};
    ConfigStore registry{};
    ModuleManager moduleManager{};
    ServiceRegistry services{};
    I2CBus primaryI2cBus{};
    I2cBusService primaryI2cBusService{&primaryI2cBus};
    const FirmwareProfile* profile = nullptr;
    const BoardSpec* board = nullptr;
    const DomainSpec* domain = nullptr;
    const ProductIdentity* identity = nullptr;
    const SupervisorRuntimeOptions* supervisorRuntime = nullptr;
    bool bootCompleted = false;
};
