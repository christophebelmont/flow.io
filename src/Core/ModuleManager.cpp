/**
 * @file ModuleManager.cpp
 * @brief Implementation file.
 */
#include "ModuleManager.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include "Core/Log.h"
#include "Core/LogModuleIds.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::CoreModuleManager)

namespace {
constexpr uint8_t kStartupPreparedFlag = 0x01U;
constexpr uint8_t kStartupActiveFlag = 0x02U;
constexpr uint8_t kStartupFailedFlag = 0x04U;

const char* stackCapsLabel_(UBaseType_t caps) {
    if ((caps & MALLOC_CAP_SPIRAM) != 0U) return "SPIRAM";
    if ((caps & MALLOC_CAP_INTERNAL) != 0U) return "INTERNAL";
    return "DEFAULT";
}
}

static void logRegisteredModules(Module* modules[], uint8_t count) {
    ///Logger::log(LogLevel::Info, "MOD", "Registered modules (%u):", count);
    for (uint8_t i = 0; i < count; ++i) {
        ///Logger::log(LogLevel::Info, "MOD", " - %s", toString(modules[i]->moduleId()));
    }
}

static void dbgDumpModules(Module* modules[], uint8_t count) {
    Log::debug(LOG_MODULE_ID, "registered modules count=%u", (unsigned)count);
    for (uint8_t i = 0; i < count; ++i) {
        if (!modules[i]) continue;
        Log::debug(LOG_MODULE_ID, "module=%s deps=%u",
                   toString(modules[i]->moduleId()),
                   (unsigned)modules[i]->dependencyCount());
        for (uint8_t d = 0; d < modules[i]->dependencyCount(); ++d) {
            const ModuleId dep = modules[i]->dependency(d);
            Log::debug(LOG_MODULE_ID, "module=%s needs=%s",
                       toString(modules[i]->moduleId()),
                       toString(dep));
        }
    }
}

static bool isValidTaskSpec(const ModuleTaskSpec& spec) {
    return spec.name && spec.name[0] != '\0' && spec.stackSize > 0 && spec.entry != nullptr;
}

bool ModuleManager::add(Module* m) {
    if (!m) {
        Log::error(LOG_MODULE_ID, "add failed: null module");
        return false;
    }

    const ModuleId moduleId = m->moduleId();
    if (!isValidModuleId(moduleId)) {
        Log::error(LOG_MODULE_ID, "add failed: invalid module id");
        return false;
    }

    if (count >= Limits::Core::Capacity::MaxModules) {
        Log::error(LOG_MODULE_ID, "add failed: module limit reached (%u)",
                   (unsigned)Limits::Core::Capacity::MaxModules);
        return false;
    }

    if (findById(moduleId)) {
        Log::error(LOG_MODULE_ID, "add failed: duplicate module id=%s", toString(moduleId));
        return false;
    }

    modules[count++] = m;
    modulesById[moduleIdIndex(moduleId)] = m;
    return true;
}

Module* ModuleManager::findById(ModuleId id) {
    if (!isValidModuleId(id)) return nullptr;
    return modulesById[moduleIdIndex(id)];
}

bool ModuleManager::buildInitOrder() {
    Log::debug(LOG_MODULE_ID, "buildInitOrder: count=%u", (unsigned)count);
    /// Kahn topo-sort
    bool placed[Limits::Core::Capacity::MaxModules] = {0};
    orderedCount = 0;

    for (uint8_t pass = 0; pass < count; ++pass) {
        bool progress = false;

        for (uint8_t i = 0; i < count; ++i) {
            Module* m = modules[i];
            if (!m || placed[i]) continue;

            /// Check if all dependencies are already placed
            bool depsOk = true;
            const uint8_t depCount = m->dependencyCount();

            for (uint8_t d = 0; d < depCount; ++d) {
                const ModuleId depId = m->dependency(d);
                if (!isValidModuleId(depId)) continue;

                Module* dep = findById(depId);
                if (!dep) {
                    Log::error(LOG_MODULE_ID, "missing dependency: module=%s requires=%s",
                               toString(m->moduleId()), toString(depId));
                    return false;
                }

                /// Is dep already placed in ordered[] ?
                bool depPlaced = false;
                for (uint8_t j = 0; j < count; ++j) {
                    if (modules[j] == dep) {
                        depPlaced = placed[j];
                        break;
                    }
                }

                if (!depPlaced) {
                    depsOk = false;
                    break;
                }
            }

            if (depsOk) {
                ordered[orderedCount++] = m;
                placed[i] = true;
                progress = true;
            }
        }
        
        if (orderedCount == count) {
            return true;
        }

        if (!progress) {
            Log::error(LOG_MODULE_ID, "cyclic or unresolved deps detected");
            for (uint8_t i = 0; i < count; ++i) {
                if (modules[i] && !placed[i]) {
                    Log::error(LOG_MODULE_ID, "not placed module=%s", toString(modules[i]->moduleId()));
                }
            }
            return false;
        }
    }

    Log::debug(LOG_MODULE_ID, "buildInitOrder: success (ordered=%u)", (unsigned)orderedCount);
    return true;
}


bool ModuleManager::initAll(ConfigStore& cfg, ServiceRegistry& services) {
    Log::debug(LOG_MODULE_ID, "initAll: moduleCount=%u", (unsigned)count);
    /*Serial.printf("[MOD] moduleCount=%d\n", count);
    for (int i=0;i<count;i++){
        Serial.printf("[MOD] module[%d]=%s\n", i, toString(modules[i]->moduleId()));
    }
    Serial.flush();
    delay(20);*/

    // dbgDumpModules(modules, count); // debug dump

    if (!buildInitOrder()) return false;

    for (uint8_t i = 0; i < orderedCount; ++i) {
        const ModuleId moduleId = ordered[i]->moduleId();
        const LogModuleId logModuleId = logModuleIdFromModuleId(moduleId);
        if (logModuleId != (LogModuleId)LogModuleIdValue::Unknown) {
            (void)Log::registerModule(logModuleId, toString(moduleId));
        }
        ///Logger::log(LogLevel::Info, "MOD", "Init %s", ordered[i]->moduleId());
        Log::debug(LOG_MODULE_ID, "init: %s", toString(moduleId));
        ordered[i]->init(cfg, services);
        if (logModuleId != (LogModuleId)LogModuleIdValue::Unknown) {
            (void)Log::registerModule(logModuleId, toString(moduleId));
        }
    }

    // Core shared dependencies must be injected before config callbacks and
    // before any module task can start mutating shared runtime state.
    wireCoreServices(services, cfg);

    /// Load persistent config after all modules registered their variables.
    cfg.loadPersistent();

    for (uint8_t i = 0; i < orderedCount; ++i) {
        ordered[i]->onConfigLoaded(cfg, services);
    }

    taskEntryCount = 0;
    for (uint8_t i = 0; i < orderedCount; ++i) {
        Module* module = ordered[i];
        if (!module) continue;
        module->resetPrimaryTaskHandle_();
    }

    startupEpochMs_ = millis();
    startupFlags_ = (uint8_t)(kStartupPreparedFlag | kStartupActiveFlag);
    startupStartedMask_ = 0U;

    if (!tickStartup(cfg, services)) {
        return false;
    }

    Log::debug(LOG_MODULE_ID, "initAll: startup prepared modules=%u", (unsigned)orderedCount);
    Log::debug(LOG_MODULE_ID, "initAll: done");
    
    return true;
}

bool ModuleManager::dependenciesStarted_(Module& module) const
{
    const uint8_t depCount = module.dependencyCount();
    for (uint8_t d = 0; d < depCount; ++d) {
        const ModuleId depId = module.dependency(d);
        if (!isValidModuleId(depId)) continue;

        bool found = false;
        for (uint8_t i = 0; i < orderedCount; ++i) {
            Module* dep = ordered[i];
            if (!dep || dep->moduleId() != depId) continue;
            found = true;
            if ((startupStartedMask_ & (1UL << i)) == 0U) return false;
            break;
        }
        if (!found) return false;
    }
    return true;
}

bool ModuleManager::startModule_(uint8_t orderedIdx, ConfigStore& cfg, ServiceRegistry& services)
{
    if (orderedIdx >= orderedCount) return false;
    if ((startupStartedMask_ & (1UL << orderedIdx)) != 0U) return true;

    Module* module = ordered[orderedIdx];
    if (!module) return false;
    if (!dependenciesStarted_(*module)) return true;
    if (!module->canStart(cfg, services)) return true;

    module->onStart(cfg, services);

    const uint8_t declaredTaskCount = module->taskCount();
    if (declaredTaskCount > 0) {
        const ModuleTaskSpec* specs = module->taskSpecs();
        if (!specs) {
            Log::error(LOG_MODULE_ID, "taskSpecs missing module=%s taskCount=%u",
                       toString(module->moduleId()),
                       (unsigned)declaredTaskCount);
            return false;
        }

        for (uint8_t taskIndex = 0; taskIndex < declaredTaskCount; ++taskIndex) {
            const ModuleTaskSpec spec = specs[taskIndex];
            if (!isValidTaskSpec(spec)) {
                Log::error(LOG_MODULE_ID, "invalid task spec module=%s index=%u",
                           toString(module->moduleId()),
                           (unsigned)taskIndex);
                return false;
            }
            if (taskEntryCount >= Limits::Core::Capacity::MaxModuleTasks) {
                Log::error(LOG_MODULE_ID, "task registry full at module=%s limit=%u",
                           toString(module->moduleId()),
                           (unsigned)Limits::Core::Capacity::MaxModuleTasks);
                return false;
            }

            TaskHandle_t handle = nullptr;
            const UBaseType_t stackCaps = module->taskStackCaps();
            Log::debug(LOG_MODULE_ID, "startTask module=%s task=%s core=%ld prio=%u stack=%lu",
                       toString(module->moduleId()),
                       spec.name,
                       (long)spec.coreId,
                       (unsigned)spec.priority,
                       (unsigned long)spec.stackSize);

            BaseType_t ok = pdFAIL;
            const bool useDefaultInternalCaps =
                ((stackCaps & MALLOC_CAP_SPIRAM) == 0U) &&
                ((stackCaps & MALLOC_CAP_INTERNAL) != 0U);
#if (configSUPPORT_STATIC_ALLOCATION == 1)
            if (!useDefaultInternalCaps) {
                ok = xTaskCreatePinnedToCoreWithCaps(
                    spec.entry,
                    spec.name,
                    spec.stackSize,
                    spec.context,
                    spec.priority,
                    &handle,
                    spec.coreId,
                    stackCaps
                );
            } else {
                ok = xTaskCreatePinnedToCore(
                    spec.entry,
                    spec.name,
                    spec.stackSize,
                    spec.context,
                    spec.priority,
                    &handle,
                    spec.coreId
                );
            }
#else
            ok = xTaskCreatePinnedToCore(
                spec.entry,
                spec.name,
                spec.stackSize,
                spec.context,
                spec.priority,
                &handle,
                spec.coreId
            );
#endif
            if (ok != pdPASS || !handle) {
                const uint32_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
                const uint32_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
                const uint32_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
                const uint32_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
                Log::error(LOG_MODULE_ID,
                           "startTask failed module=%s task=%s err=%ld stack=%lu caps=%s heap8=%lu largest8=%lu internal=%lu largest_internal=%lu",
                           toString(module->moduleId()),
                           spec.name,
                           (long)ok,
                           (unsigned long)spec.stackSize,
                           stackCapsLabel_(stackCaps),
                           (unsigned long)free8,
                           (unsigned long)largest8,
                           (unsigned long)freeInternal,
                           (unsigned long)largestInternal);
                return false;
            }

            const uint32_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            const uint32_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            const uint32_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            const uint32_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            Log::info(LOG_MODULE_ID,
                      "startTask ok module=%s task=%s stack=%lu caps=%s heap8=%lu largest8=%lu internal=%lu largest_internal=%lu",
                      toString(module->moduleId()),
                      spec.name,
                      (unsigned long)spec.stackSize,
                      stackCapsLabel_(stackCaps),
                      (unsigned long)free8,
                      (unsigned long)largest8,
                      (unsigned long)freeInternal,
                      (unsigned long)largestInternal);

            module->setPrimaryTaskHandle_(handle);
            taskEntries[taskEntryCount++] = {
                module,
                handle,
                taskIndex
            };
        }
    }

    startupStartedMask_ |= (1UL << orderedIdx);
    Log::debug(LOG_MODULE_ID, "startup release: module=%s delay_ms=%lu tasks=%u",
               toString(module->moduleId()),
               (unsigned long)module->startDelayMs(),
               (unsigned)declaredTaskCount);
    return true;
}

bool ModuleManager::removeTaskEntry(TaskHandle_t handle)
{
    if (!handle) return false;
    for (uint8_t i = 0; i < taskEntryCount; ++i) {
        if (taskEntries[i].handle != handle) continue;

        Module* module = taskEntries[i].module;
        if (module && module->getTaskHandle() == handle) {
            module->resetPrimaryTaskHandle_();
        }

        for (uint8_t j = i; (uint8_t)(j + 1U) < taskEntryCount; ++j) {
            taskEntries[j] = taskEntries[j + 1U];
        }
        --taskEntryCount;
        taskEntries[taskEntryCount] = {};
        return true;
    }
    return false;
}

bool ModuleManager::tickStartup(ConfigStore& cfg, ServiceRegistry& services)
{
    if ((startupFlags_ & kStartupPreparedFlag) == 0U) return true;
    if ((startupFlags_ & kStartupFailedFlag) != 0U) return false;
    if ((startupFlags_ & kStartupActiveFlag) == 0U) return true;

    const uint32_t elapsedMs = millis() - startupEpochMs_;
    bool allStarted = true;
    for (uint8_t i = 0; i < orderedCount; ++i) {
        if ((startupStartedMask_ & (1UL << i)) != 0U) continue;
        Module* module = ordered[i];
        if (!module) continue;

        const uint32_t releaseDelayMs = module->startDelayMs();
        if (elapsedMs < releaseDelayMs) {
            allStarted = false;
            continue;
        }

        const bool wasStarted = (startupStartedMask_ & (1UL << i)) != 0U;
        if (!startModule_(i, cfg, services)) {
            startupFlags_ |= kStartupFailedFlag;
            startupFlags_ &= (uint8_t)~kStartupActiveFlag;
            return false;
        }
        const bool nowStarted = (startupStartedMask_ & (1UL << i)) != 0U;
        if (!nowStarted) {
            allStarted = false;
        }
#if defined(FLOW_PROFILE_MICRONOVA)
        if (!wasStarted && nowStarted) {
            allStarted = false;
            break;
        }
#endif
    }

    if (allStarted) {
        startupFlags_ &= (uint8_t)~kStartupActiveFlag;
    }
    return true;
}

void ModuleManager::wireCoreServices(ServiceRegistry& services, ConfigStore& config) {

  auto* ebService = services.get<EventBusService>(ServiceId::EventBus);
  if (ebService && ebService->bus) {
    config.setEventBus(ebService->bus);
    Log::debug(LOG_MODULE_ID, "wireCoreServices: eventbus wired");
  }
}
