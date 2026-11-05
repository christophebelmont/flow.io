#pragma once
/**
 * @file ConfigStoreModule.h
 * @brief Module that exposes ConfigStore service.
 */
#include "Core/EventBus/EventPayloads.h"
#include "Core/Module.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include <freertos/queue.h>

/**
 * @brief Config module wiring JSON services and serialized NVS persistence.
 */
class ConfigStoreModule : public Module {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::ConfigStore; }
    const char* taskName() const override { return "cfg.persist"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 3072; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    /** @brief Config module depends on log hub. */
    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override { return (i == 0) ? ModuleId::LogHub : ModuleId::Unknown; }

    /** @brief Register config services. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    enum class PersistenceOp : uint8_t {
        WriteBlob,
        EraseKey,
        PersistFloat
    };

    struct PersistenceRequest {
        PersistenceOp op = PersistenceOp::EraseKey;
        char key[Limits::MaxNvsKeyLen + 1] = {0};
        uint8_t len = 0;
        uint8_t bytes[Limits::Config::Capacity::RuntimeBlobAsyncMax] = {0};
        float floatValue = 0.0f;
        uint8_t moduleId = (uint8_t)ConfigModuleId::Unknown;
        uint8_t localBranchId = ConfigBranchRef::UnknownLocalBranch;
        char moduleName[sizeof(ConfigChangedPayload::module)] = {0};
    };

    static constexpr uint8_t kPersistenceQueueLen = Limits::Config::Capacity::PersistenceQueueLen;
    static constexpr size_t kPersistenceBlobMax = Limits::Config::Capacity::RuntimeBlobAsyncMax;

    ConfigStore* registry = nullptr;
    ServiceRegistry* services_ = nullptr;
    const LogHubService* logHub = nullptr;
    const ActivityLogService* activityLog_ = nullptr;
    QueueHandle_t persistenceQ_ = nullptr;
    StaticQueue_t persistenceQStatic_{};
    uint8_t* persistenceQStorage_ = nullptr;
#if !defined(FLOW_PROFILE_WAVESHARE)
    uint8_t persistenceQStorageLocal_[kPersistenceQueueLen * sizeof(PersistenceRequest)]{};
#endif
    bool persistenceQStorageInPsram_ = false;

    bool applyJson_(const char* json);
    void emitApplyJsonActivity_(const char* json);
    void toJson_(char* out, size_t outLen);
    bool toJsonModule_(const char* module, char* out, size_t outLen, bool* truncated);
    uint8_t listModules_(const char** out, uint8_t max);
    bool erase_();
    bool readRuntimeBlob_(const char* key, void* out, size_t outLen, size_t* actualLen);
    bool writeRuntimeBlob_(const char* key, const void* value, size_t len);
    bool eraseKey_(const char* key);
    bool writeRuntimeBlobAsync_(const char* key, const void* value, size_t len);
    bool eraseKeyAsync_(const char* key);
    bool persistFloatAsync_(const char* key,
                            float value,
                            const char* moduleName,
                            uint8_t moduleId,
                            uint8_t localBranchId);
    bool enqueuePersistence_(const PersistenceRequest& req);
    void processPersistence_(const PersistenceRequest& req);

    ConfigStoreService svc_{
        ServiceBinding::bind<&ConfigStoreModule::applyJson_>,
        ServiceBinding::bind<&ConfigStoreModule::toJson_>,
        ServiceBinding::bind<&ConfigStoreModule::toJsonModule_>,
        ServiceBinding::bind<&ConfigStoreModule::listModules_>,
        ServiceBinding::bind<&ConfigStoreModule::erase_>,
        ServiceBinding::bind<&ConfigStoreModule::readRuntimeBlob_>,
        ServiceBinding::bind<&ConfigStoreModule::writeRuntimeBlob_>,
        ServiceBinding::bind<&ConfigStoreModule::eraseKey_>,
        ServiceBinding::bind<&ConfigStoreModule::writeRuntimeBlobAsync_>,
        ServiceBinding::bind<&ConfigStoreModule::eraseKeyAsync_>,
        ServiceBinding::bind<&ConfigStoreModule::persistFloatAsync_>,
        this
    };
};
