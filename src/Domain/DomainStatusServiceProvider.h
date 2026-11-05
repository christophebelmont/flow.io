#pragma once
/**
 * @file DomainStatusServiceProvider.h
 * @brief Shared evaluator backing DomainStatusService.
 */

#include "Core/ServiceBinding.h"
#include "Core/Services/IDomainStatus.h"
#include "Domain/DomainSpec.h"

class DomainStatusServiceProvider {
public:
    void configure(const DomainSpec& domain,
                   bool (*bindingPortExists)(uint16_t bindingPort));
    void bindServices(const IOServiceV2* ioSvc, const PoolDeviceService* poolSvc);
    const DomainStatusService& service() const { return service_; }

private:
    bool slotStatus_(DomainSlotId domainSlot, DomainSlotStatus* outStatus) const;
    bool summary_(DomainStatusSummary* outSummary) const;
    bool hasDomainSlotError_() const;
    bool firstError_(DomainSlotStatus* outStatus) const;

    const DomainSlotPreset* findDomainSlot_(DomainSlotId domainSlot) const;
    const DomainIoSlotBinding* findBinding_(DomainSlotId domainSlot) const;
    const PoolDevicePreset* findPoolDevice_(DomainSlotId domainSlot) const;
    bool bindingPortExists_(uint16_t bindingPort) const;

    const DomainSpec* domain_ = nullptr;
    bool (*bindingPortExistsFn_)(uint16_t bindingPort) = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const PoolDeviceService* poolSvc_ = nullptr;
    DomainStatusService service_{
        ServiceBinding::bind<&DomainStatusServiceProvider::slotStatus_>,
        ServiceBinding::bind<&DomainStatusServiceProvider::summary_>,
        ServiceBinding::bind<&DomainStatusServiceProvider::hasDomainSlotError_>,
        ServiceBinding::bind<&DomainStatusServiceProvider::firstError_>,
        this
    };
};
