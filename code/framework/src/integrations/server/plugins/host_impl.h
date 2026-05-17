/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "sdk/fw_plugin_abi.h"

#include <memory>
#include <mutex>
#include <spdlog/logger.h>
#include <string>
#include <unordered_map>
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
     * Concrete backing for FwHost::internal. One instance per loaded plugin
     * so callback userdata lifetimes match the plugin's own lifetime, and
     * we can tear everything down at unload without leaking handles.
     *
     * Stores raw subsystem pointers — never owns. The owning Server::Instance
     * outlives every plugin (plugins unload during Instance::Shutdown).
     */
    struct HostImpl {
        std::string                      pluginName;
        Framework::HTTP::Webserver      *webserver        = nullptr;
        Framework::Utils::CommandProcessor *commandProcessor = nullptr;
        Framework::World::ServerEngine  *worldEngine      = nullptr;

        std::shared_ptr<spdlog::logger> logger; /* scoped: "plugin:<name>" */

        /* Names of commands the plugin registered, so we can deregister
         * cleanly when the plugin unloads. */
        std::vector<std::string> registeredCommands;

        /* Per-event callback slots. Pointers are stable across vector growth
         * because we store unique_ptrs. */
        struct PlayerEventSlot {
            FwPlayerEventCallback fn;
            void                 *userdata;
        };
        std::vector<std::unique_ptr<PlayerEventSlot>> onConnect;
        std::vector<std::unique_ptr<PlayerEventSlot>> onDisconnect;

        std::mutex slotMutex; /* guards onConnect/onDisconnect for safe iteration */
    };

    /* Returns the shared, statically-initialised host vtable. Pointer is
     * valid for the lifetime of the process and identical across plugins. */
    const FwHostVTable *GetHostVTable();

    /* Construct a FwHost owning the given HostImpl. The caller owns the impl;
     * the returned FwHost references it by raw pointer. */
    FwHost MakeFwHost(HostImpl *impl);

    /* Invoke every registered onConnect callback for the given entity/guid.
     * Exceptions thrown by plugin callbacks are caught and logged; one
     * misbehaving plugin does not prevent others from running. */
    void DispatchPlayerConnect(HostImpl *impl, uint64_t entityId, uint64_t guid);
    void DispatchPlayerDisconnect(HostImpl *impl, uint64_t entityId, uint64_t guid);

} // namespace Framework::Integrations::Server::Plugins
