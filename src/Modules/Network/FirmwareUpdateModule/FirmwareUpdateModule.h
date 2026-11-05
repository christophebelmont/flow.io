#pragma once
/**
 * @file FirmwareUpdateModule.h
 * @brief Firmware updater.
 */

#include "Core/Module.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/ConfigTypes.h"
#include "Core/CommandRegistry.h"

struct BoardSpec;

class FirmwareUpdateModule : public Module {
public:
    explicit FirmwareUpdateModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::FirmwareUpdate; }
    const char* taskName() const override { return "fwupdate"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return 6144;
#else
        return 8192;
#endif
    }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    uint32_t startDelayMs() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return 6000U;
#else
        return 0U;
#endif
    }

    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::Wifi;
        if (i == 2) return ModuleId::Command;
        if (i == 3) return ModuleId::WebInterface;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static constexpr size_t kUrlLen = 192;
    static constexpr size_t kMsgLen = 120;

    enum class UpdateState : uint8_t {
        Idle = 0,
        Queued,
        Downloading,
        Flashing,
        Rebooting,
        Done,
        Error
    };

    struct UpdateJob {
        bool pending = false;
        FirmwareUpdateTarget target = FirmwareUpdateTarget::Waveshare;
        char url[kUrlLen] = {0};
    };

    struct UpdateStatus {
        UpdateState state = UpdateState::Idle;
        FirmwareUpdateTarget target = FirmwareUpdateTarget::Waveshare;
        uint8_t progress = 0;
        uint32_t updatedAtMs = 0;
        char msg[kMsgLen] = {0};
    };

    struct ManifestCheckJob {
        bool pending = false;
        uint32_t requestId = 0;
        char url[kUrlLen] = {0};
    };

    struct ConfigData {
        char updateHost[64] = "";
        char updatePath[64] = "/binary";
    } cfgData_{};

    ConfigVariable<char, 2> updateHostVar_{
        NVS_KEY("up_host"), "update_host", "fwupdate",
        ConfigType::CharArray, cfgData_.updateHost, ConfigPersistence::Persistent, sizeof(cfgData_.updateHost)
    };
    ConfigVariable<char, 2> updatePathVar_{
        NVS_KEY("up_base_path"), "update_path", "fwupdate",
        ConfigType::CharArray, cfgData_.updatePath, ConfigPersistence::Persistent, sizeof(cfgData_.updatePath)
    };
    ServiceRegistry* services_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    const WebInterfaceService* webInterfaceSvc_ = nullptr;
    const FlowCfgRemoteService* flowCfgSvc_ = nullptr;
    const HmiService* hmiSvc_ = nullptr;

    int8_t flowIoEnablePin_ = -1;
    int8_t nextionRxPin_ = -1;
    int8_t nextionTxPin_ = -1;
    int8_t nextionRebootPin_ = -1;
    uint32_t nextionUploadBaud_ = 115200U;

    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    UpdateJob queuedJob_{};
    UpdateStatus status_{};
    ManifestCheckJob manifestCheckJob_{};
    FirmwareManifestCheckSnapshot manifestCheck_{};
    char* manifestPayload_ = nullptr;
    uint32_t nextManifestRequestId_ = 0;
    uint8_t manifestCopyReaders_ = 0;
    bool nextionRebootQueued_ = false;
    bool busy_ = false;
    bool hmiOtaActive_ = false;
    uint32_t activeTotalBytes_ = 0;
    uint32_t activeSentBytes_ = 0;

    static bool cmdStatus_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdWaveshare_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdNextion_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdNextionReboot_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSpiffs_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    bool startUpdate_(FirmwareUpdateTarget target, const char* url, char* errOut, size_t errOutLen);
    bool queueNextionReboot_(char* errOut, size_t errOutLen);
    bool statusJson_(char* out, size_t outLen);
    bool isBusy_();
    bool configJson_(char* out, size_t outLen) const;
    bool startManifestCheck_(uint32_t* requestIdOut, char* errOut, size_t errOutLen);
    bool manifestCheckStatus_(uint32_t requestId, FirmwareManifestCheckSnapshot* out);
    bool copyManifestResult_(uint32_t requestId,
                             char* out,
                             size_t outLen,
                             size_t* copiedLenOut);
    bool setConfig_(const char* updateHost,
                    const char* updatePath,
                    char* errOut,
                    size_t errOutLen);
    bool runJob_(const UpdateJob& job);
    bool runManifestCheck_(const ManifestCheckJob& job,
                           size_t* payloadLenOut,
                           char* errOut,
                           size_t errOutLen);
    bool runWaveshareUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runNextionUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runNextionReboot_(char* errOut, size_t errOutLen);
    bool runSpiffsUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool resolveUrl_(FirmwareUpdateTarget target,
                     const char* explicitUrl,
                     char* out,
                     size_t outLen,
                     char* errOut,
                     size_t errOutLen) const;
    bool resolveUpdateUrl_(const char* path,
                           char* out,
                           size_t outLen,
                           char* errOut,
                           size_t errOutLen) const;
    bool parseUrlArg_(const CommandRequest& req, char* out, size_t outLen) const;
    void setStatus_(UpdateState state, FirmwareUpdateTarget target, uint8_t progress, const char* msg);
    void setError_(FirmwareUpdateTarget target, const char* msg);
    void setHmiOtaCondition_(bool active);
    void onProgressChunk_(uint32_t chunkBytes);
    void attachWebInterfaceSvcIfNeeded_();
    void attachFlowCfgSvcIfNeeded_();
    bool setFlowCfgPaused_(bool paused);
    static const char* stateStr_(UpdateState s);
    static const char* targetStr_(FirmwareUpdateTarget t);

    FirmwareUpdateService firmwareUpdateSvc_{
        ServiceBinding::bind<&FirmwareUpdateModule::startUpdate_>,
        ServiceBinding::bind<&FirmwareUpdateModule::statusJson_>,
        ServiceBinding::bind<&FirmwareUpdateModule::isBusy_>,
        ServiceBinding::bind<&FirmwareUpdateModule::configJson_>,
        ServiceBinding::bind<&FirmwareUpdateModule::startManifestCheck_>,
        ServiceBinding::bind<&FirmwareUpdateModule::manifestCheckStatus_>,
        ServiceBinding::bind<&FirmwareUpdateModule::copyManifestResult_>,
        ServiceBinding::bind<&FirmwareUpdateModule::setConfig_>,
        this
    };
};
