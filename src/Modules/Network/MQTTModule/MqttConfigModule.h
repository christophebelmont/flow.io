#pragma once
/**
 * @file MqttConfigModule.h
 * @brief Passive MQTT config registrar for provisioning-only boots.
 */

#include "Core/ModulePassive.h"
#include "Core/ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/SystemLimits.h"
#include "Core/WokwiDefaultOverrides.h"

struct MqttConfigData {
#if defined(FLOW_PROFILE_WAVESHARE)
    bool enabled = false;
#else
    bool enabled = FLOW_WIRDEF_MQ_EN;
#endif
    char host[Limits::Mqtt::Buffers::Host] = FLOW_WIRDEF_MQ_HOST;
    int32_t port = FLOW_WIRDEF_MQ_PORT;
    char user[Limits::Mqtt::Buffers::User] = FLOW_WIRDEF_MQ_USER;
    char pass[Limits::Mqtt::Buffers::Pass] = FLOW_WIRDEF_MQ_PASS;
    char baseTopic[Limits::Mqtt::Buffers::BaseTopic] = FLOW_WIRDEF_MQ_BASE;
    char topicDeviceId[Limits::Mqtt::Buffers::DeviceId] = FLOW_WIRDEF_MQ_TID;
    char deviceName[Limits::Mqtt::Buffers::DeviceName] = "";
};

class MqttConfigModule : public ModulePassive {
public:
    ModuleId moduleId() const override { return ModuleId::Mqtt; }
    void init(ConfigStore& cfg, ServiceRegistry&) override;

private:
    MqttConfigData cfgData_{};

    ConfigVariable<char,0> hostVar_{
        NVS_KEY(NvsKeys::Mqtt::Host), "host", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.host, ConfigPersistence::Persistent, sizeof(cfgData_.host)
    };
    ConfigVariable<int32_t,0> portVar_{
        NVS_KEY(NvsKeys::Mqtt::Port), "port", "mqtt", ConfigType::Int32,
        &cfgData_.port, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<char,0> userVar_{
        NVS_KEY(NvsKeys::Mqtt::User), "user", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.user, ConfigPersistence::Persistent, sizeof(cfgData_.user)
    };
    ConfigVariable<char,0> passVar_{
        NVS_KEY(NvsKeys::Mqtt::Pass), "pass", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.pass, ConfigPersistence::Persistent, sizeof(cfgData_.pass)
    };
    ConfigVariable<char,0> baseTopicVar_{
        NVS_KEY(NvsKeys::Mqtt::BaseTopic), "baseTopic", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.baseTopic, ConfigPersistence::Persistent, sizeof(cfgData_.baseTopic)
    };
    ConfigVariable<char,0> topicDeviceIdVar_{
        NVS_KEY(NvsKeys::Mqtt::TopicDeviceId), "topicDeviceId", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.topicDeviceId, ConfigPersistence::Persistent, sizeof(cfgData_.topicDeviceId)
    };
    ConfigVariable<char,0> deviceNameVar_{
        NVS_KEY(NvsKeys::Mqtt::DeviceName), "deviceName", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.deviceName, ConfigPersistence::Persistent, sizeof(cfgData_.deviceName)
    };
    ConfigVariable<bool,0> enabledVar_{
        NVS_KEY(NvsKeys::Mqtt::Enabled), "enabled", "mqtt", ConfigType::Bool,
        &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };
};
