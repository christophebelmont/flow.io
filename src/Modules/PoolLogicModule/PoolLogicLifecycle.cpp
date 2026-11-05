/**
 * @file PoolLogicLifecycle.cpp
 * @brief Module lifecycle wiring for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Core/MqttTopics.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"

#include <cmath>
#include <cstring>
#include <new>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

namespace {
// PoolLogic exposes one aggregated cfg/poollogic route plus the per-branch
// routes used by HA, MQTT config sync, and tooling.
static constexpr uint8_t kPoolLogicCfgProducerId = 44;
static constexpr const char* kPoolLogicCfgTopicBase = "cfg/poollogic";
static constexpr uint8_t kCfgBranchModes = 1;
static constexpr uint8_t kCfgBranchFiltration = 2;
static constexpr uint8_t kCfgBranchSensors = 3;
static constexpr uint8_t kCfgBranchSafety = 4;
static constexpr uint8_t kCfgBranchRegulation = 5;
static constexpr uint8_t kCfgBranchPh = 6;
static constexpr uint8_t kCfgBranchChlorine = 7;
static constexpr uint8_t kCfgBranchSwg = 8;
static constexpr uint8_t kCfgBranchO2 = 9;
static constexpr uint8_t kCfgBranchDevices = 10;
static constexpr uint8_t kCfgBranchHeater = 11;
static constexpr uint8_t kCfgBranchRobot = 12;
static constexpr uint8_t kCfgBranchRefill = 13;
static constexpr uint32_t kStartupActivityStabilizeMs = 3000U;
static constexpr uint32_t kStartupActivityMaxDelayMs = 30000U;
static constexpr uint64_t kActivityMinEpoch = 1609459200ULL;
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

enum : uint16_t {
    kCfgMsgBase = 1,
    kCfgMsgModes = 2,
    kCfgMsgFiltration = 3,
    kCfgMsgSensors = 4,
    kCfgMsgSafety = 5,
    kCfgMsgRegulation = 6,
    kCfgMsgPh = 7,
    kCfgMsgChlorine = 8,
    kCfgMsgSwg = 9,
    kCfgMsgO2 = 10,
    kCfgMsgDevices = 11,
    kCfgMsgHeater = 12,
    kCfgMsgRobot = 13,
    kCfgMsgRefill = 14,
};

static constexpr MqttConfigRouteProducer::Route kPoolLogicCfgRoutes[] = {
    {kCfgMsgBase,
     {(uint8_t)ConfigModuleId::PoolLogic, ConfigBranchRef::UnknownLocalBranch},
     nullptr,
     "",
     (uint8_t)MqttPublishPriority::Normal,
     &PoolLogicModule::buildCfgBaseStatic_,
     kPoolLogicCfgTopicBase},
    {kCfgMsgModes,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchModes},
     kCfgModuleModes,
     "modes",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgFiltration,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchFiltration},
     kCfgModuleFiltration,
     "filtration",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgSensors,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchSensors},
     kCfgModuleSensors,
     "sensors",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgSafety,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchSafety},
     kCfgModuleSafety,
     "safety",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgRegulation,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchRegulation},
     kCfgModuleRegulation,
     "regulation",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgPh,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchPh},
     kCfgModulePh,
     "ph",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgChlorine,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchChlorine},
     kCfgModuleChlorine,
     "chlorine",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgSwg,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchSwg},
     kCfgModuleSwg,
     "swg",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgO2,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchO2},
     kCfgModuleO2,
     "o2",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgDevices,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchDevices},
     kCfgModuleDevices,
     "devices",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgHeater,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchHeater},
     kCfgModuleHeater,
     "heater",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgRobot,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchRobot},
     kCfgModuleRobot,
     "robot",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgRefill,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchRefill},
     kCfgModuleRefill,
     "refill",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
};
}

void PoolLogicModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::PoolLogic;
    cfgStore_ = &cfg;
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);

    // Runtime moduleName reassignment keeps the config tree grouped by branch
    // even though the variables are declared on a single facade class.
    enabledVar_.moduleName = kCfgModuleModes;
    autoModeVar_.moduleName = kCfgModuleModes;
    winterModeVar_.moduleName = kCfgModuleModes;
    phAutoModeVar_.moduleName = kCfgModulePh;
    orpAutoModeVar_.moduleName = kCfgModuleChlorine;
    heaterAutoModeVar_.moduleName = kCfgModuleHeater;
    phDosePlusVar_.moduleName = kCfgModulePh;
    disinfectionTypeVar_.moduleName = kCfgModuleModes;
    swgControlModeVar_.moduleName = kCfgModuleSwg;

    tempLowVar_.moduleName = kCfgModuleFiltration;
    tempSetpointVar_.moduleName = kCfgModuleFiltration;
    startMinVar_.moduleName = kCfgModuleFiltration;
    stopMaxVar_.moduleName = kCfgModuleFiltration;
    calcStartVar_.moduleName = kCfgModuleFiltration;
    calcStopVar_.moduleName = kCfgModuleFiltration;

    phIdVar_.moduleName = kCfgModuleSensors;
    orpIdVar_.moduleName = kCfgModuleSensors;
    psiIdVar_.moduleName = kCfgModuleSensors;
    waterTempIdVar_.moduleName = kCfgModuleSensors;
    airTempIdVar_.moduleName = kCfgModuleSensors;
    levelIdVar_.moduleName = kCfgModuleSensors;
    phLevelIdVar_.moduleName = kCfgModuleSensors;
    chlorineLevelIdVar_.moduleName = kCfgModuleSensors;

    psiLowVar_.moduleName = kCfgModuleSafety;
    psiHighVar_.moduleName = kCfgModuleSafety;
    winterStartVar_.moduleName = kCfgModuleSafety;
    freezeHoldVar_.moduleName = kCfgModuleSafety;
    secureElectroVar_.moduleName = kCfgModuleSwg;
    phSetpointVar_.moduleName = kCfgModulePh;
    orpSetpointVar_.moduleName = kCfgModuleChlorine;
    heaterSetpointVar_.moduleName = kCfgModuleHeater;
    phKpVar_.moduleName = kCfgModulePh;
    phKiVar_.moduleName = kCfgModulePh;
    phKdVar_.moduleName = kCfgModulePh;
    orpKpVar_.moduleName = kCfgModuleChlorine;
    orpKiVar_.moduleName = kCfgModuleChlorine;
    orpKdVar_.moduleName = kCfgModuleChlorine;
    phWindowMsVar_.moduleName = kCfgModulePh;
    orpWindowMsVar_.moduleName = kCfgModuleChlorine;
    pidMinOnMsVar_.moduleName = kCfgModuleRegulation;
    pidSampleMsVar_.moduleName = kCfgModuleRegulation;

    psiDelayVar_.moduleName = kCfgModuleSafety;
    delayPidsVar_.moduleName = kCfgModuleRegulation;
    delayElectroVar_.moduleName = kCfgModuleSwg;
    robotDelayVar_.moduleName = kCfgModuleRobot;
    robotDurationVar_.moduleName = kCfgModuleRobot;
    fillingMinOnVar_.moduleName = kCfgModuleRefill;

    o2PoolVolumeVar_.moduleName = kCfgModuleO2;
    o2DoseVar_.moduleName = kCfgModuleO2;
    o2MainHourVar_.moduleName = kCfgModuleO2;
    o2SplitCountVar_.moduleName = kCfgModuleO2;
    o2TempCompVar_.moduleName = kCfgModuleO2;
    o2LoadFactorVar_.moduleName = kCfgModuleO2;
    o2MinFilterRunVar_.moduleName = kCfgModuleO2;
    o2ProtocolStateVar_.moduleName = kCfgModuleO2;
    o2LastDoseDayVar_.moduleName = kCfgModuleO2;
    o2WeeklyDoneVar_.moduleName = kCfgModuleO2;
    o2PendingVar_.moduleName = kCfgModuleO2;

    filtrationDeviceVar_.moduleName = kCfgModuleDevices;
    swgDeviceVar_.moduleName = kCfgModuleDevices;
    robotDeviceVar_.moduleName = kCfgModuleDevices;
    fillingDeviceVar_.moduleName = kCfgModuleDevices;
    phPumpDeviceVar_.moduleName = kCfgModuleDevices;
    orpPumpDeviceVar_.moduleName = kCfgModuleDevices;
    heaterDeviceVar_.moduleName = kCfgModuleDevices;

    // Registration order mirrors the published config branches so init remains
    // easy to diff against the generated cfgdocs and MQTT routes.
    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchModes);

    cfg.registerVar(autoModeVar_, kCfgModuleId, kCfgBranchModes);
    cfg.registerVar(winterModeVar_, kCfgModuleId, kCfgBranchModes);
    cfg.registerVar(phAutoModeVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(orpAutoModeVar_, kCfgModuleId, kCfgBranchChlorine);
    cfg.registerVar(heaterAutoModeVar_, kCfgModuleId, kCfgBranchHeater);
    cfg.registerVar(phDosePlusVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(disinfectionTypeVar_, kCfgModuleId, kCfgBranchModes);
    cfg.registerVar(swgControlModeVar_, kCfgModuleId, kCfgBranchSwg);

    cfg.registerVar(tempLowVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(tempSetpointVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(startMinVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(stopMaxVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(calcStartVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(calcStopVar_, kCfgModuleId, kCfgBranchFiltration);

    cfg.registerVar(phIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(orpIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(psiIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(waterTempIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(airTempIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(levelIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(phLevelIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(chlorineLevelIdVar_, kCfgModuleId, kCfgBranchSensors);

    cfg.registerVar(psiLowVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(psiHighVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(winterStartVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(freezeHoldVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(secureElectroVar_, kCfgModuleId, kCfgBranchSwg);
    cfg.registerVar(phSetpointVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(orpSetpointVar_, kCfgModuleId, kCfgBranchChlorine);
    cfg.registerVar(heaterSetpointVar_, kCfgModuleId, kCfgBranchHeater);
    cfg.registerVar(phKpVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phKiVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phKdVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(orpKpVar_, kCfgModuleId, kCfgBranchChlorine);
    cfg.registerVar(orpKiVar_, kCfgModuleId, kCfgBranchChlorine);
    cfg.registerVar(orpKdVar_, kCfgModuleId, kCfgBranchChlorine);
    cfg.registerVar(phWindowMsVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(orpWindowMsVar_, kCfgModuleId, kCfgBranchChlorine);
    cfg.registerVar(pidMinOnMsVar_, kCfgModuleId, kCfgBranchRegulation);
    cfg.registerVar(pidSampleMsVar_, kCfgModuleId, kCfgBranchRegulation);

    cfg.registerVar(psiDelayVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(delayPidsVar_, kCfgModuleId, kCfgBranchRegulation);
    cfg.registerVar(delayElectroVar_, kCfgModuleId, kCfgBranchSwg);
    cfg.registerVar(robotDelayVar_, kCfgModuleId, kCfgBranchRobot);
    cfg.registerVar(robotDurationVar_, kCfgModuleId, kCfgBranchRobot);
    cfg.registerVar(fillingMinOnVar_, kCfgModuleId, kCfgBranchRefill);

    cfg.registerVar(o2PoolVolumeVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2DoseVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2MainHourVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2SplitCountVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2TempCompVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2LoadFactorVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2MinFilterRunVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2ProtocolStateVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2LastDoseDayVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2WeeklyDoneVar_, kCfgModuleId, kCfgBranchO2);
    cfg.registerVar(o2PendingVar_, kCfgModuleId, kCfgBranchO2);

    cfg.registerVar(filtrationDeviceVar_, kCfgModuleId, kCfgBranchDevices);
    cfg.registerVar(swgDeviceVar_, kCfgModuleId, kCfgBranchDevices);
    cfg.registerVar(robotDeviceVar_, kCfgModuleId, kCfgBranchDevices);
    cfg.registerVar(fillingDeviceVar_, kCfgModuleId, kCfgBranchDevices);
    cfg.registerVar(phPumpDeviceVar_, kCfgModuleId, kCfgBranchDevices);
    cfg.registerVar(orpPumpDeviceVar_, kCfgModuleId, kCfgBranchDevices);
    cfg.registerVar(heaterDeviceVar_, kCfgModuleId, kCfgBranchDevices);

    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    timeSvc_ = services.get<TimeService>(ServiceId::Time);
    schedSvc_ = services.get<TimeSchedulerService>(ServiceId::TimeScheduler);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    poolSvc_ = services.get<PoolDeviceService>(ServiceId::PoolDevice);
    const HAService* haSvc = services.get<HAService>(ServiceId::Ha);
    const CommandService* cmdSvc = services.get<CommandService>(ServiceId::Command);
    alarmSvc_ = services.get<AlarmService>(ServiceId::Alarm);
    activityLogSvc_ = services.get<ActivityLogService>(ServiceId::ActivityLog);
    if (!ioSvc_) {
        LOGW("PoolLogic waiting for IOServiceV2");
    }
    if (!poolSvc_) {
        LOGW("PoolLogic waiting for PoolDeviceService");
    }
    // HA entities are still registered from the lifecycle layer because they
    // are part of startup wiring, not of the control algorithm itself.
    if (haSvc && haSvc->addSwitch) {
        const HASwitchEntry autoModeSwitch{
            "poollogic",
            "pl_modes_auto",
            "Pool Auto-regulation",
            "cfg/poollogic/modes",
            "{% if value_json.auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/modes\\\":{\\\"auto_mode\\\":true}}",
            "{\\\"poollogic/modes\\\":{\\\"auto_mode\\\":false}}",
            "mdi:calendar-clock",
            "config"
        };
        const HASwitchEntry winterModeSwitch{
            "poollogic",
            "pl_modes_winter",
            "Winter Mode",
            "cfg/poollogic/modes",
            "{% if value_json.winter_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/modes\\\":{\\\"winter_mode\\\":true}}",
            "{\\\"poollogic/modes\\\":{\\\"winter_mode\\\":false}}",
            "mdi:snowflake",
            "config"
        };
        const HASwitchEntry phAutoModeSwitch{
            "poollogic",
            "pl_ph_auto_mode",
            "pH Auto-regulation",
            "cfg/poollogic/ph",
            "{% if value_json.ph_auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/ph\\\":{\\\"ph_auto_mode\\\":true}}",
            "{\\\"poollogic/ph\\\":{\\\"ph_auto_mode\\\":false}}",
            "mdi:beaker-check-outline",
            "config"
        };
        const HASwitchEntry orpAutoModeSwitch{
            "poollogic",
            "pl_dis_auto",
            "Orp Auto-regulation",
            "cfg/poollogic/chlorine",
            "{% if value_json.dis_auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/chlorine\\\":{\\\"dis_auto_mode\\\":true}}",
            "{\\\"poollogic/chlorine\\\":{\\\"dis_auto_mode\\\":false}}",
            "mdi:water-check-outline",
            "config"
        };
        const HASwitchEntry heaterAutoModeSwitch{
            "poollogic",
            "pl_heat_auto",
            "Heater Auto-regulation",
            "cfg/poollogic/heater",
            "{% if value_json.heater_auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/heater\\\":{\\\"heater_auto_mode\\\":true}}",
            "{\\\"poollogic/heater\\\":{\\\"heater_auto_mode\\\":false}}",
            "mdi:radiator",
            "config"
        };
        const HASwitchEntry phDosePlusSwitch{
            "poollogic",
            "pl_ph_dose_plus",
            "pH Dosing uses pH+",
            "cfg/poollogic/ph",
            "{% if value_json.ph_dose_plus %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/ph\\\":{\\\"ph_dose_plus\\\":true}}",
            "{\\\"poollogic/ph\\\":{\\\"ph_dose_plus\\\":false}}",
            "mdi:beaker-plus-outline",
            "config"
        };
        const HASwitchEntry o2TempCompSwitch{
            "poollogic",
            "pl_o2_temp_comp",
            "O2 Temperature Compensation",
            "cfg/poollogic/o2",
            "{% if value_json.temp_comp %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/o2\\\":{\\\"temp_comp\\\":true}}",
            "{\\\"poollogic/o2\\\":{\\\"temp_comp\\\":false}}",
            "mdi:thermometer-lines",
            "config"
        };
        (void)haSvc->addSwitch(haSvc->ctx, &autoModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &winterModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &phAutoModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &orpAutoModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &heaterAutoModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &phDosePlusSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &o2TempCompSwitch);
    }
    if (haSvc && haSvc->addSelect) {
        static const char* kDisinfectionTypeStateTpl =
            R"({% set v = value_json.disinfection_type | int(0) %}{% if v == 1 %}Electrolyse{% elif v == 2 %}Oxygène actif{% elif v == 3 %}Désactivé{% else %}Chlore/Brome{% endif %})";
        static const char* kDisinfectionTypeCmdTpl =
            R"({% if value == 'Electrolyse' %}{\"poollogic/modes\":{\"disinfection_type\":1}}{% elif value == 'Oxygène actif' %}{\"poollogic/modes\":{\"disinfection_type\":2}}{% elif value == 'Désactivé' %}{\"poollogic/modes\":{\"disinfection_type\":3}}{% else %}{\"poollogic/modes\":{\"disinfection_type\":0}}{% endif %})";
        const HASelectEntry disinfectionTypeSelect{
            "poollogic",
            "pl_modes_dis",
            "Disinfection Type",
            "cfg/poollogic/modes",
            kDisinfectionTypeStateTpl,
            MqttTopics::SuffixCfgSet,
            kDisinfectionTypeCmdTpl,
            "[\"Désactivé\",\"Chlore/Brome\",\"Electrolyse\",\"Oxygène actif\"]",
            "mdi:water-check",
            "config"
        };
        static const char* kSwgControlModeStateTpl =
            R"({% set v = value_json.swg_control_mode | int(1) %}{% if v == 0 %}Suivi consigne ORP{% else %}Continu sur filtration{% endif %})";
        static const char* kSwgControlModeCmdTpl =
            R"({% if value == 'Suivi consigne ORP' %}{\"poollogic/swg\":{\"swg_control_mode\":0}}{% else %}{\"poollogic/swg\":{\"swg_control_mode\":1}}{% endif %})";
        const HASelectEntry swgControlModeSelect{
            "poollogic",
            "pl_swg_ctrl",
            "SWG Control Mode",
            "cfg/poollogic/swg",
            kSwgControlModeStateTpl,
            MqttTopics::SuffixCfgSet,
            kSwgControlModeCmdTpl,
            "[\"Suivi consigne ORP\",\"Continu sur filtration\"]",
            "mdi:flash",
            "config"
        };
        static const char* kO2MainHourStateTpl =
            R"({% set h = value_json.main_hour | int(12) %}{{ '%02d:00' | format(h) }})";
        static const char* kO2MainHourCmdTpl =
            R"({\"poollogic/o2\":{\"main_hour\":{{ value.split(':')[0] | int(12) }}}})";
        const HASelectEntry o2MainHourSelect{
            "poollogic",
            "pl_o2_hour",
            "O2 Dosing Hour",
            "cfg/poollogic/o2",
            kO2MainHourStateTpl,
            MqttTopics::SuffixCfgSet,
            kO2MainHourCmdTpl,
            "[\"00:00\",\"01:00\",\"02:00\",\"03:00\",\"04:00\",\"05:00\",\"06:00\",\"07:00\",\"08:00\",\"09:00\",\"10:00\",\"11:00\",\"12:00\",\"13:00\",\"14:00\",\"15:00\",\"16:00\",\"17:00\",\"18:00\",\"19:00\",\"20:00\",\"21:00\",\"22:00\",\"23:00\"]",
            "mdi:clock-time-four-outline",
            "config"
        };
        (void)haSvc->addSelect(haSvc->ctx, &disinfectionTypeSelect);
        (void)haSvc->addSelect(haSvc->ctx, &swgControlModeSelect);
        (void)haSvc->addSelect(haSvc->ctx, &o2MainHourSelect);
    }
    if (haSvc && haSvc->addSensor) {
        const HASensorEntry filtrationStart{
            "poollogic",
            "pl_flt_start",
            "Calculated Filtration Start",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_start_clc | int(0) }}",
            nullptr,
            "mdi:clock-start",
            "h"
        };
        const HASensorEntry filtrationStop{
            "poollogic",
            "pl_flt_stop",
            "Calculated Filtration Stop",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_stop_clc | int(0) }}",
            nullptr,
            "mdi:clock-end",
            "h"
        };
        static const char* kHeatAssistStatusFrTemplate =
            R"({% set st = value_json.ri | default('UNKNOWN', true) %}{% if st == 'DISABLED' %}Désactivé{% elif st == 'MANUAL_MODE' %}Mode manuel{% elif st == 'PSI_BLOCKED' %}Pression bloquée{% elif st == 'SETPOINT_INVALID' %}Consigne invalide{% elif st == 'TEMP_UNAVAILABLE' %}Température indisponible{% elif st == 'PROBE_WAIT_30M' %}Attente sonde 30 min{% elif st == 'PROBE_WAIT_20M' %}Attente sonde 20 min{% elif st == 'PROBE_WAIT_ADAPTIVE' %}Attente sonde adaptative{% elif st == 'PROBE_RUNNING' %}Sondage en cours{% elif st == 'HEATING' %}Chauffe active{% elif st == 'IDLE_PUMP_ON' %}Pompe active sans chauffe{% elif st == 'SETPOINT_REACHED' %}Consigne atteinte{% else %}Inconnu{% endif %})";
        const HASensorEntry heatAssistStatus{
            "poollogic",
            "pl_has_rsn",
            "Heat Assist Status",
            "rt/poollogic/heat_assist",
            kHeatAssistStatusFrTemplate,
            "diagnostic",
            "mdi:information-outline",
            nullptr,
            false,
            nullptr,
            true
        };
        static const char* kO2ProtocolStateTemplate =
            R"({% set st = value_json.o2.state | int(0) %}{% if st == 1 %}En attente{% elif st == 2 %}Dosage{% elif st == 3 %}Bloqué{% else %}Repos{% endif %})";
        const HASensorEntry o2ProtocolState{
            "poollogic",
            "pl_o2_state",
            "O2 Protocol State",
            "rt/poollogic/disinfection",
            kO2ProtocolStateTemplate,
            "diagnostic",
            "mdi:progress-clock",
            nullptr,
            false,
            nullptr,
            true
        };
        const HASensorEntry o2WeeklyDone{
            "poollogic",
            "pl_o2_done",
            "O2 Weekly Injected",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.done_ml | float(0) }}",
            "diagnostic",
            "mdi:bottle-tonic-plus-outline",
            "ml"
        };
        const HASensorEntry o2Pending{
            "poollogic",
            "pl_o2_pending",
            "O2 Pending Volume",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.pending_ml | float(0) }}",
            "diagnostic",
            "mdi:bottle-tonic-outline",
            "ml"
        };
        const HASensorEntry o2LastDoseDay{
            "poollogic",
            "pl_o2_last_day",
            "O2 Last Dose Day",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.last_day | int(0) }}",
            "diagnostic",
            "mdi:calendar-check-outline",
            nullptr
        };
        const HASensorEntry o2BlockReason{
            "poollogic",
            "pl_o2_block",
            "O2 Block Reason",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.block_s | default('none') }}",
            "diagnostic",
            "mdi:alert-circle-outline",
            nullptr,
            false,
            nullptr,
            true
        };
        const HASensorEntry o2PlannedDose{
            "poollogic",
            "pl_o2_plan",
            "O2 Planned Dose",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.plan_ml | float(0) }}",
            "diagnostic",
            "mdi:cup-water",
            "ml"
        };
        const HASensorEntry o2PumpFlow{
            "poollogic",
            "pl_o2_flow",
            "O2 Pump Flow",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.flow_l_h | float(0) }}",
            "diagnostic",
            "mdi:pump",
            "L/h"
        };
        (void)haSvc->addSensor(haSvc->ctx, &filtrationStart);
        (void)haSvc->addSensor(haSvc->ctx, &filtrationStop);
        (void)haSvc->addSensor(haSvc->ctx, &heatAssistStatus);
        (void)haSvc->addSensor(haSvc->ctx, &o2ProtocolState);
        (void)haSvc->addSensor(haSvc->ctx, &o2WeeklyDone);
        (void)haSvc->addSensor(haSvc->ctx, &o2Pending);
        (void)haSvc->addSensor(haSvc->ctx, &o2LastDoseDay);
        (void)haSvc->addSensor(haSvc->ctx, &o2BlockReason);
        (void)haSvc->addSensor(haSvc->ctx, &o2PlannedDose);
        (void)haSvc->addSensor(haSvc->ctx, &o2PumpFlow);
    }
    if (haSvc && haSvc->addNumber) {
        const HANumberEntry waterTempSetpoint{
            "poollogic",
            "pl_wat_temp_sp",
            "Water Temperature Setpoint",
            "cfg/poollogic/filtration",
            "{{ value_json.wat_temp_setpt | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/filtration\\\":{\\\"wat_temp_setpt\\\":{{ value | float(0) }}}}",
            5.0f,
            35.0f,
            0.1f,
            "slider",
            "config",
            "mdi:thermometer-water",
            "C"
        };
        const HANumberEntry filtrationStartMin{
            "poollogic",
            "pl_flt_start_min",
            "Min Start Filtration Pump",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_start_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/filtration\\\":{\\\"filtr_start_min\\\":{{ value | int(0) }}}}",
            0.0f,
            23.0f,
            1.0f,
            "box",
            "config",
            "mdi:clock-start",
            "h"
        };
        const HANumberEntry filtrationStopMax{
            "poollogic",
            "pl_flt_stop_max",
            "Max End Filtration Pump",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_stop_max | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/filtration\\\":{\\\"filtr_stop_max\\\":{{ value | int(0) }}}}",
            0.0f,
            23.0f,
            1.0f,
            "box",
            "config",
            "mdi:clock-end",
            "h"
        };
        const HANumberEntry delayPidsMin{
            "poollogic",
            "pl_reg_dly_pid",
            "Delay PIDs",
            "cfg/poollogic/regulation",
            "{{ value_json.dly_pid_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/regulation\\\":{\\\"dly_pid_min\\\":{{ value | int(0) }}}}",
            0.0f,
            30.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timer-sand",
            "min"
        };
        const HANumberEntry delayElectroMin{
            "poollogic",
            "pl_swg_dly_elec",
            "Delay Chlorine Generator",
            "cfg/poollogic/swg",
            "{{ value_json.dly_electro_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/swg\\\":{\\\"dly_electro_min\\\":{{ value | int(0) }}}}",
            0.0f,
            120.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timer-sand",
            "min"
        };
        const HANumberEntry fillMinUptime{
            "poollogic",
            "pl_refill_min_on",
            "Min Uptime Fill Pump",
            "cfg/poollogic/refill",
            "{{ ((value_json.fill_min_on_s | float(0)) / 60) | round(1) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/refill\\\":{\\\"fill_min_on_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
            0.0f,
            4.0f,
            0.5f,
            "box",
            "config",
            "mdi:timer-cog-outline",
            "mn"
        };
        const HANumberEntry phSetpoint{
            "poollogic",
            "pl_ph_setpoint",
            "pH Setpoint",
            "cfg/poollogic/ph",
            "{{ value_json.ph_setpoint | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/ph\\\":{\\\"ph_setpoint\\\":{{ value | float(0) }}}}",
            6.0f,
            8.0f,
            0.01f,
            "slider",
            "config",
            "mdi:beaker-outline",
            nullptr
        };
        const HANumberEntry orpSetpoint{
            "poollogic",
            "pl_dis_setpoint",
            "Orp Setpoint",
            "cfg/poollogic/chlorine",
            "{{ value_json.dis_setpoint | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/chlorine\\\":{\\\"dis_setpoint\\\":{{ value | float(0) }}}}",
            300.0f,
            900.0f,
            1.0f,
            "slider",
            "config",
            "mdi:water-outline",
            "mV"
        };
        const HANumberEntry heaterSetpoint{
            "poollogic",
            "pl_heat_setpoint",
            "Water Heater Setpoint",
            "cfg/poollogic/heater",
            "{{ value_json.heater_setpoint | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/heater\\\":{\\\"heater_setpoint\\\":{{ value | float(0) }}}}",
            10.0f,
            35.0f,
            0.1f,
            "slider",
            "config",
            "mdi:thermometer-water",
            "C"
        };
        const HANumberEntry chlorineGeneratorMinTemp{
            "poollogic",
            "pl_swg_min_temp",
            "Min Temperature Chlorine Generator",
            "cfg/poollogic/swg",
            "{{ value_json.secure_elec_t | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/swg\\\":{\\\"secure_elec_t\\\":{{ value | float(0) }}}}",
            5.0f,
            35.0f,
            0.1f,
            "slider",
            "config",
            "mdi:thermometer-alert",
            "C"
        };
        const HANumberEntry phWindowMin{
            "poollogic",
            "pl_ph_window",
            "pH PID Window Size",
            "cfg/poollogic/ph",
            "{{ ((value_json.ph_window_ms | float(0)) / 60000) | round(0) | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/ph\\\":{\\\"ph_window_ms\\\":{{ (value | float(0) * 60000) | round(0) | int(0) }}}}",
            1.0f,
            180.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timeline-clock-outline",
            "min"
        };
        const HANumberEntry orpWindowMin{
            "poollogic",
            "pl_dis_window",
            "Orp PID Window Size",
            "cfg/poollogic/chlorine",
            "{{ ((value_json.dis_window_ms | float(0)) / 60000) | round(0) | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/chlorine\\\":{\\\"dis_window_ms\\\":{{ (value | float(0) * 60000) | round(0) | int(0) }}}}",
            1.0f,
            180.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timeline-clock-outline",
            "min"
        };
        const HANumberEntry psiLowThreshold{
            "poollogic",
            "pl_safe_psi_low",
            "PSI Low Threshold",
            "cfg/poollogic/safety",
            "{{ value_json.psi_low_th | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/safety\\\":{\\\"psi_low_th\\\":{{ value | float(0) }}}}",
            0.0f,
            5.0f,
            0.01f,
            "slider",
            "config",
            "mdi:gauge-low",
            "bar"
        };
        const HANumberEntry psiHighThreshold{
            "poollogic",
            "pl_safe_psi_high",
            "PSI High Threshold",
            "cfg/poollogic/safety",
            "{{ value_json.psi_high_th | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/safety\\\":{\\\"psi_high_th\\\":{{ value | float(0) }}}}",
            0.0f,
            5.0f,
            0.01f,
            "slider",
            "config",
            "mdi:gauge-full",
            "bar"
        };
        const HANumberEntry o2PoolVolume{
            "poollogic",
            "pl_o2_vol",
            "O2 Pool Volume",
            "cfg/poollogic/o2",
            "{{ value_json.pool_volume_m3 | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/o2\\\":{\\\"pool_volume_m3\\\":{{ value | float(0) }}}}",
            1.0f,
            200.0f,
            0.1f,
            "box",
            "config",
            "mdi:pool",
            "m3"
        };
        const HANumberEntry o2WeeklyDose{
            "poollogic",
            "pl_o2_dose",
            "O2 Weekly Dose per 10 m3",
            "cfg/poollogic/o2",
            "{{ value_json.dose_ml_10m3_week | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/o2\\\":{\\\"dose_ml_10m3_week\\\":{{ value | float(0) }}}}",
            0.0f,
            5000.0f,
            10.0f,
            "box",
            "config",
            "mdi:cup-water",
            "ml"
        };
        const HANumberEntry o2SplitCount{
            "poollogic",
            "pl_o2_split",
            "O2 Weekly Split Count",
            "cfg/poollogic/o2",
            "{{ value_json.split_count | int(1) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/o2\\\":{\\\"split_count\\\":{{ value | int(1) }}}}",
            1.0f,
            3.0f,
            1.0f,
            "box",
            "config",
            "mdi:call-split",
            nullptr
        };
        const HANumberEntry o2LoadFactor{
            "poollogic",
            "pl_o2_load",
            "O2 Load Factor",
            "cfg/poollogic/o2",
            "{{ value_json.load_factor | float(1) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/o2\\\":{\\\"load_factor\\\":{{ value | float(1) }}}}",
            0.5f,
            2.0f,
            0.05f,
            "slider",
            "config",
            "mdi:scale-balance",
            nullptr
        };
        const HANumberEntry o2MinFilterRun{
            "poollogic",
            "pl_o2_min_flt",
            "O2 Min Filtration Runtime",
            "cfg/poollogic/o2",
            "{{ value_json.min_filter_run_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/o2\\\":{\\\"min_filter_run_min\\\":{{ value | int(0) }}}}",
            0.0f,
            240.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timer-check-outline",
            "min"
        };
        (void)haSvc->addNumber(haSvc->ctx, &waterTempSetpoint);
        (void)haSvc->addNumber(haSvc->ctx, &heaterSetpoint);
        (void)haSvc->addNumber(haSvc->ctx, &filtrationStartMin);
        (void)haSvc->addNumber(haSvc->ctx, &filtrationStopMax);
        (void)haSvc->addNumber(haSvc->ctx, &delayPidsMin);
        (void)haSvc->addNumber(haSvc->ctx, &delayElectroMin);
        (void)haSvc->addNumber(haSvc->ctx, &fillMinUptime);
        (void)haSvc->addNumber(haSvc->ctx, &phSetpoint);
        (void)haSvc->addNumber(haSvc->ctx, &orpSetpoint);
        (void)haSvc->addNumber(haSvc->ctx, &chlorineGeneratorMinTemp);
        (void)haSvc->addNumber(haSvc->ctx, &phWindowMin);
        (void)haSvc->addNumber(haSvc->ctx, &orpWindowMin);
        (void)haSvc->addNumber(haSvc->ctx, &psiLowThreshold);
        (void)haSvc->addNumber(haSvc->ctx, &psiHighThreshold);
        (void)haSvc->addNumber(haSvc->ctx, &o2PoolVolume);
        (void)haSvc->addNumber(haSvc->ctx, &o2WeeklyDose);
        (void)haSvc->addNumber(haSvc->ctx, &o2SplitCount);
        (void)haSvc->addNumber(haSvc->ctx, &o2LoadFactor);
        (void)haSvc->addNumber(haSvc->ctx, &o2MinFilterRun);
    }
    if (haSvc && haSvc->addButton) {
        const HAButtonEntry filtrationRecalc{
            "poollogic",
            "pl_flt_recalc",
            "Recalculate Filtration Window",
            MqttTopics::SuffixCmd,
            "{\\\"cmd\\\":\\\"poollogic.filtration.recalc\\\"}",
            "config",
            "mdi:refresh"
        };
        (void)haSvc->addButton(haSvc->ctx, &filtrationRecalc);
    }
    if (cmdSvc && cmdSvc->registerHandler) {
        cmdSvc->registerHandler(cmdSvc->ctx, "poollogic.filtration.write", &PoolLogicModule::cmdFiltrationWriteStatic_, this);
        cmdSvc->registerHandler(cmdSvc->ctx, "poollogic.filtration.recalc", &PoolLogicModule::cmdFiltrationRecalcStatic_, this);
        cmdSvc->registerHandler(cmdSvc->ctx, "poollogic.auto_mode.set", &PoolLogicModule::cmdAutoModeSetStatic_, this);
        static constexpr const char* kMqttControlCmds[] = {
            "poollogic.auto_mode.toggle",
            "poollogic.ph_auto_mode.set",
            "poollogic.ph_auto_mode.toggle",
            "poollogic.orp_auto_mode.set",
            "poollogic.orp_auto_mode.toggle",
            "poollogic.dis_auto_mode.set",
            "poollogic.dis_auto_mode.toggle",
            "poollogic.heater_auto_mode.set",
            "poollogic.heater_auto_mode.toggle",
            "poollogic.winter_mode.set",
            "poollogic.winter_mode.toggle",
            "poollogic.filtration.toggle",
            "poollogic.ph_pump.write",
            "poollogic.ph_pump.toggle",
            "poollogic.orp_pump.write",
            "poollogic.orp_pump.toggle",
            "poollogic.dis_pump.write",
            "poollogic.dis_pump.toggle",
            "poollogic.lights.write",
            "poollogic.lights.toggle",
            "poollogic.robot.write",
            "poollogic.robot.toggle",
            "poollogic.heater.write",
            "poollogic.heater.toggle",
            "poollogic.chlorine_generator.write",
            "poollogic.chlorine_generator.toggle"
        };
        for (uint8_t i = 0; i < (uint8_t)(sizeof(kMqttControlCmds) / sizeof(kMqttControlCmds[0])); ++i) {
            cmdSvc->registerHandler(cmdSvc->ctx, kMqttControlCmds[i], &PoolLogicModule::cmdMqttControlStatic_, this);
        }
    }
    // PoolLogic owns the alarm definitions but delegates evaluation to the
    // shared alarm module through static condition callbacks.
    if (alarmSvc_ && alarmSvc_->registerAlarm) {
        const AlarmRegistration psiLowAlarm{
            AlarmId::PoolPsiLow,
            AlarmSeverity::Alarm,
            true,
            2000,
            1000,
            60000,
            "psi_low",
            "Low pressure",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &psiLowAlarm, &PoolLogicModule::condPsiLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPsiLow");
        }

        const AlarmRegistration psiHighAlarm{
            AlarmId::PoolPsiHigh,
            AlarmSeverity::Critical,
            true,
            0,
            1000,
            60000,
            "psi_high",
            "High pressure",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &psiHighAlarm, &PoolLogicModule::condPsiHighStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPsiHigh");
        }

        const AlarmRegistration phTankLowAlarm{
            AlarmId::PoolPhTankLow,
            AlarmSeverity::Alarm,
            false,
            500,
            1000,
            60000,
            "ph_tank_low",
            "pH tank low",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &phTankLowAlarm, &PoolLogicModule::condPhTankLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPhTankLow");
        }

        const AlarmRegistration chlorineTankLowAlarm{
            AlarmId::PoolChlorineTankLow,
            AlarmSeverity::Alarm,
            false,
            500,
            1000,
            60000,
            "chlorine_tank_low",
            "Chlorine tank low",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &chlorineTankLowAlarm, &PoolLogicModule::condChlorineTankLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolChlorineTankLow");
        }

        const AlarmRegistration phPumpMaxUptimeAlarm{
            AlarmId::PoolPhPumpMaxUptime,
            AlarmSeverity::Alarm,
            true,
            500,
            1000,
            60000,
            "ph_pump_max_uptime",
            "pH pump max uptime reached",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &phPumpMaxUptimeAlarm, &PoolLogicModule::condPhPumpMaxUptimeStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPhPumpMaxUptime");
        }

        const AlarmRegistration chlorinePumpMaxUptimeAlarm{
            AlarmId::PoolChlorinePumpMaxUptime,
            AlarmSeverity::Alarm,
            true,
            500,
            1000,
            60000,
            "chlorine_pump_uptime",
            "Chlorine pump max uptime reached",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &chlorinePumpMaxUptimeAlarm, &PoolLogicModule::condChlorinePumpMaxUptimeStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolChlorinePumpMaxUptime");
        }

        const AlarmRegistration waterLevelLowAlarm{
            AlarmId::PoolWaterLevelLow,
            AlarmSeverity::Alarm,
            false,
            60000,
            1000,
            60000,
            "pool_water_level_low",
            "Pool water level low",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &waterLevelLowAlarm, &PoolLogicModule::condWaterLevelLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolWaterLevelLow");
        }
    } else {
        LOGW("PoolLogic running without alarm service");
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::SchedulerEventTriggered, &PoolLogicModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::ConfigChanged, &PoolLogicModule::onEventStatic_, this);
    }

    if (!enabled_) {
        LOGI("PoolLogic disabled");
        return;
    }

    LOGI("PoolLogic ready");
    (void)cfgStore_;
}

void PoolLogicModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kPoolLogicCfgProducerId,
                               kPoolLogicCfgRoutes,
                               (uint8_t)(sizeof(kPoolLogicCfgRoutes) / sizeof(kPoolLogicCfgRoutes[0])),
                               services);
    }

    startupActivityPending_ = true;
    startupActivitySinceMs_ = millis();

    if (!enabled_) return;

    if (disinfectionType_ > DisinfectionDisabled) {
        disinfectionType_ = DisinfectionChlorineBromine;
        if (cfgStore_) (void)cfgStore_->set(disinfectionTypeVar_, disinfectionType_);
    }
    if (swgControlMode_ > SwgControlContinuous) {
        swgControlMode_ = SwgControlContinuous;
        if (cfgStore_) (void)cfgStore_->set(swgControlModeVar_, swgControlMode_);
    }
    if (!std::isfinite(o2PoolVolumeM3_) || o2PoolVolumeM3_ <= 0.0f) {
        o2PoolVolumeM3_ = 50.0f;
        if (cfgStore_) (void)cfgStore_->set(o2PoolVolumeVar_, o2PoolVolumeM3_);
    }
    if (!std::isfinite(o2DoseMlPer10M3Week_) || o2DoseMlPer10M3Week_ <= 0.0f) {
        o2DoseMlPer10M3Week_ = 500.0f;
        if (cfgStore_) (void)cfgStore_->set(o2DoseVar_, o2DoseMlPer10M3Week_);
    }
    if (o2MainHour_ > 23U) {
        o2MainHour_ = 20U;
        if (cfgStore_) (void)cfgStore_->set(o2MainHourVar_, o2MainHour_);
    }
    if (o2SplitCount_ == 0U || o2SplitCount_ > 3U) {
        o2SplitCount_ = 2U;
        if (cfgStore_) (void)cfgStore_->set(o2SplitCountVar_, o2SplitCount_);
    }
    if (!std::isfinite(o2LoadFactor_) || o2LoadFactor_ <= 0.0f) {
        o2LoadFactor_ = 1.0f;
        if (cfgStore_) (void)cfgStore_->set(o2LoadFactorVar_, o2LoadFactor_);
    }
    if (o2ProtocolState_ > O2ProtocolBlocked) {
        o2ProtocolState_ = O2ProtocolIdle;
        if (cfgStore_) (void)cfgStore_->set(o2ProtocolStateVar_, o2ProtocolState_);
    }
    if (!std::isfinite(o2WeeklyDoneMl_) || o2WeeklyDoneMl_ < 0.0f) {
        o2WeeklyDoneMl_ = 0.0f;
        if (cfgStore_) (void)cfgStore_->set(o2WeeklyDoneVar_, o2WeeklyDoneMl_);
    }
    if (!std::isfinite(o2PendingMl_) || o2PendingMl_ < 0.0f) {
        o2PendingMl_ = 0.0f;
        if (cfgStore_) (void)cfgStore_->set(o2PendingVar_, o2PendingMl_);
    }

    LOGI("PoolLogic pH dosing mode=%s", phDosePlus_ ? "pH+" : "pH-");
    LOGI("PoolLogic disinfection=%s swg_control=%s",
         disinfectionTypeStr_(disinfectionType_),
         swgControlModeStr_(swgControlMode_));
    normalizeDeviceSlots_();
    logDeviceSlotConfig_();

    ensureDailySlot_();

    if (schedSvc_ && schedSvc_->isActive) {
        filtrationWindowActive_ = schedSvc_->isActive(schedSvc_->ctx, SLOT_FILTR_WINDOW);
    }

    // Trigger one recompute on startup, after persisted config and scheduler
    // blob are fully loaded, so the window reflects the final restored state.
    portENTER_CRITICAL(&pendingMux_);
    pendingDailyRecalc_ = true;
    portEXIT_CRITICAL(&pendingMux_);
}

bool PoolLogicModule::activityTimeReady_() const
{
    if (!timeSvc_ || !timeSvc_->isSynced || !timeSvc_->epoch) return false;
    if (!timeSvc_->isSynced(timeSvc_->ctx)) return false;
    const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
    return epoch >= kActivityMinEpoch;
}

void PoolLogicModule::emitStartupActivityIfReady_(uint32_t nowMs)
{
    if (!startupActivityPending_) return;

    const uint32_t elapsedMs = (uint32_t)(nowMs - startupActivitySinceMs_);
    if (elapsedMs < kStartupActivityStabilizeMs) return;

    const bool timeReady = activityTimeReady_();
    if (!timeReady && elapsedMs < kStartupActivityMaxDelayMs) return;

    if (enabled_) {
        emitActivity_(ActivityCode::PoolLogicReady,
                      ActivitySource::System,
                      ActivitySeverity::Success,
                      ActivityRole::None,
                      ActivityState::None,
                      ActivityReason::None,
                      ACTIVITY_TARGET_NONE,
                      "PoolLogic est prêt",
                      "Les automatismes piscine sont initialisés.",
                      "pool");
    } else {
        emitActivity_(ActivityCode::PoolLogicDisabled,
                      ActivitySource::System,
                      ActivitySeverity::Info,
                      ActivityRole::None,
                      ActivityState::None,
                      ActivityReason::None,
                      ACTIVITY_TARGET_NONE,
                      "PoolLogic est désactivé",
                      "Les automatismes piscine ne pilotent pas les équipements.",
                      "toggle_off");
    }
    startupActivityPending_ = false;
}

void PoolLogicModule::loop()
{
    const uint32_t nowMs = millis();
    emitStartupActivityIfReady_(nowMs);

    if (!enabled_) {
        if (!bootControlReady_) {
            if (setPoolDeviceWritesEnabled_(true)) {
                bootControlReady_ = true;
                LOGI("PoolLogic disabled: pool device writes enabled after adoption");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }

    // The loop only consumes latched scheduler events; actual work stays in the
    // task context to avoid doing heavy operations from event callbacks.
    bool doRecalc = false;
    bool doDayReset = false;
    portENTER_CRITICAL(&pendingMux_);
    doRecalc = pendingDailyRecalc_;
    pendingDailyRecalc_ = false;
    doDayReset = pendingDayReset_;
    pendingDayReset_ = false;
    portEXIT_CRITICAL(&pendingMux_);

    if (doRecalc) {
        (void)recalcAndApplyFiltrationWindow_();
    }

    if (doDayReset) {
        cleaningDone_ = false;
        LOGI("Daily reset: cleaning_done=false");
    }

    if (!bootControlReady_) {
        adoptBootDeviceState_(nowMs);
        if (setPoolDeviceWritesEnabled_(true)) {
            bootControlReady_ = true;
            LOGI("PoolLogic boot adoption complete; pool device writes enabled");
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }

    runControlLoop_(nowMs);
    vTaskDelay(pdMS_TO_TICKS(200));
}

void PoolLogicModule::onEventStatic_(const Event& e, void* user)
{
    if (!user) return;
    static_cast<PoolLogicModule*>(user)->onEvent_(e);
}

void PoolLogicModule::onEvent_(const Event& e)
{
    if (!enabled_) return;

    if (e.id == EventId::ConfigChanged) {
        if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchFiltration) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::FiltrationCalcStart) == 0 ||
                strcmp(p->nvsKey, NvsKeys::PoolLogic::FiltrationCalcStop) == 0) {
                (void)applyFiltrationWindowSlot_(filtrationCalcStart_, filtrationCalcStop_);
            } else {
                portENTER_CRITICAL(&pendingMux_);
                pendingDailyRecalc_ = true;
                portEXIT_CRITICAL(&pendingMux_);
            }
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchModes &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::AutoMode) == 0 && autoMode_) {
                portENTER_CRITICAL(&pendingMux_);
                pendingFiltrationReconcile_ = true;
                portEXIT_CRITICAL(&pendingMux_);
            } else if (strcmp(p->nvsKey, NvsKeys::PoolLogic::DisinfectionType) == 0) {
                if (disinfectionType_ > DisinfectionDisabled) disinfectionType_ = DisinfectionChlorineBromine;
                (void)writeDeviceDesired_(orpPumpDeviceSlot_, false);
                (void)writeDeviceDesired_(swgDeviceSlot_, false);
                if (disinfectionType_ == DisinfectionChlorineBromine && !orpAutoMode_ && cfgStore_) {
                    (void)cfgStore_->set(orpAutoModeVar_, true);
                    orpAutoMode_ = true;
                }
                resetTemporalPidState_(orpPidState_, millis());
                orpPidEnabled_ = false;
                LOGI("PoolLogic disinfection changed: %s", disinfectionTypeStr_(disinfectionType_));
            }
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchPh &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::PhAutoMode) == 0 && phAutoMode_) {
                // Global business rule: entering pH auto starts from a safe stopped pump.
                if (!writeDeviceDesired_(phPumpDeviceSlot_, false)) {
                    LOGW("PoolLogic failed to stop pH pump on ph_auto_mode enable (slot=%u)",
                         (unsigned)phPumpDeviceSlot_);
                }
                resetTemporalPidState_(phPidState_, millis());
            }
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchChlorine &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::OrpAutoMode) == 0 && orpAutoMode_) {
                // Global business rule: entering disinfection auto starts from a safe stopped pump.
                if (!writeDeviceDesired_(orpPumpDeviceSlot_, false)) {
                    LOGW("PoolLogic failed to stop disinfection pump on dis_auto_mode enable (slot=%u)",
                         (unsigned)orpPumpDeviceSlot_);
                }
                resetTemporalPidState_(orpPidState_, millis());
            }
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchHeater &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::HeaterAutoMode) == 0 && heaterAutoMode_) {
                // Entering heater auto starts from a safe stopped heater relay.
                if (!writeDeviceDesired_(heaterDeviceSlot_, false)) {
                    LOGW("PoolLogic failed to stop heater on heater_auto_mode enable (slot=%u)",
                         (unsigned)heaterDeviceSlot_);
                }
            }
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchSwg &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::SwgControlMode) == 0) {
                if (swgControlMode_ > SwgControlContinuous) swgControlMode_ = SwgControlContinuous;
                (void)writeDeviceDesired_(swgDeviceSlot_, false);
                LOGI("PoolLogic SWG control changed: %s", swgControlModeStr_(swgControlMode_));
            }
            return;
        }
        return;
    }

    if (e.id != EventId::SchedulerEventTriggered) return;
    if (!e.payload || e.len < sizeof(SchedulerEventTriggeredPayload)) return;

    const SchedulerEventTriggeredPayload* p = (const SchedulerEventTriggeredPayload*)e.payload;
    const SchedulerEdge edge = (SchedulerEdge)p->edge;

    if (p->eventId == POOLLOGIC_EVENT_DAILY_RECALC && edge == SchedulerEdge::Trigger) {
        portENTER_CRITICAL(&pendingMux_);
        pendingDailyRecalc_ = true;
        portEXIT_CRITICAL(&pendingMux_);
        return;
    }

    if (p->eventId == TIME_EVENT_SYS_DAY_START && edge == SchedulerEdge::Trigger) {
        portENTER_CRITICAL(&pendingMux_);
        pendingDayReset_ = true;
        portEXIT_CRITICAL(&pendingMux_);
        return;
    }

    // Scheduler edges only latch intent/state; the loop owns the control work.
    if (p->eventId == POOLLOGIC_EVENT_FILTRATION_WINDOW) {
        if (edge == SchedulerEdge::Start) {
            portENTER_CRITICAL(&pendingMux_);
            filtrationWindowActive_ = true;
            portEXIT_CRITICAL(&pendingMux_);
        } else if (edge == SchedulerEdge::Stop) {
            portENTER_CRITICAL(&pendingMux_);
            filtrationWindowActive_ = false;
            portEXIT_CRITICAL(&pendingMux_);
        }
    }
}

void PoolLogicModule::normalizeDeviceSlots_()
{
    // Persisting invalid slots back to defaults keeps future boots and cfg
    // publications aligned with the effective runtime wiring.
    auto normalize = [this](uint8_t& slot,
                            uint8_t defSlot,
                            ConfigVariable<uint8_t,0>& var,
                            const char* role) {
        if (slot < POOL_DEVICE_MAX) return;
        LOGW("PoolLogic invalid device slot role=%s slot=%u -> default=%u",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)defSlot);
        slot = defSlot;
        if (cfgStore_) {
            (void)cfgStore_->set(var, slot);
        }
    };

    normalize(filtrationDeviceSlot_, PoolIds::DeviceFiltrationPump, filtrationDeviceVar_, "filtration");
    normalize(swgDeviceSlot_, PoolIds::DeviceChlorineGenerator, swgDeviceVar_, "swg");
    normalize(robotDeviceSlot_, PoolIds::DeviceRobot, robotDeviceVar_, "robot");
    normalize(fillingDeviceSlot_, PoolIds::DeviceFillPump, fillingDeviceVar_, "filling");
    normalize(phPumpDeviceSlot_, PoolIds::DevicePhPump, phPumpDeviceVar_, "ph_pump");
    normalize(orpPumpDeviceSlot_, PoolIds::DeviceChlorinePump, orpPumpDeviceVar_, "dis_pump");
    normalize(heaterDeviceSlot_, PoolIds::DeviceWaterHeater, heaterDeviceVar_, "heater");
}

void PoolLogicModule::logDeviceSlotConfig_() const
{
    LOGI("PoolLogic slots filtr=%u swg=%u robot=%u fill=%u ph=%u dis=%u heater=%u",
         (unsigned)filtrationDeviceSlot_,
         (unsigned)swgDeviceSlot_,
         (unsigned)robotDeviceSlot_,
         (unsigned)fillingDeviceSlot_,
         (unsigned)phPumpDeviceSlot_,
         (unsigned)orpPumpDeviceSlot_,
         (unsigned)heaterDeviceSlot_);

    logDeviceSlotBinding_("filtration", filtrationDeviceSlot_, 0);
    logDeviceSlotBinding_("swg", swgDeviceSlot_, -1);
    logDeviceSlotBinding_("robot", robotDeviceSlot_, -1);
    logDeviceSlotBinding_("filling", fillingDeviceSlot_, -1);
    logDeviceSlotBinding_("ph_pump", phPumpDeviceSlot_, 1);
    logDeviceSlotBinding_("dis_pump", orpPumpDeviceSlot_, 1);
    logDeviceSlotBinding_("heater", heaterDeviceSlot_, -1);
}

void PoolLogicModule::logDeviceSlotBinding_(const char* role, uint8_t slot, int8_t expectedType) const
{
    if (!poolSvc_ || !poolSvc_->meta) {
        LOGW("PoolLogic role=%s slot=%u PDM meta unavailable", role ? role : "?", (unsigned)slot);
        return;
    }

    PoolDeviceSvcMeta meta{};
    const PoolDeviceSvcStatus st = poolSvc_->meta(poolSvc_->ctx, slot, &meta);
    if (st != POOLDEV_SVC_OK) {
        LOGW("PoolLogic role=%s slot=%u PDM meta failed st=%u",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)st);
        return;
    }

    LOGI("PoolLogic role=%s slot=%u pdm used=%u type=%u io=%u label=%s",
         role ? role : "?",
         (unsigned)slot,
         (unsigned)meta.used,
         (unsigned)meta.type,
         (unsigned)meta.ioId,
         meta.label[0] ? meta.label : "?");

    if (expectedType >= 0 && meta.type != (uint8_t)expectedType) {
        LOGW("PoolLogic role=%s slot=%u type mismatch expected=%u got=%u",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)expectedType,
             (unsigned)meta.type);
    }
}
