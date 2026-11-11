/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "call.h"

#include "../module.h"

#include <core_modules.h>

#include <mutex>
#include <unordered_map>

namespace Framework::Scripting::Bind {
    namespace {
        // Runtimes may be set up on different threads.
        std::mutex &ManagersMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<const void *, ResourceManager *> &Managers() {
            static std::unordered_map<const void *, ResourceManager *> managers;
            return managers;
        }
    } // namespace

    void SetCallResourceManager(const void *runtime, ResourceManager *manager) {
        std::scoped_lock lock(ManagersMutex());
        if (manager != nullptr) {
            Managers()[runtime] = manager;
        }
        else {
            Managers().erase(runtime);
        }
    }

    namespace Detail {
        ResourceManager *CallResourceManager(const void *runtime) {
            {
                std::scoped_lock lock(ManagersMutex());
                const auto it = Managers().find(runtime);
                if (it != Managers().end()) {
                    return it->second;
                }
            }
            const ScriptingModule *module = CoreModules::GetScriptingModule();
            return module != nullptr ? module->GetResourceManager() : nullptr;
        }
    } // namespace Detail
} // namespace Framework::Scripting::Bind
