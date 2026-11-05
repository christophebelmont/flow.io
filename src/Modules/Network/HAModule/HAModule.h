#pragma once
/**
 * @file HAModule.h
 * @brief Home Assistant discovery producer.
 */

#include "Core/Module.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/NvsKeys.h"
#include "Core/EventBus/EventBus.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/Runtime.h"
#include "Core/WokwiDefaultOverrides.h"
#include <stdint.h>
#include <stddef.h>

#ifndef FLOW_HA_ONESHOT_DISCOVERY
#define FLOW_HA_ONESHOT_DISCOVERY 0
#endif

class HAModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::Ha; }
    const char* taskName() const override { return "ha"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 2560; }
    UBaseType_t taskStackCaps() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
        return Module::taskStackCaps();
#endif
    }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::EventBus;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::DataStore;
        if (i == 3) return ModuleId::Mqtt;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry& services) override;
    bool canStart(ConfigStore&, ServiceRegistry& services) override {
#if defined(FLOW_PROFILE_WAVESHARE)
        const NetworkAccessService* net = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
        return net && net->mode && net->mode(net->ctx) == NetworkAccessMode::Station;
#else
        (void)services;
        return true;
#endif
    }
    void onStart(ConfigStore&, ServiceRegistry&) override { setStartupReady(true); }
    void loop() override;
    uint32_t startDelayMs() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return 7000U;
#else
        return Limits::Boot::HaStartDelayMs;
#endif
    }
    void setStartupReady(bool ready);
    void setBranding(const char* objectPrefix,
                     const char* originName,
                     const char* vendor,
                     const char* model);

private:
    static constexpr uint8_t ProducerId = 32;
    static constexpr uint8_t ProducerIdCfg = 49;

    static constexpr uint8_t MAX_HA_SENSORS = Limits::Ha::Capacity::MaxSensors;
    static constexpr uint8_t MAX_HA_BINARY_SENSORS = Limits::Ha::Capacity::MaxBinarySensors;
    static constexpr uint8_t MAX_HA_SWITCHES = Limits::Ha::Capacity::MaxSwitches;
    static constexpr uint8_t MAX_HA_NUMBERS = Limits::Ha::Capacity::MaxNumbers;
    static constexpr uint8_t MAX_HA_BUTTONS = Limits::Ha::Capacity::MaxButtons;
    static constexpr uint8_t MAX_HA_SELECTS = Limits::Ha::Capacity::MaxSelects;
    static constexpr uint16_t MAX_HA_ENTITIES =
        MAX_HA_SENSORS + MAX_HA_BINARY_SENSORS + MAX_HA_SWITCHES + MAX_HA_NUMBERS + MAX_HA_BUTTONS + MAX_HA_SELECTS;
    static constexpr uint16_t MAX_HA_MESSAGES = MAX_HA_ENTITIES;
    static constexpr uint16_t HA_PENDING_WORDS = (MAX_HA_MESSAGES + 31U) / 32U;

    struct HAConfig {
        bool enabled = true;
        char vendor[32] = "flow.io";
        char deviceId[32] = "";
        char entityPrefix[16] = FLOW_WIRDEF_HA_ENTITY_PREFIX;
        char discoveryPrefix[32] = "homeassistant";
        char model[40] = "Flow Controller";
    };

    const EventBusService* eventBusSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const MqttService* mqttSvc_ = nullptr;

    HAConfig cfgData_{};
    bool startupReady_ = false;
    bool producerRegistered_ = false;
    bool published_ = false;
    bool oneShotCompleted_ = false;
    bool oneShotResourcesReleased_ = false;

    char deviceId_[32] = {0};
    char deviceIdent_[64] = {0};
    char nodeTopicId_[32] = {0};
    char deviceName_[Limits::Mqtt::Buffers::DeviceName] = {0};
    char objectPrefix_[8] = "fio";
    char originName_[32] = "flow.io";

    char stateTopicBuf_[96] = {0};
    char objectIdBuf_[96] = {0};
    char commandTopicBuf_[96] = {0};

#if FLOW_HA_ONESHOT_DISCOVERY
    HASensorEntry* sensors_ = nullptr;
    HABinarySensorEntry* binarySensors_ = nullptr;
    HASwitchEntry* switches_ = nullptr;
    HANumberEntry* numbers_ = nullptr;
    HAButtonEntry* buttons_ = nullptr;
    HASelectEntry* selects_ = nullptr;
    uint32_t* pendingBits_ = nullptr;
#else
    HASensorEntry sensors_[MAX_HA_SENSORS]{};
    HABinarySensorEntry binarySensors_[MAX_HA_BINARY_SENSORS]{};
    HASwitchEntry switches_[MAX_HA_SWITCHES]{};
    HANumberEntry numbers_[MAX_HA_NUMBERS]{};
    HAButtonEntry buttons_[MAX_HA_BUTTONS]{};
    HASelectEntry selects_[MAX_HA_SELECTS]{};
    uint32_t pendingBits_[HA_PENDING_WORDS] = {0};
#endif
    uint8_t sensorCount_ = 0;
    uint8_t binarySensorCount_ = 0;
    uint8_t switchCount_ = 0;
    uint8_t numberCount_ = 0;
    uint8_t buttonCount_ = 0;
    uint8_t selectCount_ = 0;

    MqttPublishProducer producer_{};
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;

    ConfigVariable<bool,0> enabledVar {
        NVS_KEY(NvsKeys::Ha::Enabled),"enabled","ha",ConfigType::Bool,
        &cfgData_.enabled,ConfigPersistence::Persistent,0
    };
    ConfigVariable<char,0> vendorVar {
        NVS_KEY(NvsKeys::Ha::Vendor),"vendor","ha",ConfigType::CharArray,
        (char*)cfgData_.vendor,ConfigPersistence::Persistent,sizeof(cfgData_.vendor)
    };
    ConfigVariable<char,0> deviceIdVar {
        NVS_KEY(NvsKeys::Ha::DeviceId),"device_id","ha",ConfigType::CharArray,
        (char*)cfgData_.deviceId,ConfigPersistence::Persistent,sizeof(cfgData_.deviceId)
    };
    ConfigVariable<char,0> entityPrefixVar {
        NVS_KEY(NvsKeys::Ha::EntityPrefix),"entity_prefix","ha",ConfigType::CharArray,
        (char*)cfgData_.entityPrefix,ConfigPersistence::Persistent,sizeof(cfgData_.entityPrefix)
    };
    ConfigVariable<char,0> prefixVar {
        NVS_KEY(NvsKeys::Ha::DiscoveryPrefix),"disc_prefix","ha",ConfigType::CharArray,
        (char*)cfgData_.discoveryPrefix,ConfigPersistence::Persistent,sizeof(cfgData_.discoveryPrefix)
    };
    ConfigVariable<char,0> modelVar {
        NVS_KEY(NvsKeys::Ha::Model),"model","ha",ConfigType::CharArray,
        (char*)cfgData_.model,ConfigPersistence::Persistent,sizeof(cfgData_.model)
    };

    static void onEventStatic(const Event& e, void* user);
    void onEvent(const Event& e);

    bool addSensorSvc_(const HASensorEntry* entry);
    bool addBinarySensorSvc_(const HABinarySensorEntry* entry);
    bool addSwitchSvc_(const HASwitchEntry* entry);
    bool addNumberSvc_(const HANumberEntry* entry);
    bool addSelectSvc_(const HASelectEntry* entry);
    bool addButtonSvc_(const HAButtonEntry* entry);
    bool requestRefreshSvc_();
    bool ensureStorage_();
    void releaseOneShotResources_();
    size_t entityTableUsedBytes_() const;
    size_t entityTableCapacityBytes_() const;
    bool addSensorEntry(const HASensorEntry& entry);
    bool addBinarySensorEntry(const HABinarySensorEntry& entry);
    bool addSwitchEntry(const HASwitchEntry& entry);
    bool addNumberEntry(const HANumberEntry& entry);
    bool addSelectEntry(const HASelectEntry& entry);
    bool addButtonEntry(const HAButtonEntry& entry);

    void requestAutoconfigRefresh();
    void refreshIdentityFromConfig();
    void refreshDeviceNameFromMqttConfig_();
    bool enqueuePending_(MqttPublishPriority prio);
    bool discoveryReady_() const;
    void markAllPending_();
    bool isPending_(uint16_t messageId) const;
    void setPending_(uint16_t messageId, bool pending);
    bool anyPending_() const;
    uint16_t entityCount_() const;
    uint16_t messageCount_() const;

    static MqttBuildResult producerBuildStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);
    static void producerPublishedStatic_(void* ctx, uint16_t messageId);
    static void producerDroppedStatic_(void* ctx, uint16_t messageId);

    MqttBuildResult buildMessage_(uint16_t messageId, MqttBuildContext& buildCtx);
    void onMessagePublished_(uint16_t messageId);
    void onMessageDropped_(uint16_t messageId);
    void markBootLogCaptureCompleteAfterDiscovery_();

    bool buildEntityMessage_(uint16_t messageId, MqttBuildContext& buildCtx);
    bool resolveMqttTopicDeviceId_(char* out, size_t outLen) const;

    bool buildObjectId(const char* suffix, char* out, size_t outLen) const;
    bool buildDefaultEntityId(const char* component, const char* objectId, char* out, size_t outLen) const;
    bool buildUniqueId(const char* objectId, const char* name, char* out, size_t outLen) const;

    bool publishSensor(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* entityCategory = nullptr,
                       const char* icon = nullptr,
                       const char* unit = nullptr,
                       bool hasEntityName = false,
                       const char* availabilityTemplate = nullptr,
                       bool isText = false,
                       MqttBuildContext* outCtx = nullptr);
    bool publishBinarySensor(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* deviceClass = nullptr,
                             const char* entityCategory = nullptr,
                             const char* icon = nullptr,
                             MqttBuildContext* outCtx = nullptr);
    bool publishSwitch(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* commandTopic,
                       const char* payloadOn, const char* payloadOff,
                       const char* icon = nullptr,
                       const char* entityCategory = nullptr,
                       MqttBuildContext* outCtx = nullptr);
    bool publishNumber(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* commandTopic, const char* commandTemplate,
                       float minValue, float maxValue, float step,
                       const char* mode = "slider",
                       const char* entityCategory = nullptr,
                       const char* icon = nullptr,
                       const char* unit = nullptr,
                       MqttBuildContext* outCtx = nullptr);
    bool publishSelect(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* commandTopic, const char* commandTemplate,
                       const char* optionsJson,
                       const char* icon = nullptr,
                       const char* entityCategory = nullptr,
                       MqttBuildContext* outCtx = nullptr);
    bool publishButton(const char* objectId, const char* name,
                       const char* commandTopic, const char* payloadPress,
                       const char* entityCategory = nullptr,
                       const char* icon = nullptr,
                       MqttBuildContext* outCtx = nullptr);
    bool publishDiscovery(const char* component, const char* objectId, MqttBuildContext& outCtx);

    static void makeDeviceId(char* out, size_t len);
    static void makeHexNodeId(char* out, size_t len);
    static void sanitizeId(const char* in, char* out, size_t outLen);

    HAService haSvc_{
        ServiceBinding::bind<&HAModule::addSensorSvc_>,
        ServiceBinding::bind<&HAModule::addBinarySensorSvc_>,
        ServiceBinding::bind<&HAModule::addSwitchSvc_>,
        ServiceBinding::bind<&HAModule::addNumberSvc_>,
        ServiceBinding::bind<&HAModule::addSelectSvc_>,
        ServiceBinding::bind<&HAModule::addButtonSvc_>,
        ServiceBinding::bind<&HAModule::requestRefreshSvc_>,
        this
    };
};
