/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "logging/logger.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/v8_engine.h"
#include "unit.h"
#include "utils/package/package.h"
#include "utils/vfs.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#include "modules/keybinds_ut.h"
#endif

MODULE(client_scripting, {
    IT("restarts packaged client scripts and timers without restarting other resources", {
        auto &vfs = Framework::Utils::Vfs::Get();
        EQUALS(vfs.Init(nullptr), true);

        const auto mount = [&](const std::string &name, int version) {
            Framework::Utils::Package::Writer writer;
            writer.Add("package.json", "{\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"mafiahub\":{\"clientScripts\":[\"client.js\"]}}");
            writer.Add("client.js", "const state = globalThis['" + name + "'] ||= {loads: 0, ticks: 0}; ++state.loads; state.version = require('./helper'); setInterval(() => ++state.ticks, 1);");
            writer.Add("helper.js", "module.exports = " + std::to_string(version) + ";");
            std::string blob, zip, error;
            if (!writer.Build(nullptr, blob) || !Framework::Utils::Package::Open(blob, nullptr, zip, error)) {
                return false;
            }
            vfs.Unmount(name + ".zip");
            return vfs.MountMemory(std::move(zip), name + ".zip", Framework::Utils::Vfs::ResourcePath(name), error);
        };

        EQUALS(mount("restart", 1), true);
        // A shared prefix also checks that eviction respects directory boundaries.
        EQUALS(mount("restart-other", 1), true);

        Framework::Scripting::V8Engine engine;
        engine.SetModuleRootPath(Framework::Utils::Vfs::kResourceMountRoot);
        EQUALS(engine.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);
        const auto tick = [&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            engine.Tick();
        };
        {
            Framework::Scripting::ResourceManagerConfig config;
            config.resourcesPath = Framework::Utils::Vfs::kResourceMountRoot;
            config.isClient      = true;
            Framework::Scripting::ResourceManager manager(&engine, config);
            EQUALS(manager.DiscoverResources(), 2u);
            EQUALS(static_cast<bool>(manager.StartAll()), true);
            tick();
            EQUALS(engine.Execute("if (restart.loads !== 1 || restart.ticks !== 1 || restart.version !== 1) throw Error('initial start');"), true);

            // Starting an already-running resource must leave it alone.
            EQUALS(static_cast<bool>(manager.StartResource("restart")), true);
            EQUALS(engine.Execute("if (restart.loads !== 1) throw Error('duplicate start');"), true);

            // Mirror ResourceStop followed by package remount and ResourceRefresh:
            // the stopped client takes StartResource, not RefreshResource.
            for (int version = 2; version <= 3; ++version) {
                EQUALS(static_cast<bool>(manager.StopResource("restart")), true);
                EQUALS(engine.Execute("globalThis.stoppedTicks = restart.ticks; globalThis.otherTicks = globalThis['restart-other'].ticks;"), true);
                tick();
                EQUALS(engine.Execute("if (restart.ticks !== stoppedTicks || globalThis['restart-other'].ticks !== otherTicks + 1) throw Error('stop isolation');"), true);
                EQUALS(mount("restart", version), true);
                EQUALS(static_cast<bool>(manager.StartResource("restart")), true);
                tick();
                EQUALS(engine.Execute("if (restart.loads !== " + std::to_string(version) + " || restart.version !== " + std::to_string(version) + " || restart.ticks !== stoppedTicks + 1) throw Error('restart did not execute fresh modules and one timer');"), true);
                EQUALS(engine.Execute("if (globalThis['restart-other'].loads !== 1 || globalThis['restart-other'].ticks !== otherTicks + 2) throw Error('restart isolation');"), true);
                EQUALS(engine.ExecuteFile("/resources/restart-other/client.js"), true);
                EQUALS(engine.Execute("if (globalThis['restart-other'].loads !== 1) throw Error('unrelated module evicted');"), true);
            }
            manager.StopAll();
        }
        engine.Shutdown();
        vfs.Unmount("restart.zip");
        vfs.Unmount("restart-other.zip");
    });
    IT("routes thrown client timers and unhandled promises through resource cleanup", {
        auto &vfs = Framework::Utils::Vfs::Get();
        EQUALS(vfs.Init(nullptr), true);
        const auto mount = [&](const std::string &name, const std::string &script) {
            Framework::Utils::Package::Writer writer;
            writer.Add("package.json", "{\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"mafiahub\":{\"clientScripts\":[\"client.js\"]}}");
            writer.Add("client.js", script);
            std::string blob, zip, error;
            if (!writer.Build(nullptr, blob) || !Framework::Utils::Package::Open(blob, nullptr, zip, error)) return false;
            return vfs.MountMemory(std::move(zip), name + ".zip", Framework::Utils::Vfs::ResourcePath(name), error);
        };
        EQUALS(mount("timer-failure", "globalThis.failedTicks = 0; setInterval(() => { ++failedTicks; throw Error('timer boom'); }, 1);"), true);
        EQUALS(mount("promise-failure", "setTimeout(async () => { await Promise.resolve(); throw Error('promise boom'); }, 0);"), true);
        EQUALS(mount("healthy", "globalThis.healthyTicks = 0; setInterval(() => ++healthyTicks, 1); const p = Promise.reject(Error('handled')); queueMicrotask(() => p.catch(() => {}));"), true);
        Framework::Scripting::V8Engine engine;
        engine.SetModuleRootPath(Framework::Utils::Vfs::kResourceMountRoot);
        EQUALS(engine.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);
        {
            Framework::Scripting::ResourceManagerConfig config;
            config.resourcesPath = Framework::Utils::Vfs::kResourceMountRoot;
            config.isClient = true;
            Framework::Scripting::ResourceManager manager(&engine, config);
            std::vector<std::string> cleaned;
            // This is the same manager notification used by native resource owners.
            manager.AddOnResourceError([&](const std::string &name, const std::string &) {
                cleaned.push_back(name);
                engine.ClearResourceTimers(name);
            });
            EQUALS(manager.DiscoverResources(), 3u);
            EQUALS(static_cast<bool>(manager.StartAll()), true);
            for (int i = 0; i < 4; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                engine.Tick();
            }
            EQUALS(cleaned.size(), 2u);
            EQUALS(std::count(cleaned.begin(), cleaned.end(), "timer-failure"), 1);
            EQUALS(std::count(cleaned.begin(), cleaned.end(), "promise-failure"), 1);
            EQUALS(engine.Execute("if (failedTicks !== 1 || healthyTicks < 2) throw Error('cleanup or isolation failed');"), true);
            // Pending and not-yet-rejected promises from the old incarnation
            // must not tear down a resource started under the same name.
            EQUALS(engine.Execute("globalThis.oldReject = null; new Promise((resolve, reject) => { oldReject = reject; }); Promise.reject(Error('queued old error'));", "/resources/healthy/client.js"), true);
            EQUALS(static_cast<bool>(manager.StopResource("healthy")), true);
            EQUALS(static_cast<bool>(manager.StartResource("healthy")), true);
            EQUALS(engine.Execute("oldReject(Error('late old error'));"), true);
            engine.Tick();
            EQUALS(cleaned.size(), 2u);
            EQUALS(engine.Execute("globalThis.currentTicks = healthyTicks;"), true);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            engine.Tick();
            EQUALS(engine.Execute("if (healthyTicks <= currentTicks) throw Error('stale rejection stopped new timer');"), true);
            manager.StopAll();
        }
        engine.Shutdown();
        vfs.Unmount("timer-failure.zip");
        vfs.Unmount("promise-failure.zip");
        vfs.Unmount("healthy.zip");
    });

});

int main() {
    UNIT_CREATE("FrameworkClientScriptingTests");
    Framework::Logging::GetInstance()->PauseLogging(true);
    UNIT_MODULE(client_scripting);
#ifdef _WIN32
    UNIT_MODULE(keybinds);
#endif
    return UNIT_RUN();
}
