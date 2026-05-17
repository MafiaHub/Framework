/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "host_impl.h"
#include "loader.h"
#include "sdk/fw_plugin_abi.h"

#include <memory>
#include <string>
#include <vector>

namespace Framework {
    namespace HTTP {
        class Webserver;
    }
    namespace Utils {
        class CommandProcessor;
    }
    namespace World {
        class ServerEngine;
    }
} // namespace Framework

namespace Framework::Integrations::Server::Plugins {

    /*
     * Resolved plugin entry points, kept alongside the loaded library so
     * the function pointers stay valid for the plugin's lifetime.
     */
    struct LoadedPlugin {
        std::string                       name;
        std::string                       version;
        SharedLibrary                     library;
        std::unique_ptr<HostImpl>         impl;
        FwHost                            host {};

        const FwPluginInfo *(*infoFn)(void)                = nullptr;
        int  (*initFn)(FwHost *)                           = nullptr;
        void (*postScriptInitFn)(FwHost *)                 = nullptr;
        void (*updateFn)(FwHost *, double)                 = nullptr;
        void (*preShutdownFn)(FwHost *)                    = nullptr;
        void (*shutdownFn)(FwHost *)                       = nullptr;

        bool initSucceeded = false;
    };

    /*
     * Loads, ticks, and unloads native plugins listed in server.json.
     *
     * Lifecycle (called from Server::Instance):
     *   1. Init(webserver, commands, world)  — wire subsystem pointers
     *   2. LoadAll(modulesDir, names)        — find manifests, dlopen,
     *                                          validate ABI, run fw_plugin_init
     *   3. PostScriptInit()                  — after scripting engine is up
     *   4. Update(dt) every tick             — fw_plugin_update on each
     *   5. PreShutdown()                     — fw_plugin_pre_shutdown
     *   6. Shutdown()                        — fw_plugin_shutdown + unload
     *
     * The manager guarantees that exceptions thrown out of any plugin entry
     * point are caught and logged. A plugin that fails to load or that
     * throws during a lifecycle hook is disabled, not propagated.
     */
    class PluginManager final {
      public:
        PluginManager();
        ~PluginManager();

        void Init(Framework::HTTP::Webserver *webserver, Framework::Utils::CommandProcessor *commandProcessor, Framework::World::ServerEngine *worldEngine);

        /*
         * Load each plugin in `pluginNames`, in order. For each name, looks
         * up <modulesDir>/<name>/<name>.module.json. Plugins that fail to
         * load are logged and skipped; load failures are not fatal.
         * Returns the number of plugins successfully loaded.
         */
        size_t LoadAll(const std::string &modulesDir, const std::vector<std::string> &pluginNames);

        void PostScriptInit();
        void Update(double dtSeconds);
        void PreShutdown();
        void Shutdown();

        /*
         * Dispatch points wired from Instance's existing player connect /
         * disconnect callbacks. entityId is the flecs entity id; guid is
         * the network GUID.
         */
        void DispatchPlayerConnect(uint64_t entityId, uint64_t guid);
        void DispatchPlayerDisconnect(uint64_t entityId, uint64_t guid);

        size_t Count() const {
            return _plugins.size();
        }

      private:
        bool LoadOne(const std::string &modulesDir, const std::string &name);

        Framework::HTTP::Webserver         *_webserver        = nullptr;
        Framework::Utils::CommandProcessor *_commandProcessor = nullptr;
        Framework::World::ServerEngine     *_worldEngine      = nullptr;

        std::vector<std::unique_ptr<LoadedPlugin>> _plugins;
        bool _shutdownCalled = false;
    };

} // namespace Framework::Integrations::Server::Plugins
