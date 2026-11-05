#pragma once

#include "App/FirmwareProfile.h"
#include "Core/SystemLimits.h"
#include "Modules/AlarmModule/AlarmModule.h"
#include "Modules/CommandModule/CommandModule.h"
#include "Modules/EventBusModule/EventBusModule.h"
#include "Modules/HMIModule/HMIModule.h"
#include "Modules/HMIBuzzerModule/HMIBuzzerModule.h"
#include "Modules/IOModule/IOBus/OneWireBus.h"
#include "Modules/IOModule/IOModule.h"
#if FLOW_ENABLE_BOOT_LOG_CAPTURE
#include "Modules/Logs/BootLogCaptureModule/BootLogCaptureModule.h"
#endif
#include "Modules/Logs/LogDispatcherModule/LogDispatcherModule.h"
#include "Modules/Logs/LogHubModule/LogHubModule.h"
#include "Modules/Logs/LogSerialSinkModule/LogSerialSinkModule.h"
#include "Modules/Logs/ActivityLogModule/ActivityLogModule.h"
#include "Modules/Network/EthernetModule/EthernetModule.h"
#include "Modules/Network/HAModule/HAModule.h"
#include "Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.h"
#include "Modules/Network/HmiUdpServerModule/HmiUdpServerModule.h"
#include "Modules/Network/MQTTModule/MQTTModule.h"
#include "Modules/Network/TimeModule/TimeModule.h"
#include "Modules/Network/WebInterfaceModule/WebInterfaceModule.h"
#include "Modules/Network/WifiProvisioningModule/WifiProvisioningModule.h"
#include "Modules/Network/WifiModule/WifiModule.h"
#include "Modules/PoolDeviceModule/PoolDeviceModule.h"
#include "Modules/PoolLogicModule/PoolLogicModule.h"
#include "Modules/Stores/ConfigStoreModule/ConfigStoreModule.h"
#include "Modules/Stores/DataStoreModule/DataStoreModule.h"
#if defined(FLOW_ENABLE_LOCAL_TFT_HMI) && (FLOW_ENABLE_LOCAL_TFT_HMI != 0)
#include "Modules/SupervisorHMIModule/SupervisorHMIModule.h"
#endif
#if defined(FLOW_ENABLE_TFT_S3) && (FLOW_ENABLE_TFT_S3 != 0)
#include "Modules/TFTModuleS3/TFTModuleS3.h"
#endif
#include "Modules/System/SystemModule/SystemModule.h"
#include "Modules/System/SystemMonitorModule/SystemMonitorModule.h"

class DataStore;
struct HAService;

namespace Profiles {
namespace Waveshare {

struct ModuleInstances {
    explicit ModuleInstances(const BoardSpec& board);

    EthernetModule ethernetModule{};
    WifiModule wifiModule;
    TimeModule timeModule{};
    WifiProvisioningModule wifiProvisioningModule{};
    WebInterfaceModule webInterfaceModule;
    FirmwareUpdateModule firmwareUpdateModule;
#if defined(FLOW_ENABLE_LOCAL_TFT_HMI) && (FLOW_ENABLE_LOCAL_TFT_HMI != 0)
    SupervisorHMIModule supervisorHMIModule;
#endif
#if defined(FLOW_ENABLE_TFT_S3) && (FLOW_ENABLE_TFT_S3 != 0)
    TFTModuleS3 tftModuleS3;
#endif
    CommandModule commandModule{};
    ConfigStoreModule configStoreModule{};
    DataStoreModule dataStoreModule{};
    MQTTModule mqttModule{};
    HAModule haModule{};
    SystemModule systemModule{};
    SystemMonitorModule systemMonitorModule{};
    LogSerialSinkModule logSerialSinkModule{};
    ActivityLogModule activityLogModule{};
#if FLOW_ENABLE_BOOT_LOG_CAPTURE
    BootLogCaptureModule bootLogCaptureModule{};
#endif
    LogDispatcherModule logDispatcherModule{};
    LogHubModule logHubModule{};
    EventBusModule eventBusModule{};
    AlarmModule alarmModule{};
    HmiUdpServerModule hmiUdpServerModule{};
    HMIModule hmiModule;
    HMIBuzzerModule hmiBuzzerModule;
    IOModule ioModule;
    PoolDeviceModule poolDeviceModule{};
    PoolLogicModule poolLogicModule{};
    OneWireBus oneWireWater;
    OneWireBus oneWireAir;
    DataStore* ioDataStore = nullptr;
    const HAService* haService = nullptr;
    char topicNetworkState[Limits::TopicBuf] = {0};
    char topicSystemState[Limits::TopicBuf] = {0};
};

ModuleInstances& moduleInstances();
const FirmwareProfile& profile();
void setupProfile(AppContext& ctx);
void loopProfile(AppContext& ctx);

}  // namespace Waveshare
}  // namespace Profiles
