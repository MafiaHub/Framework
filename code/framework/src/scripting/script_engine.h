/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <utils/lifecycle.h>

namespace Framework::Scripting {

    class ResourceManager; // Forward declaration for resource context tracking

    enum class Language : uint8_t {
        JavaScript,
        Lua
    };

    /**
     * What ResourceManager needs of a scripting engine, whatever language its scripts are written in.
     * Engine is the JavaScript one, LuaEngine the Lua one.
     */
    class ScriptEngine: public Framework::Lifecycle {
      public:
        virtual Language GetLanguage() const = 0;

        // Whether this engine runs the script file at `path`. Every file belongs to exactly one engine.
        virtual bool HandlesFile(std::string_view path) const = 0;

        // Advance timers and whatever else waits, without blocking.
        virtual void Tick() = 0;

        /**
         * Create the runtime a resource's scripts run in.
         * @return false with the reason in GetLastError().
         */
        virtual bool CreateResourceRuntime(const std::string &resourceName) = 0;

        // Destroy it, and with it everything the resource left running there. A runtime that is executing is destroyed
        // at the next tick instead.
        virtual void DestroyResourceRuntime(const std::string &resourceName) = 0;

        /**
         * Run one of a resource's script files in its runtime.
         * @return false with the reason in GetLastError().
         */
        virtual bool ExecuteResourceFile(const std::string &resourceName, std::string_view filepath) = 0;

        /**
         * Get the last error message.
         */
        const std::string &GetLastError() const {
            return _lastError;
        }

        /**
         * Set the owning ResourceManager for resource context tracking.
         * Called by ResourceManager during construction.
         */
        void SetResourceManager(ResourceManager *manager) {
            _resourceManager = manager;
        }

        /**
         * Get the owning ResourceManager.
         */
        ResourceManager *GetResourceManager() const {
            return _resourceManager;
        }

      protected:
        std::string _lastError;
        ResourceManager *_resourceManager = nullptr;
    };

} // namespace Framework::Scripting
