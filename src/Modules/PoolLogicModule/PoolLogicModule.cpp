/**
 * @file PoolLogicModule.cpp
 * @brief Facade translation unit for PoolLogicModule.
 *
 * Architecture: PoolLogicModule keeps a single public facade and splits its
 * implementation across Lifecycle / Scheduler / Control / Runtime / Commands
 * translation units.
 */

#include "PoolLogicModule.h"

#if 0
// Config-doc generation compatibility anchor:
// the generator keys runtime moduleName aliases by translation-unit stem.
namespace {
static constexpr const char* kCfgModuleModes = "poollogic/modes";
static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
static constexpr const char* kCfgModuleSafety = "poollogic/safety";
static constexpr const char* kCfgModuleRegulation = "poollogic/regulation";
static constexpr const char* kCfgModulePh = "poollogic/ph";
static constexpr const char* kCfgModuleChlorine = "poollogic/chlorine";
static constexpr const char* kCfgModuleSwg = "poollogic/swg";
static constexpr const char* kCfgModuleO2 = "poollogic/o2";
static constexpr const char* kCfgModuleDevices = "poollogic/devices";
static constexpr const char* kCfgModuleHeater = "poollogic/heater";
static constexpr const char* kCfgModuleRobot = "poollogic/robot";
static constexpr const char* kCfgModuleRefill = "poollogic/refill";
}

static void poolLogicCfgDocsAnchor_(PoolLogicModule& self)
{
    self.enabledVar_.moduleName = kCfgModuleModes;
    self.autoModeVar_.moduleName = kCfgModuleModes;
    self.winterModeVar_.moduleName = kCfgModuleModes;
    self.phAutoModeVar_.moduleName = kCfgModulePh;
    self.orpAutoModeVar_.moduleName = kCfgModuleChlorine;
    self.heaterAutoModeVar_.moduleName = kCfgModuleHeater;
    self.phDosePlusVar_.moduleName = kCfgModulePh;
    self.disinfectionTypeVar_.moduleName = kCfgModuleModes;
    self.tempLowVar_.moduleName = kCfgModuleFiltration;
    self.tempSetpointVar_.moduleName = kCfgModuleFiltration;
    self.startMinVar_.moduleName = kCfgModuleFiltration;
    self.stopMaxVar_.moduleName = kCfgModuleFiltration;
    self.calcStartVar_.moduleName = kCfgModuleFiltration;
    self.calcStopVar_.moduleName = kCfgModuleFiltration;

    self.phIdVar_.moduleName = kCfgModuleSensors;
    self.orpIdVar_.moduleName = kCfgModuleSensors;
    self.psiIdVar_.moduleName = kCfgModuleSensors;
    self.waterTempIdVar_.moduleName = kCfgModuleSensors;
    self.airTempIdVar_.moduleName = kCfgModuleSensors;
    self.levelIdVar_.moduleName = kCfgModuleSensors;
    self.phLevelIdVar_.moduleName = kCfgModuleSensors;
    self.chlorineLevelIdVar_.moduleName = kCfgModuleSensors;

    self.psiLowVar_.moduleName = kCfgModuleSafety;
    self.psiHighVar_.moduleName = kCfgModuleSafety;
    self.winterStartVar_.moduleName = kCfgModuleSafety;
    self.freezeHoldVar_.moduleName = kCfgModuleSafety;
    self.secureElectroVar_.moduleName = kCfgModuleSwg;
    self.phSetpointVar_.moduleName = kCfgModulePh;
    self.orpSetpointVar_.moduleName = kCfgModuleChlorine;
    self.heaterSetpointVar_.moduleName = kCfgModuleHeater;
    self.phKpVar_.moduleName = kCfgModulePh;
    self.phKiVar_.moduleName = kCfgModulePh;
    self.phKdVar_.moduleName = kCfgModulePh;
    self.orpKpVar_.moduleName = kCfgModuleChlorine;
    self.orpKiVar_.moduleName = kCfgModuleChlorine;
    self.orpKdVar_.moduleName = kCfgModuleChlorine;
    self.phWindowMsVar_.moduleName = kCfgModulePh;
    self.orpWindowMsVar_.moduleName = kCfgModuleChlorine;
    self.pidMinOnMsVar_.moduleName = kCfgModuleRegulation;
    self.pidSampleMsVar_.moduleName = kCfgModuleRegulation;

    self.psiDelayVar_.moduleName = kCfgModuleSafety;
    self.delayPidsVar_.moduleName = kCfgModuleRegulation;
    self.delayElectroVar_.moduleName = kCfgModuleSwg;
    self.robotDelayVar_.moduleName = kCfgModuleRobot;
    self.robotDurationVar_.moduleName = kCfgModuleRobot;
    self.fillingMinOnVar_.moduleName = kCfgModuleRefill;

    self.filtrationDeviceVar_.moduleName = kCfgModuleDevices;
    self.swgDeviceVar_.moduleName = kCfgModuleDevices;
    self.robotDeviceVar_.moduleName = kCfgModuleDevices;
    self.fillingDeviceVar_.moduleName = kCfgModuleDevices;
    self.phPumpDeviceVar_.moduleName = kCfgModuleDevices;
    self.orpPumpDeviceVar_.moduleName = kCfgModuleDevices;
    self.heaterDeviceVar_.moduleName = kCfgModuleDevices;
}
#endif
