/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "host_impl.h"

#include "http/webserver.h"
#include "logging/logger.h"
#include "utils/command_processor.h"
#include "world/modules/base.hpp"
#include "world/server.h"

#include <cstring>
#include <httplib.h>
#include <string>

namespace Framework::Integrations::Server::Plugins {

    /*
     * Bridging between the C ABI's opaque FwPlayer* and the world's
     * flecs::entity. The dispatcher fills a stack-local PlayerHandle and
     * hands its address to plugin callbacks. The handle is valid only for
     * the duration of the dispatched call.
     */
    struct PlayerHandle {
        HostImpl *host;
        uint64_t  entityId;
        uint64_t  guid;
    };

    /* ----------------------------------------------------------------------- */
    /* Vtable function implementations                                          */
    /* ----------------------------------------------------------------------- */

    static FwLogger *VT_logger_for(FwHost *host, const char *plugin_name) {
        auto *impl = static_cast<HostImpl *>(host->internal);
        /* No name (or empty) → return the plugin's default logger. */
        if (!plugin_name || !*plugin_name) {
            return reinterpret_cast<FwLogger *>(impl->logger.get());
        }

        /* Namespace every plugin-requested logger under "plugin:" so a
         * plugin can't impersonate a Framework-internal logger by passing
         * names like FRAMEWORK_INNER_SERVER. */
        std::string fullName = std::string("plugin:") + plugin_name;

        std::lock_guard lock(impl->loggerCacheMutex);
        auto            it = impl->loggerCache.find(fullName);
        if (it != impl->loggerCache.end()) {
            return reinterpret_cast<FwLogger *>(it->second.get());
        }
        auto sub                       = Framework::Logging::GetLogger(fullName.c_str());
        auto [inserted, _]             = impl->loggerCache.emplace(fullName, std::move(sub));
        return reinterpret_cast<FwLogger *>(inserted->second.get());
    }

    static void VT_log_debug(FwLogger *logger, const char *message) {
        if (logger && message) reinterpret_cast<spdlog::logger *>(logger)->debug("{}", message);
    }
    static void VT_log_info(FwLogger *logger, const char *message) {
        if (logger && message) reinterpret_cast<spdlog::logger *>(logger)->info("{}", message);
    }
    static void VT_log_warn(FwLogger *logger, const char *message) {
        if (logger && message) reinterpret_cast<spdlog::logger *>(logger)->warn("{}", message);
    }
    static void VT_log_error(FwLogger *logger, const char *message) {
        if (logger && message) reinterpret_cast<spdlog::logger *>(logger)->error("{}", message);
    }

    static int VT_register_command(FwHost *host, const char *name, const char *description, FwCommandCallback callback, void *userdata) {
        auto *impl = static_cast<HostImpl *>(host->internal);
        if (!impl->commandProcessor || !name || !callback) return 1;

        std::string nameStr(name);
        std::string descStr(description ? description : "");

        /* cxxopts treats the first parsed token as the program name and
         * excludes it from unmatched(); we re-prepend the command name so
         * the plugin sees the conventional argv[0]==command shape. */
        auto proc = [callback, userdata, nameStr](cxxopts::ParseResult &result) {
            std::vector<std::string>  args = result.unmatched();
            std::vector<const char *> argv;
            argv.reserve(args.size() + 1);
            argv.push_back(nameStr.c_str());
            for (auto &a : args) argv.push_back(a.c_str());
            try {
                callback(static_cast<int>(argv.size()), argv.data(), userdata);
            }
            catch (const std::exception &e) {
                Framework::Logging::GetLogger("plugins")->error("Plugin command '{}' threw: {}", nameStr, e.what());
            }
            catch (...) {
                Framework::Logging::GetLogger("plugins")->error("Plugin command '{}' threw non-std exception", nameStr);
            }
        };

        auto result = impl->commandProcessor->RegisterCommand(nameStr, std::initializer_list<cxxopts::Option> {}, proc, descStr);
        if (result.GetError() != Framework::Utils::CommandProcessorError::COMMAND_NONE) {
            return static_cast<int>(result.GetError());
        }
        impl->registeredCommands.push_back(nameStr);
        return 0;
    }

    static int VT_register_http_endpoint(FwHost *host, const char *path, FwHttpCallback callback, void *userdata) {
        auto *impl = static_cast<HostImpl *>(host->internal);
        if (!impl->webserver || !path || !callback) return 1;

        std::string pathStr(path);
        impl->webserver->RegisterRequest(pathStr, [callback, userdata, pathStr](const httplib::Request &req, httplib::Response &res) {
            try {
                callback(req.method.c_str(), req.path.c_str(), req.body.data(), req.body.size(), reinterpret_cast<FwHttpResponse *>(&res), userdata);
            }
            catch (const std::exception &e) {
                Framework::Logging::GetLogger("plugins")->error("Plugin HTTP handler '{}' threw: {}", pathStr, e.what());
                /* Drop whatever the handler partially wrote so the client
                 * doesn't get a 500 with a half-built body. */
                res.body.clear();
                res.status = 500;
            }
            catch (...) {
                Framework::Logging::GetLogger("plugins")->error("Plugin HTTP handler '{}' threw non-std exception", pathStr);
                res.body.clear();
                res.status = 500;
            }
        });
        return 0;
    }

    static void VT_http_response_set_status(FwHttpResponse *response, int status) {
        if (response) reinterpret_cast<httplib::Response *>(response)->status = status;
    }

    static void VT_http_response_set_body(FwHttpResponse *response, const char *body, size_t body_len) {
        if (response && body) reinterpret_cast<httplib::Response *>(response)->body.assign(body, body_len);
    }

    static void VT_http_response_set_header(FwHttpResponse *response, const char *key, const char *value) {
        if (response && key && value) reinterpret_cast<httplib::Response *>(response)->set_header(key, value);
    }

    static int VT_on_player_connect(FwHost *host, FwPlayerEventCallback callback, void *userdata) {
        auto *impl = static_cast<HostImpl *>(host->internal);
        if (!callback) return 1;
        auto slot      = std::make_unique<HostImpl::PlayerEventSlot>();
        slot->fn       = callback;
        slot->userdata = userdata;
        std::lock_guard lock(impl->slotMutex);
        impl->onConnect.push_back(std::move(slot));
        return 0;
    }

    static int VT_on_player_disconnect(FwHost *host, FwPlayerEventCallback callback, void *userdata) {
        auto *impl = static_cast<HostImpl *>(host->internal);
        if (!callback) return 1;
        auto slot      = std::make_unique<HostImpl::PlayerEventSlot>();
        slot->fn       = callback;
        slot->userdata = userdata;
        std::lock_guard lock(impl->slotMutex);
        impl->onDisconnect.push_back(std::move(slot));
        return 0;
    }

    static uint64_t VT_player_get_guid(FwPlayer *player) {
        if (!player) return 0;
        return reinterpret_cast<PlayerHandle *>(player)->guid;
    }

    static size_t VT_player_get_nickname(FwPlayer *player, char *buf, size_t buf_size) {
        if (!player) return 0;
        auto *handle = reinterpret_cast<PlayerHandle *>(player);
        if (!handle->host || !handle->host->worldEngine) return 0;
        auto world = handle->host->worldEngine->GetWorld();
        if (!world) return 0;
        flecs::entity e(*world, handle->entityId);
        if (!e.is_valid()) return 0;
        const auto *streamer = e.get<Framework::World::Modules::Base::Streamer>();
        if (!streamer) return 0;
        const std::string &nick = streamer->nickname;
        if (buf && buf_size > 0) {
            const size_t toCopy = std::min(nick.size(), buf_size - 1);
            std::memcpy(buf, nick.data(), toCopy);
            buf[toCopy] = '\0';
        }
        return nick.size();
    }

    /* ----------------------------------------------------------------------- */
    /* Public surface                                                           */
    /* ----------------------------------------------------------------------- */

    const FwHostVTable *GetHostVTable() {
        static const FwHostVTable vt = {
            /* abi_version              */ FW_PLUGIN_ABI_VERSION,
            /* logger_for               */ &VT_logger_for,
            /* log_debug                */ &VT_log_debug,
            /* log_info                 */ &VT_log_info,
            /* log_warn                 */ &VT_log_warn,
            /* log_error                */ &VT_log_error,
            /* register_command         */ &VT_register_command,
            /* register_http_endpoint   */ &VT_register_http_endpoint,
            /* http_response_set_status */ &VT_http_response_set_status,
            /* http_response_set_body   */ &VT_http_response_set_body,
            /* http_response_set_header */ &VT_http_response_set_header,
            /* on_player_connect        */ &VT_on_player_connect,
            /* on_player_disconnect     */ &VT_on_player_disconnect,
            /* player_get_guid          */ &VT_player_get_guid,
            /* player_get_nickname      */ &VT_player_get_nickname,
        };
        return &vt;
    }

    FwHost MakeFwHost(HostImpl *impl) {
        FwHost host {};
        host.vtable   = GetHostVTable();
        host.internal = impl;
        return host;
    }

    void DispatchPlayerConnect(HostImpl *impl, uint64_t entityId, uint64_t guid) {
        if (!impl) return;
        PlayerHandle handle {impl, entityId, guid};
        /* Copy the slot list under the lock so callbacks can register more
         * slots without us iterating a mutating vector. */
        std::vector<HostImpl::PlayerEventSlot> snapshot;
        {
            std::lock_guard lock(impl->slotMutex);
            snapshot.reserve(impl->onConnect.size());
            for (auto &slot : impl->onConnect) snapshot.push_back(*slot);
        }
        for (auto &slot : snapshot) {
            try {
                slot.fn(reinterpret_cast<FwPlayer *>(&handle), guid, slot.userdata);
            }
            catch (const std::exception &e) {
                Framework::Logging::GetLogger("plugins")->error("Plugin '{}' onConnect threw: {}", impl->pluginName, e.what());
            }
            catch (...) {
                Framework::Logging::GetLogger("plugins")->error("Plugin '{}' onConnect threw non-std exception", impl->pluginName);
            }
        }
    }

    void DispatchPlayerDisconnect(HostImpl *impl, uint64_t entityId, uint64_t guid) {
        if (!impl) return;
        PlayerHandle handle {impl, entityId, guid};
        std::vector<HostImpl::PlayerEventSlot> snapshot;
        {
            std::lock_guard lock(impl->slotMutex);
            snapshot.reserve(impl->onDisconnect.size());
            for (auto &slot : impl->onDisconnect) snapshot.push_back(*slot);
        }
        for (auto &slot : snapshot) {
            try {
                slot.fn(reinterpret_cast<FwPlayer *>(&handle), guid, slot.userdata);
            }
            catch (const std::exception &e) {
                Framework::Logging::GetLogger("plugins")->error("Plugin '{}' onDisconnect threw: {}", impl->pluginName, e.what());
            }
            catch (...) {
                Framework::Logging::GetLogger("plugins")->error("Plugin '{}' onDisconnect threw non-std exception", impl->pluginName);
            }
        }
    }

} // namespace Framework::Integrations::Server::Plugins
