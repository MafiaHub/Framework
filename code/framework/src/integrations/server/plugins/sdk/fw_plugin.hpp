/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

/*
 * Framework Server Plugin SDK — C++ wrapper
 * ===========================================================================
 *
 * Header-only C++17 ergonomics layered on top of fw_plugin_abi.h. Plugin
 * authors get type-safe lambdas, RAII handles, and a single macro to wire
 * up the required C exports.
 *
 * Minimal plugin:
 *
 *   #include "fw_plugin.hpp"
 *
 *   class MyPlugin : public Framework::Plugin::Base {
 *     public:
 *       int OnInit(Framework::Plugin::Host &host) override {
 *           auto log = host.LoggerFor("my-plugin");
 *           log.Info("hello, framework");
 *
 *           host.RegisterCommand("greet", "Say hi to a player",
 *               [log](int argc, const char *const *argv) {
 *                   log.Info(argc > 1 ? argv[1] : "anonymous");
 *               });
 *
 *           host.OnPlayerConnect([log](Framework::Plugin::Player &p) {
 *               log.Info(p.Nickname() + " joined");
 *           });
 *           return 0;
 *       }
 *   };
 *
 *   FW_PLUGIN_DECLARE(MyPlugin, "my-plugin", "1.0.0")
 *
 * Callback storage: lambdas are heap-allocated and owned by the Host wrapper
 * for the lifetime of the plugin. There is no Unregister API in v1 — all
 * registrations live for the plugin's lifetime and are torn down on unload.
 * ===========================================================================
 */

#pragma once

#include "fw_plugin_abi.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Plugin {

    /* --------------------------------------------------------------------- */
    /* Logger                                                                 */
    /* --------------------------------------------------------------------- */

    class Logger final {
      public:
        Logger() = default;
        Logger(const FwHostVTable *vtable, FwLogger *logger): _vtable(vtable), _logger(logger) {}

        void Debug(const std::string &msg) const {
            if (_vtable && _logger) _vtable->log_debug(_logger, msg.c_str());
        }
        void Info(const std::string &msg) const {
            if (_vtable && _logger) _vtable->log_info(_logger, msg.c_str());
        }
        void Warn(const std::string &msg) const {
            if (_vtable && _logger) _vtable->log_warn(_logger, msg.c_str());
        }
        void Error(const std::string &msg) const {
            if (_vtable && _logger) _vtable->log_error(_logger, msg.c_str());
        }

      private:
        const FwHostVTable *_vtable = nullptr;
        FwLogger           *_logger = nullptr;
    };

    /* --------------------------------------------------------------------- */
    /* Player                                                                 */
    /* --------------------------------------------------------------------- */

    class Player final {
      public:
        Player(const FwHostVTable *vtable, FwPlayer *player, uint64_t guid): _vtable(vtable), _player(player), _guid(guid) {}

        uint64_t Guid() const {
            return _guid;
        }

        std::string Nickname() const {
            if (!_vtable || !_player) return {};
            char   stackBuf[128];
            size_t needed = _vtable->player_get_nickname(_player, stackBuf, sizeof(stackBuf));
            if (needed < sizeof(stackBuf)) {
                return std::string(stackBuf, needed);
            }
            std::string heap(needed, '\0');
            _vtable->player_get_nickname(_player, heap.data(), heap.size() + 1);
            return heap;
        }

      private:
        const FwHostVTable *_vtable = nullptr;
        FwPlayer           *_player = nullptr;
        uint64_t            _guid   = 0;
    };

    /* --------------------------------------------------------------------- */
    /* HTTP response builder                                                  */
    /* --------------------------------------------------------------------- */

    class HttpResponse final {
      public:
        HttpResponse(const FwHostVTable *vtable, FwHttpResponse *response): _vtable(vtable), _response(response) {}

        void SetStatus(int status) const {
            _vtable->http_response_set_status(_response, status);
        }
        void SetBody(std::string_view body) const {
            _vtable->http_response_set_body(_response, body.data(), body.size());
        }
        void SetHeader(const std::string &key, const std::string &value) const {
            _vtable->http_response_set_header(_response, key.c_str(), value.c_str());
        }

      private:
        const FwHostVTable *_vtable;
        FwHttpResponse     *_response;
    };

    /* --------------------------------------------------------------------- */
    /* Host wrapper                                                           */
    /* --------------------------------------------------------------------- */

    using CommandFn     = std::function<void(int argc, const char *const *argv)>;
    using HttpFn        = std::function<void(std::string_view method, std::string_view path, std::string_view body, HttpResponse &response)>;
    using PlayerEventFn = std::function<void(Player &player)>;

    class Host final {
      public:
        explicit Host(FwHost *host): _host(host), _vtable(host ? host->vtable : nullptr) {}

        bool Valid() const {
            return _host && _vtable && _vtable->abi_version == FW_PLUGIN_ABI_VERSION;
        }

        Logger LoggerFor(const std::string &pluginName) const {
            return Logger(_vtable, _vtable->logger_for(_host, pluginName.c_str()));
        }

        bool RegisterCommand(const std::string &name, const std::string &description, CommandFn callback) {
            auto slot = std::make_unique<CommandFn>(std::move(callback));
            int  rc   = _vtable->register_command(
                _host, name.c_str(), description.c_str(),
                [](int argc, const char *const *argv, void *userdata) {
                    (*static_cast<CommandFn *>(userdata))(argc, argv);
                },
                slot.get());
            if (rc == 0) {
                _commandSlots.push_back(std::move(slot));
                return true;
            }
            return false;
        }

        bool RegisterHttpEndpoint(const std::string &path, HttpFn callback) {
            auto slot       = std::make_unique<HttpFn>(std::move(callback));
            auto binding    = std::make_unique<HostBindings>();
            binding->vtable = _vtable;
            binding->fn     = slot.get();
            int rc          = _vtable->register_http_endpoint(
                _host, path.c_str(),
                [](const char *method, const char *path_c, const char *body, size_t body_len, FwHttpResponse *response, void *userdata) {
                    auto        *b = reinterpret_cast<HostBindings *>(userdata);
                    HttpResponse wrappedResponse(b->vtable, response);
                    (*b->fn)(method, path_c, std::string_view(body, body_len), wrappedResponse);
                },
                binding.get());
            if (rc == 0) {
                _httpSlots.push_back(std::move(slot));
                _httpBindings.push_back(std::move(binding));
                return true;
            }
            /* slot + binding destroyed on this return; the host did not
             * store the userdata pointer on failure. */
            return false;
        }

        bool OnPlayerConnect(PlayerEventFn callback) {
            return RegisterPlayerEvent(_vtable->on_player_connect, std::move(callback));
        }
        bool OnPlayerDisconnect(PlayerEventFn callback) {
            return RegisterPlayerEvent(_vtable->on_player_disconnect, std::move(callback));
        }

      private:
        /*
         * The HTTP trampoline needs both the std::function* and the vtable* in
         * its userdata. We bundle them in a tiny owned struct.
         */
        struct HostBindings {
            const FwHostVTable *vtable;
            HttpFn             *fn;
        };
        std::vector<std::unique_ptr<HostBindings>> _httpBindings;

        bool RegisterPlayerEvent(int (*registerFn)(FwHost *, FwPlayerEventCallback, void *), PlayerEventFn callback) {
            auto slot = std::make_unique<PlayerEventFn>(std::move(callback));
            auto binding    = std::make_unique<PlayerEventBinding>();
            binding->vtable = _vtable;
            binding->fn     = slot.get();
            int rc = registerFn(
                _host,
                [](FwPlayer *player, uint64_t guid, void *userdata) {
                    auto  *b = static_cast<PlayerEventBinding *>(userdata);
                    Player wrappedPlayer(b->vtable, player, guid);
                    (*b->fn)(wrappedPlayer);
                },
                binding.get());
            if (rc == 0) {
                _playerEventSlots.push_back(std::move(slot));
                _playerEventBindings.push_back(std::move(binding));
                return true;
            }
            return false;
        }

        struct PlayerEventBinding {
            const FwHostVTable *vtable;
            PlayerEventFn      *fn;
        };

        FwHost                                          *_host;
        const FwHostVTable                              *_vtable;
        std::vector<std::unique_ptr<CommandFn>>          _commandSlots;
        std::vector<std::unique_ptr<HttpFn>>             _httpSlots;
        std::vector<std::unique_ptr<PlayerEventFn>>      _playerEventSlots;
        std::vector<std::unique_ptr<PlayerEventBinding>> _playerEventBindings;
    };

    /* --------------------------------------------------------------------- */
    /* Plugin base class                                                      */
    /* --------------------------------------------------------------------- */

    class Base {
      public:
        virtual ~Base() = default;

        /* Return 0 on success, nonzero to abort plugin load. */
        virtual int OnInit(Host &host) = 0;

        virtual void OnPostScriptInit(Host & /*host*/) {}
        virtual void OnUpdate(Host & /*host*/, double /*dt_seconds*/) {}
        virtual void OnPreShutdown(Host & /*host*/) {}
        virtual void OnShutdown(Host & /*host*/) {}
    };

} // namespace Framework::Plugin

/* --------------------------------------------------------------------------- */
/* Required-export glue macro                                                   */
/* --------------------------------------------------------------------------- */

/*
 * Expand once per plugin, at file scope outside any namespace. Generates the
 * required C exports and wires them to a singleton instance of PluginClass.
 *
 *   FW_PLUGIN_DECLARE(MyPlugin, "my-plugin", "1.2.3")
 */
#define FW_PLUGIN_DECLARE(PluginClass, PluginName, PluginVersion)                                                                                                                                                                                                                              \
    namespace {                                                                                                                                                                                                                                                                                \
        PluginClass                       g_fw_plugin_instance;                                                                                                                                                                                                                                \
        std::unique_ptr<Framework::Plugin::Host> g_fw_plugin_host;                                                                                                                                                                                                                             \
        const FwPluginInfo                g_fw_plugin_info_value = {PluginName, PluginVersion, FW_PLUGIN_ABI_VERSION};                                                                                                                                                                         \
    }                                                                                                                                                                                                                                                                                          \
    extern "C" FW_PLUGIN_EXPORT const FwPluginInfo *fw_plugin_info(void) {                                                                                                                                                                                                                     \
        return &g_fw_plugin_info_value;                                                                                                                                                                                                                                                        \
    }                                                                                                                                                                                                                                                                                          \
    extern "C" FW_PLUGIN_EXPORT int fw_plugin_init(FwHost *host) {                                                                                                                                                                                                                             \
        g_fw_plugin_host = std::make_unique<Framework::Plugin::Host>(host);                                                                                                                                                                                                                    \
        if (!g_fw_plugin_host->Valid()) return 1;                                                                                                                                                                                                                                              \
        return g_fw_plugin_instance.OnInit(*g_fw_plugin_host);                                                                                                                                                                                                                                 \
    }                                                                                                                                                                                                                                                                                          \
    extern "C" FW_PLUGIN_EXPORT void fw_plugin_post_script_init(FwHost * /*host*/) {                                                                                                                                                                                                           \
        if (g_fw_plugin_host) g_fw_plugin_instance.OnPostScriptInit(*g_fw_plugin_host);                                                                                                                                                                                                        \
    }                                                                                                                                                                                                                                                                                          \
    extern "C" FW_PLUGIN_EXPORT void fw_plugin_update(FwHost * /*host*/, double dt) {                                                                                                                                                                                                          \
        if (g_fw_plugin_host) g_fw_plugin_instance.OnUpdate(*g_fw_plugin_host, dt);                                                                                                                                                                                                            \
    }                                                                                                                                                                                                                                                                                          \
    extern "C" FW_PLUGIN_EXPORT void fw_plugin_pre_shutdown(FwHost * /*host*/) {                                                                                                                                                                                                               \
        if (g_fw_plugin_host) g_fw_plugin_instance.OnPreShutdown(*g_fw_plugin_host);                                                                                                                                                                                                           \
    }                                                                                                                                                                                                                                                                                          \
    extern "C" FW_PLUGIN_EXPORT void fw_plugin_shutdown(FwHost * /*host*/) {                                                                                                                                                                                                                   \
        if (g_fw_plugin_host) {                                                                                                                                                                                                                                                                \
            g_fw_plugin_instance.OnShutdown(*g_fw_plugin_host);                                                                                                                                                                                                                                \
            g_fw_plugin_host.reset();                                                                                                                                                                                                                                                          \
        }                                                                                                                                                                                                                                                                                      \
    }
