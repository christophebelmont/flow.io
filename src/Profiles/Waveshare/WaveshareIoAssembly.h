#pragma once

class AppContext;

namespace Profiles {
namespace Waveshare {

struct ModuleInstances;

void configureIoModule(const AppContext& ctx, ModuleInstances& modules);
void registerIoHomeAssistant(AppContext& ctx, ModuleInstances& modules);
void releaseIoHomeAssistantDiscoveryHeapIfDone(ModuleInstances& modules);
void refreshIoHomeAssistantIfNeeded(ModuleInstances& modules);

}  // namespace Waveshare
}  // namespace Profiles
