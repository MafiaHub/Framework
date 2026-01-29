/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/node_engine.h"
#include "scripting/builtins/builtins.h"
#include "scripting/builtins/console.h"
#include "scripting/builtins/events.h"
#include "scripting/resource/resource_manager.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include <fstream>

// Helper to execute JS and get integer result
static int32_t RunJS(Framework::Scripting::NodeEngine &engine, const char *code) {
    v8::Isolate *isolate = engine.GetIsolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine.GetContext();
    v8::Context::Scope contextScope(context);

    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
    v8::MaybeLocal<v8::Script> maybeScript = v8::Script::Compile(context, source);

    if (maybeScript.IsEmpty() || tryCatch.HasCaught()) {
        return -999999;
    }

    v8::MaybeLocal<v8::Value> maybeResult = maybeScript.ToLocalChecked()->Run(context);

    if (tryCatch.HasCaught() || maybeResult.IsEmpty()) {
        return -999998;
    }

    v8::Local<v8::Value> result = maybeResult.ToLocalChecked();
    if (!result->IsNumber()) {
        return -999997;
    }

    return result->Int32Value(context).FromJust();
}

// Helper to execute JS and get boolean result
static bool RunJSBool(Framework::Scripting::NodeEngine &engine, const char *code) {
    v8::Isolate *isolate = engine.GetIsolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine.GetContext();
    v8::Context::Scope contextScope(context);

    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
    v8::MaybeLocal<v8::Script> maybeScript = v8::Script::Compile(context, source);

    if (maybeScript.IsEmpty() || tryCatch.HasCaught()) {
        return false;
    }

    v8::MaybeLocal<v8::Value> maybeResult = maybeScript.ToLocalChecked()->Run(context);

    if (tryCatch.HasCaught() || maybeResult.IsEmpty()) {
        return false;
    }

    return maybeResult.ToLocalChecked()->BooleanValue(isolate);
}

// Helper to check if JS throws
static bool RunJSThrows(Framework::Scripting::NodeEngine &engine, const char *code) {
    v8::Isolate *isolate = engine.GetIsolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine.GetContext();
    v8::Context::Scope contextScope(context);

    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
    v8::MaybeLocal<v8::Script> maybeScript = v8::Script::Compile(context, source);

    if (maybeScript.IsEmpty()) {
        return true;
    }

    maybeScript.ToLocalChecked()->Run(context);
    return tryCatch.HasCaught();
}

// Test helper for Events tests
class EventsTestHelper {
  public:
    static std::string GetTestPath() {
#ifdef _WIN32
        const char *temp = std::getenv("TEMP");
        if (!temp) temp = std::getenv("TMP");
        if (!temp) temp = "C:\\Temp";
        return std::string(temp) + "\\framework_events_test";
#else
        return "/tmp/framework_events_test";
#endif
    }

    static void Setup() {
        cppfs::FileHandle dir = cppfs::fs::open(GetTestPath());
        if (!dir.exists()) {
            dir.createDirectory();
        }
    }

    static void Cleanup() {
        cppfs::FileHandle dir = cppfs::fs::open(GetTestPath());
        if (dir.exists()) {
            dir.removeDirectoryRec();
        }
    }
};

MODULE(js_features, {
    using namespace Framework::Scripting;

    // ========================================
    // BUILTIN TYPES TESTS (single engine to avoid v8pp caching bug)
    // ========================================

    IT("All builtin types work correctly", {
        NodeEngine engine;
        EQUALS(engine.Init(), true);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);
            Builtins::RegisterAll(isolate, context->Global());
        }

        // Vector3 tests
        EQUALS(RunJSBool(engine, "typeof Vector3 === 'function'"), true);
        EQUALS(RunJS(engine, "new Vector3(1, 2, 3).x"), 1);
        EQUALS(RunJS(engine, "new Vector3(1, 2, 3).y"), 2);
        EQUALS(RunJS(engine, "new Vector3(1, 2, 3).z"), 3);
        EQUALS(RunJS(engine, "const v = new Vector3(0,0,0); v.x = 5; v.x"), 5);
        EQUALS(RunJS(engine, "const a = new Vector3(1,2,3); a.add(new Vector3(4,5,6)); a.x"), 5);
        EQUALS(RunJS(engine, "new Vector3(1,0,0).dot(new Vector3(0,1,0))"), 0);
        EQUALS(RunJSBool(engine, "Math.abs(new Vector3(3,4,0).length - 5) < 0.001"), true);
        EQUALS(RunJS(engine, "Vector3.zero().x + Vector3.zero().y + Vector3.zero().z"), 0);
        EQUALS(RunJS(engine, "Vector3.one().x + Vector3.one().y + Vector3.one().z"), 3);

        // Vector2 tests
        EQUALS(RunJSBool(engine, "typeof Vector2 === 'function'"), true);
        EQUALS(RunJS(engine, "new Vector2(10, 20).x"), 10);
        EQUALS(RunJS(engine, "new Vector2(10, 20).y"), 20);

        // Vector4 tests
        EQUALS(RunJSBool(engine, "typeof Vector4 === 'function'"), true);
        EQUALS(RunJS(engine, "new Vector4(1,2,3,4).w"), 4);

        // Quaternion tests
        EQUALS(RunJSBool(engine, "typeof Quaternion === 'function'"), true);
        EQUALS(RunJSBool(engine, "Quaternion.identity().w === 1"), true);

        // Color tests
        EQUALS(RunJSBool(engine, "typeof Color === 'function'"), true);
        EQUALS(RunJS(engine, "new Color(255, 128, 64, 255).r"), 255);
        EQUALS(RunJS(engine, "new Color(255, 128, 64, 255).g"), 128);

        engine.Shutdown();
    });

    // ========================================
    // EVENTS SYSTEM TESTS
    // ========================================

    IT("Events.on registers handler and CleanupResource removes it", {
        EventsTestHelper::Setup();

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            manager.GetEvents().Register(isolate, context, context->Global(), &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        EQUALS(RunJSBool(engine, R"(
            const unsub = Events.on('testEvent', () => {});
            typeof unsub === 'function'
        )"), true);

        EQUALS(manager.GetEvents().GetListenerCount("testEvent"), (size_t)1);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            manager.GetEvents().CleanupResource("testResource");
        }
        EQUALS(manager.GetEvents().GetListenerCount("testEvent"), (size_t)0);

        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    IT("Events.emit blocks reserved events from JS", {
        EventsTestHelper::Setup();

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            manager.GetEvents().Register(isolate, context, context->Global(), &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        EQUALS(RunJSThrows(engine, "Events.emit('resourceStart')"), true);
        EQUALS(RunJSThrows(engine, "Events.emit('resourceStop')"), true);
        EQUALS(RunJSThrows(engine, "Events.emit('playerConnect')"), true);
        EQUALS(RunJSThrows(engine, "Events.emit('customEvent')"), false);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            manager.GetEvents().CleanupResource("testResource");
        }
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    IT("Events.listenerCount returns correct count", {
        EventsTestHelper::Setup();

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            manager.GetEvents().Register(isolate, context, context->Global(), &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        EQUALS(RunJS(engine, "Events.listenerCount('countTest')"), 0);
        RunJS(engine, "Events.on('countTest', () => {}); 0");
        EQUALS(RunJS(engine, "Events.listenerCount('countTest')"), 1);
        RunJS(engine, "Events.on('countTest', () => {}); 0");
        EQUALS(RunJS(engine, "Events.listenerCount('countTest')"), 2);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            manager.GetEvents().CleanupResource("testResource");
        }
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    IT("Events unsubscribe function removes handler", {
        EventsTestHelper::Setup();

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            manager.GetEvents().Register(isolate, context, context->Global(), &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        RunJS(engine, "globalThis.unsub = Events.on('unsubTest', () => {}); 0");
        EQUALS(manager.GetEvents().GetListenerCount("unsubTest"), (size_t)1);

        RunJS(engine, "globalThis.unsub(); 0");
        EQUALS(manager.GetEvents().GetListenerCount("unsubTest"), (size_t)0);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            manager.GetEvents().CleanupResource("testResource");
        }
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    IT("Events.on throws without resource context", {
        EventsTestHelper::Setup();

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            manager.GetEvents().Register(isolate, context, context->Global(), &manager);
            // NOT setting resource context
        }

        EQUALS(RunJSThrows(engine, "Events.on('test', () => {})"), true);

        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    IT("Events.on throws with invalid arguments", {
        EventsTestHelper::Setup();

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            manager.GetEvents().Register(isolate, context, context->Global(), &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        EQUALS(RunJSThrows(engine, "Events.on()"), true);
        EQUALS(RunJSThrows(engine, "Events.on('test')"), true);
        EQUALS(RunJSThrows(engine, "Events.on(123, () => {})"), true);
        EQUALS(RunJSThrows(engine, "Events.on('test', 'notafunction')"), true);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            manager.GetEvents().CleanupResource("testResource");
        }
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // ========================================
    // CONSOLE BUILTIN TESTS
    // ========================================

    IT("console methods are callable without crash", {
        EventsTestHelper::Setup();

        NodeEngine engine;
        EQUALS(engine.Init(), true);

        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            Console::Register(isolate, context, &manager);
        }

        EQUALS(RunJSBool(engine, "typeof console.log === 'function'"), true);
        EQUALS(RunJSBool(engine, "typeof console.warn === 'function'"), true);
        EQUALS(RunJSBool(engine, "typeof console.error === 'function'"), true);

        RunJS(engine, "console.log('test'); 0");
        RunJS(engine, "console.warn('warn'); 0");
        RunJS(engine, "console.error('error'); 0");
        RunJS(engine, "console.log('a', 'b', 1, 2, true, null, {x:1}); 0");

        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

});
