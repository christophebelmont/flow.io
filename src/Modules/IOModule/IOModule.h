#pragma once
/**
 * @file IOModule.h
 * @brief Unified IO module with endpoint registry and scheduler.
 */

#include "Board/BoardSpec.h"
#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/I2cBus.h"
#include "Core/RuntimeUi.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/SystemLimits.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Modules/IOModule/IODrivers/Ads1115Driver.h"
#include "Modules/IOModule/IODrivers/Bme680Driver.h"
#include "Modules/IOModule/IODrivers/Bmp280Driver.h"
#include "Modules/IOModule/IODrivers/Ds18b20Driver.h"
#include "Modules/IOModule/IODrivers/GpioDriver.h"
#include "Modules/IOModule/IODrivers/Ina226Driver.h"
#include "Modules/IOModule/IODrivers/Mcp23017BitDriver.h"
#include "Modules/IOModule/IODrivers/Mcp23017Driver.h"
#include "Modules/IOModule/IODrivers/PcntCounterDriver.h"
#include "Modules/IOModule/IODrivers/Pcf8574BitDriver.h"
#include "Modules/IOModule/IODrivers/Pcf8574Driver.h"
#include "Modules/IOModule/IODrivers/Sht40Driver.h"
#include "Modules/IOModule/IODrivers/Tca9554BitDriver.h"
#include "Modules/IOModule/IODrivers/Tca9554Driver.h"
#include "Modules/IOModule/IOEndpoints/AnalogSensorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/DigitalActuatorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/DigitalSensorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/RunningMedianAverageFloat.h"
#include "Modules/IOModule/IOModuleDataModel.h"
#include "Modules/IOModule/IOModuleTypes.h"
#include "Modules/IOModule/IOProviders/IOProviders.h"
#include "Modules/IOModule/IORegistry/IORegistry.h"
#include "Modules/IOModule/IOScheduler/IOScheduler.h"
#include <stdio.h>

class DataStore;
class OneWireBus;
struct IOConfigDescriptorStorage;

class IOModule : public Module, public IRuntimeSnapshotProvider, public IRuntimeUiValueProvider {
public:
    IOModule() = default;
    explicit IOModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::Io; }
    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }
    const char* taskName() const override { return "io"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 2560; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    UBaseType_t taskStackCaps() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
        return Module::taskStackCaps();
#endif
    }

#if defined(FLOW_PROFILE_SUPERVISOR)
    uint8_t dependencyCount() const override { return 3; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        if (i == 2) return ModuleId::ConfigStore;
        return ModuleId::Unknown;
    }
#else
    uint8_t dependencyCount() const override { return 3; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        if (i == 2) return ModuleId::ConfigStore;
        return ModuleId::Unknown;
    }
#endif

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;
    void onStart(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;
    uint32_t startDelayMs() const override { return Limits::Boot::IoStartDelayMs; }

    void setOneWireBuses(OneWireBus* water, OneWireBus* air);
    void setBindingPorts(const IOBindingPortSpec* ports, uint8_t count);
    void setExpanders(const IOExpanderSpec* expanders, uint8_t count);
    bool defineAnalogInput(const IOAnalogDefinition& def);
    bool applyAnalogInputDefaults(const IOAnalogDefinition& def);
    bool defineDigitalInput(const IODigitalInputDefinition& def);
    bool defineDigitalOutput(const IODigitalOutputDefinition& def);
    const char* analogSlotName(uint8_t idx) const;
    bool analogSlotUsed(uint8_t idx) const;
    bool analogSlotPublished(uint8_t idx) const;
    bool digitalInputSlotUsed(uint8_t logicalIdx) const;
    bool digitalInputSlotPublished(uint8_t logicalIdx) const;
    uint8_t digitalInputValueType(uint8_t logicalIdx) const;
    int32_t digitalInputPrecision(uint8_t logicalIdx) const;
    bool digitalOutputSlotUsed(uint8_t logicalIdx) const;
    bool digitalOutputSlotWritable(uint8_t logicalIdx) const;
    int32_t analogPrecision(uint8_t idx) const;
    uint32_t takeAnalogConfigDirtyMask();
    const char* endpointLabel(const char* endpointId) const;
    bool buildInputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const;
    bool buildOutputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const;
    uint8_t runtimeSnapshotCount() const override;
    const char* runtimeSnapshotSuffix(uint8_t idx) const override;
    RuntimeRouteClass runtimeSnapshotClass(uint8_t idx) const override;
    bool runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const override;
    bool buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const override;
    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override;

    IORegistry& registry() { return registry_; }

private:
    struct AnalogSlot;
    struct DigitalSlot;

    enum RuntimeUiValueId : uint8_t {
        RuntimeUiWaterTemp = 1,
        RuntimeUiAirTemp = 2,
        RuntimeUiPh = 3,
        RuntimeUiOrp = 4,
        RuntimeUiWaterCounter = 5,
        RuntimeUiPsi = 6,
        RuntimeUiBmp280Temp = 7,
        RuntimeUiBme680Temp = 8,
        RuntimeUiBmp280Pressure = 9,
        RuntimeUiSht40Temperature = 10,
        RuntimeUiSht40Humidity = 11,
        RuntimeUiBme680Humidity = 12,
        RuntimeUiBme680Pressure = 13,
        RuntimeUiBme680Gaz = 14,
    };

    static bool tickFastAds_(void* ctx, uint32_t nowMs);
    static bool tickSlowDs_(void* ctx, uint32_t nowMs);
    static bool tickI2cAnalogs_(void* ctx, uint32_t nowMs);
    static bool tickDigitalInputs_(void* ctx, uint32_t nowMs);

    uint8_t ioCount_() const;
    IoStatus ioIdAt_(uint8_t index, IoId* outId) const;
    IoStatus ioMeta_(IoId id, IoEndpointMeta* outMeta) const;
    IoStatus ioRuntimeStatus_(IoId id, IoRuntimeStatus* outStatus) const;
    IoStatus ioBindingPortStatus_(PhysicalPortId portId, IoRuntimeStatus* outStatus) const;
    IoStatus ioReadValue_(IoId id, IoValue* outValue) const;
    IoStatus ioReadDigital_(IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq) const;
    IoStatus ioWriteDigital_(IoId id, uint8_t on, uint32_t tsMs);
    IoStatus ioReadAnalog_(IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq) const;
    IoStatus ioTick_(uint32_t nowMs);
    IoStatus ioLastCycle_(IoCycleInfo* outCycle) const;
    IoStatus ioSensorStatus_(IoId id, IoSensorStatus* outStatus) const;
    IoStatus ioListInvalidSensors_(IoId* outIds, uint8_t maxIds, uint8_t* outCount) const;

    bool configureRuntime_();
    const IOBindingPortSpec* bindingPortSpec_(PhysicalPortId portId) const;
    bool resolveAnalogBinding_(PhysicalPortId portId, uint8_t& sourceOut, uint8_t& channelOut, uint8_t& backendOut) const;
    bool resolveDigitalInputBinding_(PhysicalPortId portId,
                                     uint8_t& pinOut,
                                     uint8_t& backendOut,
                                     uint8_t& channelOut,
                                     IOExpanderId& expanderOut) const;
    bool resolveDigitalOutputBinding_(PhysicalPortId portId,
                                      uint8_t& pinOut,
                                      uint8_t& backendOut,
                                      uint8_t& channelOut,
                                      IOExpanderId& expanderOut,
                                      bool& usesPcfOut,
                                      bool& usesTcaOut,
                                      bool& usesMcpOut) const;
    bool resolveDsBusAddress_(OneWireBus* bus, const char* runtimeKey, uint8_t outAddr[8]);
    bool runtimeSnapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& routeTypeOut, uint8_t& slotIdxOut) const;
    bool buildEndpointSnapshot_(IOEndpoint* ep, char* out, size_t len, uint32_t& maxTsOut, bool invalidAsUndefined = false) const;
    bool buildGroupSnapshot_(char* out, size_t len, bool inputGroup, uint32_t& maxTsOut) const;
    const IOAnalogProvider* analogProviderForSource_(uint8_t source) const;
    bool writeAnalogProviderRuntimeValue_(RuntimeUiId runtimeId, uint8_t source, uint8_t channel, IRuntimeUiWriter& writer) const;
    bool resolveConfiguredAnalogSource_(uint8_t idx, uint8_t& sourceOut) const;
    bool analogSourceRequiresDriverEnable_(uint8_t source) const;
    bool analogSourceDriverEnabled_(uint8_t source) const;
    bool analogRuntimeRoutePublished_(uint8_t idx) const;
    bool digitalRuntimeRoutePublished_(uint8_t slotIdx) const;
    bool analogSlotPublished_(uint8_t idx) const;
    bool analogSlotUsesUndefinedInvalidValue_(uint8_t idx) const;
    void invalidateAnalogSlot_(AnalogSlot& slot, uint32_t nowMs);
    bool processAnalogDefinition_(uint8_t idx, uint32_t nowMs);
    bool processDigitalInputDefinition_(uint8_t slotIdx, uint32_t nowMs);
    int32_t sanitizeAnalogPrecision_(int32_t precision) const;
    void forceAnalogSnapshotPublish_(uint8_t analogIdx, uint32_t nowMs);
    void refreshAnalogConfigState_();
    bool ensureAnalogPrecisionState_();
    bool ensureConfigDescriptorStorage_();
    bool ensureScalableStorage_();
    bool ensureDigitalCounterConfigState_();
    bool ensureLastCycleState_();
    bool endpointIndexFromId_(const char* id, uint8_t& idxOut) const;
    void configureRuntimeAfterConfig_();
    bool digitalLogicalUsed_(uint8_t kind, uint8_t logicalIdx) const;
    bool findDigitalSlotByLogical_(uint8_t kind, uint8_t logicalIdx, uint8_t& slotIdxOut) const;
    bool findDigitalSlotByIoId_(IoId id, uint8_t& slotIdxOut) const;
    ConfigVariable<float,0>* counterTotalVar_(uint8_t logicalIdx);
    float* counterConfigTotalState_(uint8_t logicalIdx);
    void eraseLegacyCounterPersistedTotal_(uint8_t logicalIdx);
    bool persistCounterTotalIfNeeded_(DigitalSlot& slot, int32_t rawCount, uint32_t nowMs);
    void traceDigitalCounters_(uint32_t nowMs);
    void beginIoCycle_(uint32_t nowMs);
    void markIoCycleChanged_(IoId id);
    void logI2cConfigTrace_(const char* stage) const;
    const IOExpanderSpec* expanderSpec_(IOExpanderId expanderId) const;
    IOExpanderConfig* expanderConfig_(IOExpanderId expanderId);
    const IOExpanderConfig* expanderConfig_(IOExpanderId expanderId) const;
    bool expanderEnabled_(IOExpanderId expanderId) const;
    bool expanderUsable_(IOExpanderId expanderId) const;
    uint8_t expanderAddress_(IOExpanderId expanderId) const;
    uint8_t expanderMaskDefault_(IOExpanderId expanderId) const;
    bool expanderOutputsInverted_(IOExpanderId expanderId) const;
    bool validateExpanderTopology_();
    static bool writeDigitalOut_(void* ctx, bool on);
    void applyBoardDefaults_(const BoardSpec& board);
    void pollPulseOutputs_(uint32_t nowMs);
    AnalogSensorEndpoint* allocAnalogEndpoint_(const char* endpointId);
    DigitalSensorEndpoint* allocDigitalSensorEndpoint_(const char* endpointId, uint8_t valueType);
    DigitalActuatorEndpoint* allocDigitalActuatorEndpoint_(const char* endpointId, DigitalWriteFn writeFn, void* writeCtx);
    IDigitalCounterDriver* allocGpioDriver_(const char* driverId,
                                            uint8_t pin,
                                            bool output,
                                            bool activeHigh,
                                            uint8_t inputPullMode = GpioDriver::PullNone,
                                            bool counterEnabled = false,
                                            uint8_t edgeMode = IO_EDGE_RISING,
                                            uint32_t counterDebounceUs = 0);
    IAnalogSourceDriver* allocAdsDriver_(const char* driverId, I2CBus* bus, const Ads1115DriverConfig& cfg);
    IAnalogSourceDriver* allocDsDriver_(const char* driverId, OneWireBus* bus, const uint8_t address[8], const Ds18b20DriverConfig& cfg);
    IAnalogSourceDriver* allocSht40Driver_(const char* driverId, I2CBus* bus, const Sht40DriverConfig& cfg);
    IAnalogSourceDriver* allocBmp280Driver_(const char* driverId, I2CBus* bus, const Bmp280DriverConfig& cfg);
    IAnalogSourceDriver* allocBme680Driver_(const char* driverId, I2CBus* bus, const Bme680DriverConfig& cfg);
    IAnalogSourceDriver* allocIna226Driver_(const char* driverId, I2CBus* bus, const Ina226DriverConfig& cfg);
    IDigitalPinDriver* allocPcfBitDriver_(const char* driverId, Pcf8574Driver* parent, uint8_t bit, bool activeHigh);
    IDigitalPinDriver* allocTcaBitDriver_(const char* driverId, Tca9554Driver* parent, uint8_t bit, bool activeHigh);
    IDigitalPinDriver* allocMcpBitDriver_(const char* driverId,
                                          Mcp23017Driver* parent,
                                          uint8_t bit,
                                          bool activeHigh,
                                          bool output,
                                          uint8_t inputPullMode = GpioDriver::PullNone);
    IMaskOutputDriver* beginMaskExpander_(IOExpanderId expanderId, uint8_t expectedKind, bool preserveHardwareState);
    Mcp23017Driver* beginMcpExpander_(IOExpanderId expanderId);
    IMaskOutputDriver* allocPcfDriver_(const char* driverId, I2CBus* bus, uint8_t address);
    IMaskOutputDriver* allocTcaDriver_(const char* driverId, I2CBus* bus, uint8_t address);
    Mcp23017Driver* allocMcpDriver_(const char* driverId, I2CBus* bus, uint8_t address);

    static constexpr uint8_t MAX_ANALOG_ENDPOINTS = Limits::Io::MaxAnalogEndpoints;
    static constexpr uint8_t MAX_DIGITAL_INPUTS = Limits::Io::MaxDigitalInputs;
    static constexpr uint8_t MAX_DIGITAL_OUTPUTS = Limits::Io::MaxDigitalOutputs;
    static constexpr uint8_t MAX_DIGITAL_SLOTS = MAX_DIGITAL_INPUTS + MAX_DIGITAL_OUTPUTS;
    static_assert(
        (uint16_t)MAX_ANALOG_ENDPOINTS + (uint16_t)MAX_DIGITAL_INPUTS +
            (uint16_t)MAX_DIGITAL_OUTPUTS <= IO_MAX_ENDPOINTS,
        "Declared I/O runtime capacities exceed IORegistry capacity"
    );
    static constexpr uint8_t ANALOG_CFG_SLOTS = Limits::Io::AnalogConfigSlots;
    static constexpr uint8_t DIGITAL_INPUT_CFG_SLOTS = Limits::Io::DigitalInputConfigSlots;
    static constexpr uint8_t DIGITAL_CFG_SLOTS = Limits::Io::DigitalOutputConfigSlots;
    static constexpr uint8_t ANALOG_CFG_STORAGE_SLOTS = (ANALOG_CFG_SLOTS < 17U) ? 17U : ANALOG_CFG_SLOTS;
    static constexpr uint8_t DIGITAL_INPUT_CFG_STORAGE_SLOTS =
        (DIGITAL_INPUT_CFG_SLOTS < 8U) ? 8U : DIGITAL_INPUT_CFG_SLOTS;
    static constexpr uint8_t DIGITAL_CFG_STORAGE_SLOTS = (DIGITAL_CFG_SLOTS < 16U) ? 16U : DIGITAL_CFG_SLOTS;
    static_assert(ANALOG_CFG_SLOTS <= 32U, "I/O descriptors support analog slots a00..a31");
    static_assert(DIGITAL_INPUT_CFG_SLOTS <= 16U, "I/O descriptors support digital input slots i00..i15");
    static_assert(DIGITAL_CFG_SLOTS <= 16U, "I/O descriptors support digital output slots d00..d15");
    /** End-exclusive upper bounds for each static id range. */
    static constexpr IoId IO_ID_DO_MAX = IO_ID_DO_BASE + MAX_DIGITAL_OUTPUTS;
    static constexpr IoId IO_ID_DI_MAX = IO_ID_DI_BASE + MAX_DIGITAL_INPUTS;
    static constexpr IoId IO_ID_AI_MAX = IO_ID_AI_BASE + MAX_ANALOG_ENDPOINTS;

    struct AnalogSlot {
        bool used = false;
        IoId ioId = IO_ID_INVALID;
        IOAnalogDefinition def{};
        // `source` identifies the shared physical driver; `channel` selects one logical measurement.
        uint8_t source = IO_SRC_ADS_INTERNAL_SINGLE;
        uint8_t channel = 0;
        uint8_t backend = IO_BACKEND_ADS1115_INT;
        AnalogSensorEndpoint* endpoint = nullptr;
        RunningMedianAverageFloat median{11, 5};
        bool lastSampleSeqValid = false;
        uint32_t lastSampleSeq = 0;
        bool lastRoundedValid = false;
        float lastRounded = 0.0f;
    };
    enum DigitalSlotKind : uint8_t {
        DIGITAL_SLOT_INPUT = 0,
        DIGITAL_SLOT_OUTPUT = 1
    };
    struct DigitalSlot {
        bool used = false;
        IoId ioId = IO_ID_INVALID;
        IOModule* owner = nullptr;
        uint8_t kind = DIGITAL_SLOT_INPUT;
        uint8_t logicalIdx = 0;
        char endpointId[8] = {0};
        IODigitalInputDefinition inDef{};
        IODigitalOutputDefinition outDef{};
        uint8_t backend = IO_BACKEND_GPIO;
        uint8_t channel = 0;
        IOExpanderId expanderId = IO_EXPANDER_INVALID;
        IODigitalProvider provider{};
        IOEndpoint* endpoint = nullptr;
        bool pulseArmed = false;
        uint32_t pulseDeadlineMs = 0;
        bool lastValid = false;
        bool lastValue = false;
        float counterScaledTotal = 0.0f;
        float counterLastPersistedTotal = 0.0f;
        int32_t counterLastRawCount = 0;
        int32_t counterLastFlushedRawCount = 0;
        uint32_t counterLastPersistMs = 0;
    };

    struct RuntimeExpander {
        const IOExpanderSpec* spec = nullptr;
        bool configValid = false;
        bool beginAttempted = false;
        bool beginOk = false;
        Pcf8574Driver* pcf = nullptr;
        Tca9554Driver* tca = nullptr;
        Mcp23017Driver* mcp = nullptr;
    };

    IOModuleConfig cfgData_{};
    IOExpanderConfig expanderCfg_[IO_MAX_EXPANDERS]{};
    IOAnalogSlotConfig analogCfg_[ANALOG_CFG_STORAGE_SLOTS]{};
    IODigitalInputSlotConfig digitalInCfg_[DIGITAL_INPUT_CFG_STORAGE_SLOTS]{};
    IODigitalOutputSlotConfig digitalCfg_[DIGITAL_CFG_STORAGE_SLOTS]{};
    const IOBindingPortSpec* bindingPorts_ = nullptr;
    uint8_t bindingPortCount_ = 0;
    const IOExpanderSpec* expanders_ = nullptr;
    uint8_t expanderCount_ = 0;

    ConfigStore* cfgStore_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    DataStore* dataStore_ = nullptr;
    MqttConfigRouteProducer cfgMqttPub_{};
    bool cfgMqttPubConfigured_ = false;

    IORegistry registry_{};
    IOScheduler scheduler_{};
    I2CBus* i2cBus_ = nullptr;

    OneWireBus* oneWireWater_ = nullptr;
    OneWireBus* oneWireAir_ = nullptr;
    uint8_t oneWireWaterAddr_[8] = {0};
    uint8_t oneWireAirAddr_[8] = {0};
    bool oneWireWaterAddrValid_ = false;
    bool oneWireAirAddrValid_ = false;

    IOAnalogProvider analogProviders_[IO_SRC_COUNT]{};
    RuntimeExpander runtimeExpanders_[IO_MAX_EXPANDERS]{};
    IOServiceV2 ioSvc_{
        ServiceBinding::bind<&IOModule::ioCount_>,
        ServiceBinding::bind<&IOModule::ioIdAt_>,
        ServiceBinding::bind<&IOModule::ioMeta_>,
        ServiceBinding::bind<&IOModule::ioRuntimeStatus_>,
        ServiceBinding::bind<&IOModule::ioBindingPortStatus_>,
        ServiceBinding::bind<&IOModule::ioReadValue_>,
        ServiceBinding::bind<&IOModule::ioReadDigital_>,
        ServiceBinding::bind<&IOModule::ioWriteDigital_>,
        ServiceBinding::bind<&IOModule::ioReadAnalog_>,
        ServiceBinding::bind<&IOModule::ioTick_>,
        ServiceBinding::bind<&IOModule::ioLastCycle_>,
        ServiceBinding::bind<&IOModule::ioSensorStatus_>,
        ServiceBinding::bind<&IOModule::ioListInvalidSensors_>,
        this
    };
    IoCycleInfo* lastCycle_ = nullptr;

    AnalogSlot* analogSlots_ = nullptr;
    DigitalSlot* digitalSlots_ = nullptr;
    AnalogSensorEndpoint* analogEndpointPool_ = nullptr;
    uint8_t (*digitalSensorEndpointPool_)[sizeof(DigitalSensorEndpoint)] = nullptr;
    uint8_t (*digitalActuatorEndpointPool_)[sizeof(DigitalActuatorEndpoint)] = nullptr;
    uint8_t (*gpioDriverPool_)[sizeof(GpioDriver)] = nullptr;
    alignas(PcntCounterDriver) uint8_t gpioCounterDriverPool_[MAX_DIGITAL_INPUTS][sizeof(PcntCounterDriver)]{};
    alignas(Ads1115Driver) uint8_t adsDriverPool_[2][sizeof(Ads1115Driver)]{};
    alignas(Ds18b20Driver) uint8_t dsDriverPool_[2][sizeof(Ds18b20Driver)]{};
    alignas(Sht40Driver) uint8_t sht40DriverPool_[1][sizeof(Sht40Driver)]{};
    alignas(Bmp280Driver) uint8_t bmp280DriverPool_[1][sizeof(Bmp280Driver)]{};
    alignas(Bme680Driver) uint8_t bme680DriverPool_[1][sizeof(Bme680Driver)]{};
    alignas(Ina226Driver) uint8_t ina226DriverPool_[1][sizeof(Ina226Driver)]{};
    alignas(Pcf8574Driver) uint8_t pcfDriverPool_[IO_MAX_EXPANDERS][sizeof(Pcf8574Driver)]{};
    alignas(Tca9554Driver) uint8_t tcaDriverPool_[IO_MAX_EXPANDERS][sizeof(Tca9554Driver)]{};
    alignas(Mcp23017Driver) uint8_t mcpDriverPool_[IO_MAX_EXPANDERS][sizeof(Mcp23017Driver)]{};
    uint8_t analogEndpointPoolUsed_ = 0;
    uint8_t digitalSensorEndpointPoolUsed_ = 0;
    uint8_t digitalActuatorEndpointPoolUsed_ = 0;
    uint8_t gpioDriverPoolUsed_ = 0;
    uint8_t gpioCounterDriverPoolUsed_ = 0;
    uint8_t pcfBitDriverPoolUsed_ = 0;
    uint8_t tcaBitDriverPoolUsed_ = 0;
    uint8_t mcpBitDriverPoolUsed_ = 0;
    uint8_t adsDriverPoolUsed_ = 0;
    uint8_t dsDriverPoolUsed_ = 0;
    uint8_t sht40DriverPoolUsed_ = 0;
    uint8_t bmp280DriverPoolUsed_ = 0;
    uint8_t bme680DriverPoolUsed_ = 0;
    uint8_t ina226DriverPoolUsed_ = 0;
    uint8_t pcfDriverPoolUsed_ = 0;
    uint8_t tcaDriverPoolUsed_ = 0;
    uint8_t mcpDriverPoolUsed_ = 0;
    bool runtimeReady_ = false;
    bool runtimeInitAttempted_ = false;
    int32_t boardDefaultI2cSda_ = FLOW_WIRDEF_IO_SDA;
    int32_t boardDefaultI2cScl_ = FLOW_WIRDEF_IO_SCL;
    const char* boardProfileName_ = "unknown";
    uint32_t counterTraceLastMs_ = 0;
    uint32_t analogCalcLogLastMs_[3]{0, 0, 0};
    int32_t* analogPrecisionLast_ = nullptr;
    float* digitalCounterLastConfigTotals_ = nullptr;
    bool analogPrecisionLastInit_ = false;
    uint32_t analogConfigDirtyMask_ = 0;
    IOConfigDescriptorStorage* configDescriptors_ = nullptr;

    ConfigVariable<bool,0> enabledVar_ { NVS_KEY(NvsKeys::Io::IO_EN),"enabled","io",ConfigType::Bool,&cfgData_.enabled,ConfigPersistence::Persistent,0 };
#if defined(FLOW_BOARD_FLOWIO_S3)
    ConfigVariable<int32_t,0> i2cSdaVar_ { NVS_KEY(NvsKeys::Io::IO_SDA_S3),"sda","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cSda,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSclVar_ { NVS_KEY(NvsKeys::Io::IO_SCL_S3),"scl","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cScl,ConfigPersistence::Persistent,0 };
#else
    ConfigVariable<int32_t,0> i2cSdaVar_ { NVS_KEY(NvsKeys::Io::IO_SDA),"sda","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cSda,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSclVar_ { NVS_KEY(NvsKeys::Io::IO_SCL),"scl","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cScl,ConfigPersistence::Persistent,0 };
#endif
    ConfigVariable<int32_t,0> adsPollVar_ { NVS_KEY(NvsKeys::Io::IO_ADS),"poll_ms","io/drivers/ads1115",ConfigType::Int32,&cfgData_.adsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> dsPollVar_ { NVS_KEY(NvsKeys::Io::IO_DS),"poll_ms","io/drivers/ds18b20",ConfigType::Int32,&cfgData_.dsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> digitalPollVar_ { NVS_KEY(NvsKeys::Io::IO_DIN),"poll_ms","io/drivers/gpio",ConfigType::Int32,&cfgData_.digitalPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsInternalAddrVar_ { NVS_KEY(NvsKeys::Io::IO_AIAD),"address","io/drivers/ads1115_int",ConfigType::UInt8,&cfgData_.adsInternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsExternalAddrVar_ { NVS_KEY(NvsKeys::Io::IO_AEAD),"address","io/drivers/ads1115_ext",ConfigType::UInt8,&cfgData_.adsExternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsGainVar_ { NVS_KEY(NvsKeys::Io::IO_AGAI),"gain","io/drivers/ads1115",ConfigType::Int32,&cfgData_.adsGain,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsRateVar_ { NVS_KEY(NvsKeys::Io::IO_ARAT),"rate","io/drivers/ads1115",ConfigType::Int32,&cfgData_.adsRate,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> sht40EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_SHTEN),"enabled","io/drivers/sht40",ConfigType::Bool,&cfgData_.sht40Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> sht40AddressVar_ { NVS_KEY(NvsKeys::Io::IO_SHTAD),"address","io/drivers/sht40",ConfigType::UInt8,&cfgData_.sht40Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> sht40PollVar_ { NVS_KEY(NvsKeys::Io::IO_SHTPL),"poll_ms","io/drivers/sht40",ConfigType::Int32,&cfgData_.sht40PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> bmp280EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_BMPEN),"enabled","io/drivers/bmp280",ConfigType::Bool,&cfgData_.bmp280Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> bmp280AddressVar_ { NVS_KEY(NvsKeys::Io::IO_BMPAD),"address","io/drivers/bmp280",ConfigType::UInt8,&cfgData_.bmp280Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> bmp280PollVar_ { NVS_KEY(NvsKeys::Io::IO_BMPPL),"poll_ms","io/drivers/bmp280",ConfigType::Int32,&cfgData_.bmp280PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> bme680EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_BMEEN),"enabled","io/drivers/bme680",ConfigType::Bool,&cfgData_.bme680Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> bme680AddressVar_ { NVS_KEY(NvsKeys::Io::IO_BMEAD),"address","io/drivers/bme680",ConfigType::UInt8,&cfgData_.bme680Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> bme680PollVar_ { NVS_KEY(NvsKeys::Io::IO_BMEPL),"poll_ms","io/drivers/bme680",ConfigType::Int32,&cfgData_.bme680PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> ina226EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_INAEN),"enabled","io/drivers/ina226",ConfigType::Bool,&cfgData_.ina226Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> ina226AddressVar_ { NVS_KEY(NvsKeys::Io::IO_INAAD),"address","io/drivers/ina226",ConfigType::UInt8,&cfgData_.ina226Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> ina226PollVar_ { NVS_KEY(NvsKeys::Io::IO_INAPL),"poll_ms","io/drivers/ina226",ConfigType::Int32,&cfgData_.ina226PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<float,0> ina226ShuntOhmsVar_ { NVS_KEY(NvsKeys::Io::IO_INASH),"shunt_ohms","io/drivers/ina226",ConfigType::Float,&cfgData_.ina226ShuntOhms,ConfigPersistence::Persistent,0 };
#define FLOW_IO_EXPANDER_CFG_DECL(INDEX, SLOT_STR, KEYEN, KEYAD, KEYMK) \
    ConfigVariable<bool,0> exp##INDEX##EnabledVar_{NVS_KEY(NvsKeys::Io::KEYEN),"enabled","io/drivers/expander" SLOT_STR,ConfigType::Bool,&expanderCfg_[INDEX].enabled,ConfigPersistence::Persistent,0}; \
    ConfigVariable<uint8_t,0> exp##INDEX##AddressVar_{NVS_KEY(NvsKeys::Io::KEYAD),"address","io/drivers/expander" SLOT_STR,ConfigType::UInt8,&expanderCfg_[INDEX].address,ConfigPersistence::Persistent,0}; \
    ConfigVariable<uint8_t,0> exp##INDEX##MaskDefaultVar_{NVS_KEY(NvsKeys::Io::KEYMK),"mask_default","io/drivers/expander" SLOT_STR,ConfigType::UInt8,&expanderCfg_[INDEX].maskDefault,ConfigPersistence::Persistent,0};
    FLOW_IO_EXPANDER_CFG_DECL(0, "00", IO_X0EN, IO_X0AD, IO_X0MK)
    FLOW_IO_EXPANDER_CFG_DECL(1, "01", IO_X1EN, IO_X1AD, IO_X1MK)
    FLOW_IO_EXPANDER_CFG_DECL(2, "02", IO_X2EN, IO_X2AD, IO_X2MK)
    FLOW_IO_EXPANDER_CFG_DECL(3, "03", IO_X3EN, IO_X3AD, IO_X3MK)
#undef FLOW_IO_EXPANDER_CFG_DECL
    ConfigVariable<bool,0> traceEnabledVar_ { NVS_KEY(NvsKeys::Io::IO_TREN),"trace_enabled","io/debug",ConfigType::Bool,&cfgData_.traceEnabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> tracePeriodVar_ { NVS_KEY(NvsKeys::Io::IO_TRMS),"trace_period_ms","io/debug",ConfigType::Int32,&cfgData_.tracePeriodMs,ConfigPersistence::Persistent,0 };

};
