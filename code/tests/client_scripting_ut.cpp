/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "logging/logger.h"
#include "scripting/builtins/execution_environment.h"
#include "scripting/scripting_catalog.h"
#include "scripting/resource/resource_manager.h"
#include "scripting/v8_engine.h"
#include "unit.h"
#include "utils/package/package.h"
#include "utils/vfs.h"
#include "utils/version.h"

#include <v8pp/convert.hpp>

#include <chrono>
#include <thread>

MODULE(client_scripting, {
    IT("client environment preserves local release versions across context resets", {
        Framework::Scripting::V8Engine engine;
        EQUALS(engine.Init(), Framework::Scripting::ScriptingError::SCRIPTING_NONE);
        for (int registration = 0; registration < 2; ++registration) {
            {
                v8::Isolate *isolate = engine.GetIsolate();
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                v8::HandleScope handleScope(isolate);
                auto context = engine.GetContext();
                v8::Context::Scope contextScope(context);
                Framework::Scripting::SetScriptingCatalog(isolate, "framework-client");
                Framework::Scripting::Builtins::ExecutionEnvironment::Register(isolate, context, context->Global(), true, "2.4.0-rc.2+client");
                auto environment = context->Global()->Get(context, v8pp::to_v8(isolate, "ExecutionEnvironment")).ToLocalChecked().As<v8::Object>();
                auto version = environment->Get(context, v8pp::to_v8(isolate, "frameworkVersion")).ToLocalChecked();
                EQUALS(version->IsString(), true);
                STREQUALS(v8pp::from_v8<std::string>(isolate, version).c_str(), Framework::Utils::Version::rel);
            }
            EQUALS(engine.Execute(R"JS(
                if (!ExecutionEnvironment.isClient || ExecutionEnvironment.isServer) throw Error('wrong side');
                if (ExecutionEnvironment.modVersion !== '2.4.0-rc.2+client') throw Error('wrong mod version');
                for (const name of ['frameworkVersion', 'modVersion']) {
                    const original = ExecutionEnvironment[name];
                    if (Reflect.set(ExecutionEnvironment, name, 'changed')) throw Error('writable version');
                    if (Reflect.deleteProperty(ExecutionEnvironment, name)) throw Error('configurable version');
                    if (ExecutionEnvironment[name] !== original) throw Error('changed version');
                }
            )JS"), true);
            if (registration == 0) {
                EQUALS(engine.ResetContext(), true);
            }
        }
        Framework::Scripting::ClearScriptingCatalog(engine.GetIsolate());
        engine.Shutdown();
    });

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
});

int main() {
    UNIT_CREATE("FrameworkClientScriptingTests");
    Framework::Logging::GetInstance()->PauseLogging(true);
    UNIT_MODULE(client_scripting);
    return UNIT_RUN();
}
