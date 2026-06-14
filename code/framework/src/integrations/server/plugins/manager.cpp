/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "manager.h"

#include "logging/logger.h"
#include "utils/command_processor.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace Framework::Integrations::Server::Plugins {

    static constexpr const char *kLoggerName = "plugins";

    PluginManager::PluginManager() = default;

    PluginManager::~PluginManager() {
        if (!_shutdownCalled) Shutdown();
    }

    void PluginManager::Init(Framework::HTTP::Webserver *webserver, Framework::Utils::CommandProcessor *commandProcessor, Framework::World::ServerEngine *worldEngine) {
        _webserver        = webserver;
        _commandProcessor = commandProcessor;
        _worldEngine      = worldEngine;
    }

    size_t PluginManager::LoadAll(const std::string &modulesDir, const std::vector<std::string> &pluginNames) {
        auto log = Framework::Logging::GetLogger(kLoggerName);
        if (pluginNames.empty()) {
            log->debug("No plugins listed in server config; skipping load");
            return 0;
        }
        log->info("Loading {} plugin(s) from '{}'", pluginNames.size(), modulesDir);
        size_t loaded = 0;
        for (const auto &name : pluginNames) {
            if (LoadOne(modulesDir, name)) ++loaded;
        }
        log->info("Plugin load complete: {}/{} succeeded", loaded, pluginNames.size());
        return loaded;
    }

    bool PluginManager::LoadOne(const std::string &modulesDir, const std::string &name) {
        auto log = Framework::Logging::GetLogger(kLoggerName);

        const std::filesystem::path baseDir = std::filesystem::path(modulesDir) / name;
        const std::filesystem::path manifestPath = baseDir / (name + ".module.json");

        if (!std::filesystem::exists(manifestPath)) {
            log->error("Plugin '{}' missing manifest at {}", name, manifestPath.string());
            return false;
        }

        nlohmann::json manifest;
        try {
            std::ifstream     in(manifestPath);
            std::stringstream buf;
            buf << in.rdbuf();
            manifest = nlohmann::json::parse(buf.str());
        }
        catch (const std::exception &e) {
            log->error("Plugin '{}' manifest parse error: {}", name, e.what());
            return false;
        }

        const std::string declaredName = manifest.value("name", "");
        if (declaredName != name) {
            log->error("Plugin '{}' manifest name mismatch (got '{}')", name, declaredName);
            return false;
        }

        const uint32_t manifestAbi = manifest.value("abi_version", 0u);
        if (manifestAbi != FW_PLUGIN_ABI_VERSION) {
            log->error("Plugin '{}' manifest abi_version {} != host {}", name, manifestAbi, FW_PLUGIN_ABI_VERSION);
            return false;
        }

        const std::string entry = manifest.value("entry", name);
        const auto        libPath = baseDir / SharedLibrary::PlatformFilename(entry);

        auto plugin     = std::make_unique<LoadedPlugin>();
        plugin->name    = name;
        plugin->version = manifest.value("version", "");

        if (!plugin->library.Open(libPath.string())) {
            log->error("Plugin '{}' dlopen failed for {}: {}", name, libPath.string(), plugin->library.GetLastError());
            return false;
        }

        plugin->infoFn           = reinterpret_cast<const FwPluginInfo *(*)()>(plugin->library.Symbol("fw_plugin_info"));
        plugin->initFn           = reinterpret_cast<int  (*)(FwHost *)>(plugin->library.Symbol("fw_plugin_init"));
        plugin->shutdownFn       = reinterpret_cast<void (*)(FwHost *)>(plugin->library.Symbol("fw_plugin_shutdown"));
        plugin->postScriptInitFn = reinterpret_cast<void (*)(FwHost *)>(plugin->library.Symbol("fw_plugin_post_script_init"));
        plugin->updateFn         = reinterpret_cast<void (*)(FwHost *, double)>(plugin->library.Symbol("fw_plugin_update"));
        plugin->preShutdownFn    = reinterpret_cast<void (*)(FwHost *)>(plugin->library.Symbol("fw_plugin_pre_shutdown"));

        if (!plugin->infoFn || !plugin->initFn || !plugin->shutdownFn) {
            log->error("Plugin '{}' missing required exports (fw_plugin_info / fw_plugin_init / fw_plugin_shutdown)", name);
            return false;
        }

        const FwPluginInfo *info = nullptr;
        try {
            info = plugin->infoFn();
        }
        catch (...) {
            log->error("Plugin '{}' fw_plugin_info threw", name);
            return false;
        }
        if (!info || info->abi_version != FW_PLUGIN_ABI_VERSION) {
            log->error("Plugin '{}' reported abi_version {} != host {}", name, info ? info->abi_version : 0u, FW_PLUGIN_ABI_VERSION);
            return false;
        }
        if (info->name && declaredName != info->name) {
            log->warn("Plugin '{}' info->name='{}' disagrees with manifest", name, info->name);
        }
        if (info->version && plugin->version != info->version) {
            log->warn("Plugin '{}' info->version='{}' disagrees with manifest '{}'", name, info->version, plugin->version);
        }

        plugin->impl                  = std::make_unique<HostImpl>();
        plugin->impl->pluginName      = name;
        plugin->impl->webserver       = _webserver;
        plugin->impl->commandProcessor = _commandProcessor;
        plugin->impl->worldEngine     = _worldEngine;
        plugin->impl->logger          = Framework::Logging::GetLogger((std::string("plugin:") + name).c_str());
        plugin->host                  = MakeFwHost(plugin->impl.get());

        int initRc = 0;
        try {
            initRc = plugin->initFn(&plugin->host);
        }
        catch (const std::exception &e) {
            log->error("Plugin '{}' fw_plugin_init threw: {}", name, e.what());
            return false;
        }
        catch (...) {
            log->error("Plugin '{}' fw_plugin_init threw non-std exception", name);
            return false;
        }
        if (initRc != 0) {
            log->error("Plugin '{}' fw_plugin_init returned {}", name, initRc);
            return false;
        }

        /* Informational: warn about unsatisfied dependencies. v1 does not
         * reorder loads, but flags missing deps so authors notice. */
        if (manifest.contains("depends_on") && manifest["depends_on"].is_array()) {
            for (const auto &dep : manifest["depends_on"]) {
                const std::string depName = dep.value("name", "");
                bool found = false;
                for (const auto &p : _plugins) {
                    if (p->name == depName) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    log->warn("Plugin '{}' declares dependency on '{}' which is not loaded (yet)", name, depName);
                }
            }
        }

        plugin->initSucceeded = true;
        log->info("Loaded plugin '{}' v{}", name, plugin->version);
        _plugins.push_back(std::move(plugin));
        return true;
    }

    /* ----------------------------------------------------------------------- */
    /* Lifecycle dispatch                                                       */
    /* ----------------------------------------------------------------------- */

    void PluginManager::PostScriptInit() {
        for (auto &p : _plugins) {
            if (!p->initSucceeded || !p->postScriptInitFn) continue;
            try {
                p->postScriptInitFn(&p->host);
            }
            catch (const std::exception &e) {
                Framework::Logging::GetLogger(kLoggerName)->error("Plugin '{}' fw_plugin_post_script_init threw: {}", p->name, e.what());
            }
            catch (...) {
                Framework::Logging::GetLogger(kLoggerName)->error("Plugin '{}' fw_plugin_post_script_init threw non-std exception", p->name);
            }
        }
    }

    void PluginManager::Update(double dtSeconds) {
        for (auto &p : _plugins) {
            if (!p->initSucceeded || !p->updateFn) continue;
            try {
                p->updateFn(&p->host, dtSeconds);
            }
            catch (const std::exception &e) {
                Framework::Logging::GetLogger(kLoggerName)->error("Plugin '{}' fw_plugin_update threw: {}", p->name, e.what());
                p->initSucceeded = false; /* disable to stop the spam */
            }
            catch (...) {
                Framework::Logging::GetLogger(kLoggerName)->error("Plugin '{}' fw_plugin_update threw non-std exception", p->name);
                p->initSucceeded = false;
            }
        }
    }

    void PluginManager::PreShutdown() {
        for (auto &p : _plugins) {
            if (!p->initSucceeded || !p->preShutdownFn) continue;
            try {
                p->preShutdownFn(&p->host);
            }
            catch (const std::exception &e) {
                Framework::Logging::GetLogger(kLoggerName)->error("Plugin '{}' fw_plugin_pre_shutdown threw: {}", p->name, e.what());
            }
            catch (...) {
                Framework::Logging::GetLogger(kLoggerName)->error("Plugin '{}' fw_plugin_pre_shutdown threw non-std exception", p->name);
            }
        }
    }

    void PluginManager::Shutdown() {
        if (_shutdownCalled) return;
        _shutdownCalled = true;

        /* Reverse order: last-loaded plugin shuts down first, so a plugin
         * built on top of another's services can clean up before that
         * service goes away. */
        for (auto it = _plugins.rbegin(); it != _plugins.rend(); ++it) {
            auto &p = *it;
            if (p->initSucceeded && p->shutdownFn) {
                try {
                    p->shutdownFn(&p->host);
                }
                catch (const std::exception &e) {
                    Framework::Logging::GetLogger(kLoggerName)->error("Plugin '{}' fw_plugin_shutdown threw: {}", p->name, e.what());
                }
                catch (...) {
                    Framework::Logging::GetLogger(kLoggerName)->error("Plugin '{}' fw_plugin_shutdown threw non-std exception", p->name);
                }
            }
            /* Tear down host side bindings (commands etc.) before the
             * library is unmapped, so trampoline pointers can't be invoked
             * against freed code. */
            if (p->impl && _commandProcessor) {
                for (const auto &cmd : p->impl->registeredCommands) {
                    _commandProcessor->RemoveCommand(cmd);
                }
            }
        }

        _plugins.clear();
    }

    void PluginManager::DispatchPlayerConnect(uint64_t entityId, uint64_t guid) {
        for (auto &p : _plugins) {
            if (!p->initSucceeded) continue;
            Framework::Integrations::Server::Plugins::DispatchPlayerConnect(p->impl.get(), entityId, guid);
        }
    }

    void PluginManager::DispatchPlayerDisconnect(uint64_t entityId, uint64_t guid) {
        for (auto &p : _plugins) {
            if (!p->initSucceeded) continue;
            Framework::Integrations::Server::Plugins::DispatchPlayerDisconnect(p->impl.get(), entityId, guid);
        }
    }

} // namespace Framework::Integrations::Server::Plugins
