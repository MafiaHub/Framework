/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/node_engine.h"
#include "scripting/builtins/events.h"
#include "scripting/resource/resource_manager.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <chrono>
#include <fstream>
#include <thread>

class TimerContextTestHelper {
  public:
    static std::string GetTestPath() {
#ifdef _WIN32
        const char *temp = std::getenv("TEMP");
        if (!temp) temp = std::getenv("TMP");
        if (!temp) temp = "C:\\Temp";
        return std::string(temp) + "\\framework_timer_ctx_test";
#else
        return "/tmp/framework_timer_ctx_test";
#endif
    }

    static void Setup() {
        Cleanup();
        cppfs::FileHandle dir = cppfs::fs::open(GetTestPath());
        if (!dir.exists()) {
            dir.createDirectory();
        }
    }

    static void CreateResource(const std::string &name) {
        std::string resourcePath = GetTestPath() + "/" + name;
        cppfs::FileHandle dir = cppfs::fs::open(resourcePath);
        if (!dir.exists()) {
            dir.createDirectory();
        }
        std::ofstream pkg(resourcePath + "/package.json");
        pkg << R"({"name":")" << name << R"(","version":"1.0.0"})";
        pkg.close();
    }

    static void Cleanup() {
        cppfs::FileHandle dir = cppfs::fs::open(GetTestPath());
        if (dir.exists()) {
            dir.removeDirectoryRec();
        }
    }
};

// Compile and run JS with a ScriptOrigin inside a resource directory.
// Functions defined in the code carry this origin, so GetResourceNameFromFunction works.
static int32_t TimerRunJS(Framework::Scripting::NodeEngine &engine,
                          const char *code,
                          const std::string &resourceName) {
    v8::Isolate *isolate = engine.GetIsolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine.GetContext();
    v8::Context::Scope contextScope(context);

    std::string fakePath = TimerContextTestHelper::GetTestPath() + "/" + resourceName + "/test.js";
    v8::ScriptOrigin origin(v8::String::NewFromUtf8(isolate, fakePath.c_str()).ToLocalChecked());

    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
    v8::MaybeLocal<v8::Script> maybeScript = v8::Script::Compile(context, source, &origin);
    if (maybeScript.IsEmpty() || tryCatch.HasCaught()) return -999999;
    v8::MaybeLocal<v8::Value> maybeResult = maybeScript.ToLocalChecked()->Run(context);
    if (tryCatch.HasCaught() || maybeResult.IsEmpty()) return -999998;
    v8::Local<v8::Value> result = maybeResult.ToLocalChecked();
    if (!result->IsNumber()) return -999997;
    return result->Int32Value(context).FromJust();
}

static bool TimerRunJSBool(Framework::Scripting::NodeEngine &engine, const char *code) {
    v8::Isolate *isolate = engine.GetIsolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine.GetContext();
    v8::Context::Scope contextScope(context);

    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
    v8::MaybeLocal<v8::Script> maybeScript = v8::Script::Compile(context, source);
    if (maybeScript.IsEmpty() || tryCatch.HasCaught()) return false;
    v8::MaybeLocal<v8::Value> maybeResult = maybeScript.ToLocalChecked()->Run(context);
    if (tryCatch.HasCaught() || maybeResult.IsEmpty()) return false;
    return maybeResult.ToLocalChecked()->BooleanValue(isolate);
}

static void TickUntil(Framework::Scripting::NodeEngine &engine,
                      const char *conditionCode,
                      int maxMs = 200) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        engine.Tick();
        if (TimerRunJSBool(engine, conditionCode)) break;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= maxMs) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

MODULE(timer_context, {
    using namespace Framework::Scripting;

    IT("Events.on works inside setTimeout via handler function origin", {
        TimerContextTestHelper::Setup();
        TimerContextTestHelper::CreateResource("timerRes");

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        flecs::world world;
        ResourceManagerConfig config;
        config.resourcesPath = TimerContextTestHelper::GetTestPath();
        ResourceManager manager(&engine, &world, config);
        manager.DiscoverResources();

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context,
                v8::String::NewFromUtf8Literal(isolate, "Core"),
                coreObj).Check();
            manager.GetEvents().Register(isolate, context, coreObj, &manager);
        }

        // No ambient context set — function origin is the only signal
        TimerRunJS(engine, R"(
            globalThis.__timerFired = false;
            setTimeout(() => {
                Core.Events.on('timerEvent', () => {});
                globalThis.__timerFired = true;
            }, 1);
            0
        )", "timerRes");

        TickUntil(engine, "globalThis.__timerFired === true");

        EQUALS(TimerRunJSBool(engine, "globalThis.__timerFired"), true);
        EQUALS(manager.GetEvents().GetListenerCount("timerEvent"), (size_t)1);
        // Ambient context was never set, stays clean
        STREQUALS(manager.GetCurrentResourceContext().c_str(), "");

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            manager.GetEvents().CleanupResource("timerRes");
        }
        engine.Shutdown();
        TimerContextTestHelper::Cleanup();
    });

    IT("setTimeout without resource context does not crash", {
        TimerContextTestHelper::Setup();

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        flecs::world world;
        ResourceManagerConfig config;
        config.resourcesPath = TimerContextTestHelper::GetTestPath();
        ResourceManager manager(&engine, &world, config);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, R"(
                globalThis.__noCtxDone = false;
                setTimeout(() => { globalThis.__noCtxDone = true; }, 1);
                0
            )").ToLocalChecked();
            v8::Script::Compile(context, source).ToLocalChecked()->Run(context);
        }

        TickUntil(engine, "globalThis.__noCtxDone === true");
        EQUALS(TimerRunJSBool(engine, "globalThis.__noCtxDone"), true);

        engine.Shutdown();
        TimerContextTestHelper::Cleanup();
    });

    IT("handlers from different resources are attributed correctly", {
        TimerContextTestHelper::Setup();
        TimerContextTestHelper::CreateResource("resourceA");
        TimerContextTestHelper::CreateResource("resourceB");

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        flecs::world world;
        ResourceManagerConfig config;
        config.resourcesPath = TimerContextTestHelper::GetTestPath();
        ResourceManager manager(&engine, &world, config);
        manager.DiscoverResources();

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context,
                v8::String::NewFromUtf8Literal(isolate, "Core"),
                coreObj).Check();
            manager.GetEvents().Register(isolate, context, coreObj, &manager);
        }

        TimerRunJS(engine, R"(
            globalThis.__doneA = false;
            setTimeout(() => {
                Core.Events.on('eventA', () => {});
                globalThis.__doneA = true;
            }, 1);
            0
        )", "resourceA");

        TimerRunJS(engine, R"(
            globalThis.__doneB = false;
            setTimeout(() => {
                Core.Events.on('eventB', () => {});
                globalThis.__doneB = true;
            }, 1);
            0
        )", "resourceB");

        TickUntil(engine, "globalThis.__doneA && globalThis.__doneB");

        EQUALS(manager.GetEvents().GetListenerCount("eventA"), (size_t)1);
        EQUALS(manager.GetEvents().GetListenerCount("eventB"), (size_t)1);

        // Cleaning up A should not affect B
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            manager.GetEvents().CleanupResource("resourceA");
        }
        EQUALS(manager.GetEvents().GetListenerCount("eventA"), (size_t)0);
        EQUALS(manager.GetEvents().GetListenerCount("eventB"), (size_t)1);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            manager.GetEvents().CleanupResource("resourceB");
        }
        EQUALS(manager.GetEvents().GetListenerCount("eventB"), (size_t)0);

        engine.Shutdown();
        TimerContextTestHelper::Cleanup();
    });

    IT("timer context final cleanup", {
        TimerContextTestHelper::Cleanup();
    });
})
