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
static constexpr const char* kCfgModuleMode = "poollogic/mode";
static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
static constexpr const char* kCfgModulePid = "poollogic/pid";
static constexpr const char* kCfgModuleDelay = "poollogic/delay";
static constexpr const char* kCfgModuleDevice = "poollogic/device";
}

static void poolLogicCfgDocsAnchor_(PoolLogicModule& self)
{
    self.enabledVar_.moduleName = kCfgModuleMode;
    self.autoModeVar_.moduleName = kCfgModuleMode;
    self.winterModeVar_.moduleName = kCfgModuleMode;
    self.phAutoModeVar_.moduleName = kCfgModuleMode;
    self.orpAutoModeVar_.moduleName = kCfgModuleMode;
    self.heaterAutoModeVar_.moduleName = kCfgModuleMode;
    self.phDosePlusVar_.moduleName = kCfgModuleMode;
    self.electrolyseModeVar_.moduleName = kCfgModuleMode;
    self.electroRunModeVar_.moduleName = kCfgModuleMode;

    self.tempLowVar_.moduleName = kCfgModuleFiltration;
    self.tempSetpointVar_.moduleName = kCfgModuleFiltration;
    self.startMinVar_.moduleName = kCfgModuleFiltration;
    self.stopMaxVar_.moduleName = kCfgModuleFiltration;
    self.calcStartVar_.moduleName = kCfgModuleFiltration;
    self.calcStopVar_.moduleName = kCfgModuleFiltration;
    self.winterDurationVar_.moduleName = kCfgModuleFiltration;
    self.winterIntervalVar_.moduleName = kCfgModuleFiltration;

    self.phIdVar_.moduleName = kCfgModuleSensors;
    self.orpIdVar_.moduleName = kCfgModuleSensors;
    self.psiIdVar_.moduleName = kCfgModuleSensors;
    self.waterTempIdVar_.moduleName = kCfgModuleSensors;
    self.airTempIdVar_.moduleName = kCfgModuleSensors;
    self.levelIdVar_.moduleName = kCfgModuleSensors;
    self.phLevelIdVar_.moduleName = kCfgModuleSensors;
    self.chlorineLevelIdVar_.moduleName = kCfgModuleSensors;

    self.psiLowVar_.moduleName = kCfgModulePid;
    self.psiHighVar_.moduleName = kCfgModulePid;
    self.winterStartVar_.moduleName = kCfgModulePid;
    self.freezeHoldVar_.moduleName = kCfgModulePid;
    self.secureElectroVar_.moduleName = kCfgModulePid;
    self.phSetpointVar_.moduleName = kCfgModulePid;
    self.orpSetpointVar_.moduleName = kCfgModulePid;
    self.heaterSetpointVar_.moduleName = kCfgModulePid;
    self.phKpVar_.moduleName = kCfgModulePid;
    self.phKiVar_.moduleName = kCfgModulePid;
    self.phKdVar_.moduleName = kCfgModulePid;
    self.orpKpVar_.moduleName = kCfgModulePid;
    self.orpKiVar_.moduleName = kCfgModulePid;
    self.orpKdVar_.moduleName = kCfgModulePid;
    self.phWindowMsVar_.moduleName = kCfgModulePid;
    self.orpWindowMsVar_.moduleName = kCfgModulePid;
    self.pidMinOnMsVar_.moduleName = kCfgModulePid;
    self.pidSampleMsVar_.moduleName = kCfgModulePid;

    self.psiDelayVar_.moduleName = kCfgModuleDelay;
    self.delayPidsVar_.moduleName = kCfgModuleDelay;
    self.delayElectroVar_.moduleName = kCfgModuleDelay;
    self.robotDelayVar_.moduleName = kCfgModuleDelay;
    self.robotDurationVar_.moduleName = kCfgModuleDelay;
    self.fillingMinOnVar_.moduleName = kCfgModuleDelay;

    self.filtrationDeviceVar_.moduleName = kCfgModuleDevice;
    self.swgDeviceVar_.moduleName = kCfgModuleDevice;
    self.robotDeviceVar_.moduleName = kCfgModuleDevice;
    self.fillingDeviceVar_.moduleName = kCfgModuleDevice;
    self.phPumpDeviceVar_.moduleName = kCfgModuleDevice;
    self.orpPumpDeviceVar_.moduleName = kCfgModuleDevice;
    self.heaterDeviceVar_.moduleName = kCfgModuleDevice;
}
#endif
