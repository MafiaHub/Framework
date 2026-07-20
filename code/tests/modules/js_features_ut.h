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
#include "scripting/builtins/environment.h"
#include "scripting/builtins/exports.h"
#include "scripting/builtins/imports.h"
#include "scripting/builtins/messages.h"
#include "scripting/builtins/player.h"
#include "scripting/resource/resource.h"
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

// Helper to execute JS and get string result
static std::string RunJSString(Framework::Scripting::NodeEngine &engine, const char *code) {
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
        return "";
    }

    v8::MaybeLocal<v8::Value> maybeResult = maybeScript.ToLocalChecked()->Run(context);

    if (tryCatch.HasCaught() || maybeResult.IsEmpty()) {
        return "";
    }

    v8::Local<v8::Value> result = maybeResult.ToLocalChecked();
    if (!result->IsString()) {
        return "";
    }

    v8::String::Utf8Value str(isolate, result);
    return *str ? *str : "";
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

// Run code and return the constructor name of whatever it throws ("TypeError", "Error", ...),
// or "" when it does not throw. Used to assert the builtins' throw-idiom convention.
static std::string RunJSErrorName(Framework::Scripting::NodeEngine &engine, const std::string &code) {
    std::string wrapped = "(() => { try { " + code + "; return ''; } catch (e) { return (e && e.constructor && e.constructor.name) || 'Error'; } })()";
    return RunJSString(engine, wrapped.c_str());
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
    using namespace Framework::Scripting::Builtins;

    // ========================================
    // BUILTIN TYPES TESTS (single engine to avoid v8pp caching bug)
    // ========================================

    IT("All builtin types work correctly", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            // Create Core object on global (normally done by engine bootstrap)
            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context,
                v8::String::NewFromUtf8Literal(isolate, "Core"),
                coreObj).Check();
            Builtins::RegisterValueTypes(isolate, coreObj);
        }

        // Vector3 tests
        EQUALS(RunJSBool(engine, "typeof Core.Vector3 === 'function'"), true);
        EQUALS(RunJS(engine, "new Core.Vector3(1, 2, 3).x"), 1);
        EQUALS(RunJS(engine, "new Core.Vector3(1, 2, 3).y"), 2);
        EQUALS(RunJS(engine, "new Core.Vector3(1, 2, 3).z"), 3);
        EQUALS(RunJS(engine, "const v = new Core.Vector3(0,0,0); v.x = 5; v.x"), 5);
        EQUALS(RunJS(engine, "const a = new Core.Vector3(1,2,3); a.add(new Core.Vector3(4,5,6)); a.x"), 5);
        EQUALS(RunJS(engine, "new Core.Vector3(1,0,0).dot(new Core.Vector3(0,1,0))"), 0);
        EQUALS(RunJSBool(engine, "Math.abs(new Core.Vector3(3,4,0).length - 5) < 0.001"), true);
        EQUALS(RunJS(engine, "Core.Vector3.zero().x + Core.Vector3.zero().y + Core.Vector3.zero().z"), 0);
        EQUALS(RunJS(engine, "Core.Vector3.one().x + Core.Vector3.one().y + Core.Vector3.one().z"), 3);

        // Vector2 tests
        EQUALS(RunJSBool(engine, "typeof Core.Vector2 === 'function'"), true);
        EQUALS(RunJS(engine, "new Core.Vector2(10, 20).x"), 10);
        EQUALS(RunJS(engine, "new Core.Vector2(10, 20).y"), 20);

        // Vector4 tests
        EQUALS(RunJSBool(engine, "typeof Core.Vector4 === 'function'"), true);
        EQUALS(RunJS(engine, "new Core.Vector4(1,2,3,4).w"), 4);

        // Quaternion tests
        EQUALS(RunJSBool(engine, "typeof Core.Quaternion === 'function'"), true);
        EQUALS(RunJSBool(engine, "Core.Quaternion.identity().w === 1"), true);

        // Color tests
        EQUALS(RunJSBool(engine, "typeof Core.Color === 'function'"), true);
        EQUALS(RunJS(engine, "new Core.Color(255, 128, 64, 255).r"), 255);
        EQUALS(RunJS(engine, "new Core.Color(255, 128, 64, 255).g"), 128);

        engine.Shutdown();
    });

    // Value-type parity: Quaternion gained length/lengthSquared (like Vector) and mul (renamed from
    // multiply — the old name is gone); Color gained NewInstance so native C++ can return a Color.
    IT("Quaternion length/mul parity and Color::NewInstance", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        v8::Isolate *isolate = engine.GetIsolate();
        {
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);
            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "Core"), coreObj).Check();
            RegisterValueTypes(isolate, coreObj);

            // Native C++ hands a Color to JS via NewInstance (the native-SDK direction).
            v8::Local<v8::Object> col = Color::NewInstance(isolate, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
            context->Global()->Set(context, v8pp::to_v8(isolate, "__col"), col).Check();
        }

        // length / lengthSquared, mirroring Vector.
        EQUALS(RunJSBool(engine, "Math.abs(new Core.Quaternion(0,3,4,0).length - 5) < 0.001"), true);
        EQUALS(RunJSBool(engine, "Math.abs(new Core.Quaternion(0,3,4,0).lengthSquared - 25) < 0.001"), true);
        EQUALS(RunJSBool(engine, "Math.abs(Core.Quaternion.identity().length - 1) < 0.001"), true);

        // mul is the (renamed) composition; multiply is gone.
        EQUALS(RunJSBool(engine, "typeof new Core.Quaternion(1,0,0,0).mul === 'function'"), true);
        EQUALS(RunJSBool(engine, "new Core.Quaternion(1,0,0,0).multiply === undefined"), true);
        EQUALS(RunJSBool(engine, "Core.Quaternion.identity().mul(Core.Quaternion.identity()).w === 1"), true);

        // Color::NewInstance produced a real, live Core.Color.
        EQUALS(RunJSBool(engine, "__col instanceof Core.Color"), true);
        EQUALS(RunJSBool(engine, "Math.abs(__col.r - 0.25) < 0.001 && Math.abs(__col.g - 0.5) < 0.001 && Math.abs(__col.b - 0.75) < 0.001"), true);
        EQUALS(RunJSBool(engine, "typeof __col.toHex === 'function'"), true);

        engine.Shutdown();
    });

    // The two packed layouts Chat (RGBA) and TextLabel (ARGB) accept a Color through are computed by
    // Color::toRGBA/toARGB. Their end-to-end use needs networking/replication (integration-tested);
    // here we lock the byte ordering directly. Color(1, 0.5, 0, 1) -> R=255 G=128 B=0 A=255.
    IT("Color packs to RGBA and ARGB with the documented byte order", {
        Color c(1.0f, 0.5f, 0.0f, 1.0f);
        // 0xRRGGBBAA
        UEQUALS(c.toRGBA(), (uint32_t)0xFF80'00FFu);
        // 0xAARRGGBB
        UEQUALS(c.toARGB(), (uint32_t)0xFFFF'8000u);

        // Fully opaque red, distinct in the two layouts.
        Color red(1.0f, 0.0f, 0.0f, 1.0f);
        UEQUALS(red.toRGBA(), (uint32_t)0xFF00'00FFu);
        UEQUALS(red.toARGB(), (uint32_t)0xFFFF'0000u);
    });

    // ========================================
    // EVENTS SYSTEM TESTS
    // ========================================

    IT("Events.on registers handler and CleanupResource removes it", {
        EventsTestHelper::Setup();

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context,
                v8::String::NewFromUtf8Literal(isolate, "Core"),
                coreObj).Check();
            manager.GetEvents().Register(isolate, context, coreObj, &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        EQUALS(RunJSBool(engine, R"(
            const unsub = Core.Events.on('testEvent', () => {});
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

    IT("Events.listenerCount returns correct count", {
        EventsTestHelper::Setup();

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context,
                v8::String::NewFromUtf8Literal(isolate, "Core"),
                coreObj).Check();
            manager.GetEvents().Register(isolate, context, coreObj, &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        EQUALS(RunJS(engine, "Core.Events.listenerCount('countTest')"), 0);
        RunJS(engine, "Core.Events.on('countTest', () => {}); 0");
        EQUALS(RunJS(engine, "Core.Events.listenerCount('countTest')"), 1);
        RunJS(engine, "Core.Events.on('countTest', () => {}); 0");
        EQUALS(RunJS(engine, "Core.Events.listenerCount('countTest')"), 2);

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

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context,
                v8::String::NewFromUtf8Literal(isolate, "Core"),
                coreObj).Check();
            manager.GetEvents().Register(isolate, context, coreObj, &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        RunJS(engine, "globalThis.unsub = Core.Events.on('unsubTest', () => {}); 0");
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

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context,
                v8::String::NewFromUtf8Literal(isolate, "Core"),
                coreObj).Check();
            manager.GetEvents().Register(isolate, context, coreObj, &manager);
            // NOT setting resource context
        }

        EQUALS(RunJSThrows(engine, "Core.Events.on('test', () => {})"), true);

        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    IT("Events.on throws with invalid arguments", {
        EventsTestHelper::Setup();

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context,
                v8::String::NewFromUtf8Literal(isolate, "Core"),
                coreObj).Check();
            manager.GetEvents().Register(isolate, context, coreObj, &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        EQUALS(RunJSThrows(engine, "Core.Events.on()"), true);
        EQUALS(RunJSThrows(engine, "Core.Events.on('test')"), true);
        EQUALS(RunJSThrows(engine, "Core.Events.on(123, () => {})"), true);
        EQUALS(RunJSThrows(engine, "Core.Events.on('test', 'notafunction')"), true);

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

    // Throw-idiom convention: arg-shape errors are TypeError (not Error), and Events.off no longer
    // swallows bad arguments — it throws like Events.on. Guards the builtins conventions sweep.
    IT("Events arg-shape errors are TypeError and off is no longer silent", {
        EventsTestHelper::Setup();

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "Core"), coreObj).Check();
            manager.GetEvents().Register(isolate, context, coreObj, &manager);
            manager.SetCurrentResourceContext("testResource");
        }

        std::string err;
        // Arg-shape failures throw TypeError.
        err = RunJSErrorName(engine, "Core.Events.on()");
        STREQUALS(err.c_str(), "TypeError");
        err = RunJSErrorName(engine, "Core.Events.on(123, () => {})");
        STREQUALS(err.c_str(), "TypeError");
        err = RunJSErrorName(engine, "Core.Events.emit()");
        STREQUALS(err.c_str(), "TypeError");
        err = RunJSErrorName(engine, "Core.Events.emitTo('r')");
        STREQUALS(err.c_str(), "TypeError");

        // Events.off used to silently ignore bad arguments; it now throws TypeError like on().
        err = RunJSErrorName(engine, "Core.Events.off('e', 'notafn')");
        STREQUALS(err.c_str(), "TypeError");
        err = RunJSErrorName(engine, "Core.Events.off('e')");
        STREQUALS(err.c_str(), "TypeError");
        // A well-formed off() with no matching handler stays a quiet no-op.
        err = RunJSErrorName(engine, "Core.Events.off('e', () => {})");
        STREQUALS(err.c_str(), "");

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
    // CLIENT-EVENT CHANNEL TESTS (onClient)
    // ========================================
    // Assert the guarantee structurally via the two listener counts: onClient() handlers land in a
    // table disjoint from on() and are removed independently. The async emit round-trip stays covered
    // by integration testing (its Promise-aggregation teardown is flaky under this unit harness).

    auto registerEvents = [](NodeEngine &engine, ResourceManager &manager) {
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
        manager.SetCurrentResourceContext("testResource");
    };

    auto cleanupResource = [](NodeEngine &engine, ResourceManager &manager) {
        v8::Isolate *isolate = engine.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        manager.GetEvents().CleanupResource("testResource");
    };

    // The core guarantee: on() and onClient() (incl. onceClient) register into disjoint tables under
    // the same event name — so a client event, which only ever dispatches into the client table, can
    // never resolve to an on() handler.
    IT("onClient/onceClient register into a table disjoint from on", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);
        registerEvents(engine, manager);

        RunJS(engine, "Core.Events.onClient('shared', () => {}); 0");
        // In the client table, invisible to the global on() count.
        EQUALS(manager.GetEvents().GetClientListenerCount("shared"), (size_t)1);
        EQUALS(manager.GetEvents().GetListenerCount("shared"), (size_t)0);

        // Same name on the global bus lands in the other table; neither perturbs the other.
        RunJS(engine, "Core.Events.on('shared', () => {}); 0");
        EQUALS(manager.GetEvents().GetListenerCount("shared"), (size_t)1);
        EQUALS(manager.GetEvents().GetClientListenerCount("shared"), (size_t)1);

        RunJS(engine, "Core.Events.onceClient('once1', () => {}); 0");
        EQUALS(manager.GetEvents().GetClientListenerCount("once1"), (size_t)1);
        EQUALS(manager.GetEvents().GetListenerCount("once1"), (size_t)0);

        cleanupResource(engine, manager);
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // onClient's unsubscribe fn and offClient remove from the client table only, leaving on() intact.
    IT("onClient unsubscribe and offClient touch only the client table", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);
        registerEvents(engine, manager);

        RunJS(engine, R"(
            Core.Events.on('e', () => {});
            globalThis.u = Core.Events.onClient('e', () => {});
            0
        )");
        EQUALS(manager.GetEvents().GetClientListenerCount("e"), (size_t)1);
        EQUALS(manager.GetEvents().GetListenerCount("e"), (size_t)1);

        RunJS(engine, "globalThis.u(); 0");
        EQUALS(manager.GetEvents().GetClientListenerCount("e"), (size_t)0);
        EQUALS(manager.GetEvents().GetListenerCount("e"), (size_t)1); // on() handler untouched

        RunJS(engine, R"(
            globalThis.h = () => {};
            Core.Events.onClient('e', globalThis.h);
            0
        )");
        EQUALS(manager.GetEvents().GetClientListenerCount("e"), (size_t)1);
        RunJS(engine, "Core.Events.offClient('e', globalThis.h); 0");
        EQUALS(manager.GetEvents().GetClientListenerCount("e"), (size_t)0);

        cleanupResource(engine, manager);
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // CleanupResource drops client handlers along with global ones (a resource stop leaks neither).
    IT("CleanupResource clears client handlers", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);
        registerEvents(engine, manager);

        RunJS(engine, R"(
            Core.Events.on('e', () => {});
            Core.Events.onClient('e', () => {});
            0
        )");
        EQUALS(manager.GetEvents().GetClientListenerCount("e"), (size_t)1);
        EQUALS(manager.GetEvents().GetListenerCount("e"), (size_t)1);

        cleanupResource(engine, manager);
        EQUALS(manager.GetEvents().GetClientListenerCount("e"), (size_t)0);
        EQUALS(manager.GetEvents().GetListenerCount("e"), (size_t)0);

        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // ========================================
    // ALLSETTLED AGGREGATION ROBUSTNESS
    // ========================================
    // emit() aggregates via the script-mutable global Promise.allSettled, so its then-handler must
    // not trust the record shape. These drive it with hostile shapes: pre-fix each aborts the
    // process, post-fix each settles the emit() promise cleanly.

    // Throwing `status` getter: pre-fix, Get("status").ToLocalChecked() aborts on the empty MaybeLocal.
    IT("emit survives a Promise.allSettled whose records throw on access", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);
        registerEvents(engine, manager);

        RunJS(engine, R"(
            globalThis.__done = 0;
            Promise.allSettled = function () {
                return Promise.resolve([{ get status() { throw new Error('boom'); } }]);
            };
            Core.Events.on('hostile', () => 1);
            Core.Events.emit('hostile').then(() => { globalThis.__done = 1; },
                                             () => { globalThis.__done = 2; });
            0
        )");

        // Drain microtasks so the then-handler and the emit() continuation run.
        for (int i = 0; i < 8; ++i) engine.Tick();

        // No crash, and the emit() promise settled — resolved, since no well-formed rejection.
        EQUALS(RunJS(engine, "globalThis.__done"), 1);

        cleanupResource(engine, manager);
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // Non-array settled value: pre-fix it reaches info[0].As<v8::Array>() + Array::Length() (UB).
    IT("emit survives a Promise.allSettled that resolves with a non-array", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);
        registerEvents(engine, manager);

        RunJS(engine, R"(
            globalThis.__done2 = 0;
            Promise.allSettled = function () { return Promise.resolve("not-an-array"); };
            Core.Events.on('hostile2', () => 1);
            Core.Events.emit('hostile2').then(() => { globalThis.__done2 = 1; },
                                              () => { globalThis.__done2 = 2; });
            0
        )");

        for (int i = 0; i < 8; ++i) engine.Tick();

        EQUALS(RunJS(engine, "globalThis.__done2"), 1);

        cleanupResource(engine, manager);
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // Non-Promise return: pre-fix, allSettledResult.ToLocalChecked().As<v8::Promise>()->Then()
    // runs on a non-Promise handle and aborts. Post-fix the IsPromise() guard soft-resolves.
    IT("emit survives a Promise.allSettled that returns a non-Promise", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);
        registerEvents(engine, manager);

        RunJS(engine, R"(
            globalThis.__done3 = 0;
            Promise.allSettled = function () { return 42; };
            Core.Events.on('hostile3', () => 1);
            Core.Events.emit('hostile3').then(() => { globalThis.__done3 = 1; },
                                              () => { globalThis.__done3 = 2; });
            0
        )");

        for (int i = 0; i < 8; ++i) engine.Tick();

        EQUALS(RunJS(engine, "globalThis.__done3"), 1);

        cleanupResource(engine, manager);
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // Sanity: the hardening must not swallow genuine, well-formed rejections.
    IT("emit still rejects when a handler rejects (real allSettled)", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);
        registerEvents(engine, manager);

        RunJS(engine, R"(
            globalThis.__r = 0;
            Core.Events.on('boom', () => { throw new Error('handler failed'); });
            Core.Events.emit('boom').then(() => { globalThis.__r = 1; },
                                          () => { globalThis.__r = 2; });
            0
        )");

        for (int i = 0; i < 8; ++i) engine.Tick();

        EQUALS(RunJS(engine, "globalThis.__r"), 2);

        cleanupResource(engine, manager);
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // ========================================
    // IMPORTS (real cross-resource values)
    // ========================================
    // imports.get must return an object of *real* export values keyed by name — mirroring
    // Exports.get — not a placeholder listing export names. Regression guard for the old
    // `_availableExports` stub that returned names instead of values.
    IT("imports.get builds an object of real export values keyed by name", {
        EventsTestHelper::Setup();

        // Provider resource declaring two exports in its manifest.
        std::string providerPath = EventsTestHelper::GetTestPath() + "/provider";
        {
            cppfs::FileHandle dir = cppfs::fs::open(providerPath);
            if (!dir.exists()) dir.createDirectory();
            std::ofstream pkg(providerPath + "/package.json");
            pkg << R"({"name":"provider","version":"1.0.0","mafiahub":{"exports":["alpha","beta"]}})";
        }

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        v8::Isolate *isolate = engine.GetIsolate();
        {
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            Resource provider(providerPath);
            provider.SetIsolate(isolate);
            EQUALS(provider.RegisterExport("alpha", v8::Integer::New(isolate, 42)), true);
            EQUALS(provider.RegisterExport("beta", v8pp::to_v8(isolate, std::string("hello"))), true);

            v8::Local<v8::Object> imported = Imports::BuildImportsObject(isolate, context, &provider);
            context->Global()->Set(context, v8pp::to_v8(isolate, "__imp"), imported).Check();
        }

        // Real values, not a name list.
        EQUALS(RunJS(engine, "__imp.alpha"), 42);
        EQUALS(RunJSBool(engine, "__imp.beta === 'hello'"), true);
        // The old placeholder key must be gone, and only the two exports present.
        EQUALS(RunJSBool(engine, "__imp._availableExports === undefined"), true);
        EQUALS(RunJSBool(engine, "Object.keys(__imp).length === 2"), true);

        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // ========================================
    // MESSAGES (handler lifecycle)
    // ========================================
    // messages.handle stores a Global<Function> per (resource, type). A stopped resource must not
    // leave those handlers behind until full Shutdown — CleanupResource drops them eagerly, like
    // Events. (The cross-isolate guard added alongside can only be exercised with two live isolates,
    // which libnode cannot host in one process, so it is covered by integration testing.)
    auto registerMessages = [](NodeEngine &engine, ResourceManager &manager) {
        v8::Isolate *isolate = engine.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine.GetContext();
        v8::Context::Scope contextScope(context);
        v8::Local<v8::Object> frameworkObj = v8::Object::New(isolate);
        context->Global()->Set(context, v8pp::to_v8(isolate, "Framework"), frameworkObj).Check();
        Messages::Register(isolate, context, frameworkObj, &manager);
    };

    auto pumpMessages = [](NodeEngine &engine) {
        for (int i = 0; i < 8; ++i) {
            {
                v8::Isolate *isolate = engine.GetIsolate();
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                v8::HandleScope handleScope(isolate);
                v8::Local<v8::Context> context = engine.GetContext();
                v8::Context::Scope contextScope(context);
                Messages::ProcessPendingResponses(isolate, context);
            }
            engine.Tick();
        }
    };

    // A registered handler answers a same-isolate request; after CleanupResource the same request
    // is rejected because the handler is gone — proving the resource's handlers were released.
    IT("messages.request round-trips, and CleanupResource drops the handler", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);
        registerMessages(engine, manager);

        // Arg-shape failures throw TypeError (convention), before any state checks.
        std::string margErr;
        margErr = RunJSErrorName(engine, "Framework.messages.request(123)");
        STREQUALS(margErr.c_str(), "TypeError");
        margErr = RunJSErrorName(engine, "Framework.messages.send('r')");
        STREQUALS(margErr.c_str(), "TypeError");

        // Register the handler as resource 'provider'.
        manager.SetCurrentResourceContext("provider");
        RunJS(engine, "Framework.messages.handle('ping', (payload, reply) => { reply(payload + 1); }); 0");

        // Issue a request as resource 'consumer'.
        manager.SetCurrentResourceContext("consumer");
        RunJS(engine, R"(
            globalThis.__reply = 0;
            Framework.messages.request('provider', 'ping', 41).then(v => { globalThis.__reply = v; },
                                                                    () => { globalThis.__reply = -1; });
            0
        )");
        pumpMessages(engine);
        EQUALS(RunJS(engine, "globalThis.__reply"), 42);

        // Drop 'provider' handlers as its resource stops.
        Messages::CleanupResource("provider");

        // The same request now rejects: no handler remains for the target resource.
        RunJS(engine, R"(
            globalThis.__after = 0;
            Framework.messages.request('provider', 'ping', 1).then(() => { globalThis.__after = 1; },
                                                                   () => { globalThis.__after = 2; });
            0
        )");
        pumpMessages(engine);
        EQUALS(RunJS(engine, "globalThis.__after"), 2);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            Messages::Shutdown();
        }
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // ========================================
    // REGISTRATION SHAPE
    // ========================================
    // RegisterValueTypes publishes only the value types; Player gained a Register that publishes its
    // constructor like its handle-type siblings (Entity, TextLabel). Guards the registration sweep.
    IT("RegisterValueTypes and Player::Register publish their constructors", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        v8::Isolate *isolate = engine.GetIsolate();
        {
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);
            v8::Local<v8::Object> target = context->Global();
            Builtins::RegisterValueTypes(isolate, target);
            Builtins::Player::Register(isolate, target);
        }

        // Value types published by RegisterValueTypes.
        EQUALS(RunJSBool(engine, "typeof Vector3 === 'function'"), true);
        EQUALS(RunJSBool(engine, "typeof Color === 'function'"), true);
        // Player constructor published by the new Player::Register.
        EQUALS(RunJSBool(engine, "typeof Player === 'function'"), true);

        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // ========================================
    // JS-FACING NAMING
    // ========================================
    // The Framework.* namespace group uses one casing: exports joins the lowercase imports/messages
    // (was Framework.Exports). Environment flags are camelCase isClient/isServer (were IsClient/
    // IsServer). Guards the breaking rename; the old names must be gone.
    IT("Framework.exports is lowercase and Environment flags are camelCase", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
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
            v8::Local<v8::Object> frameworkObj = v8::Object::New(isolate);
            context->Global()->Set(context, v8pp::to_v8(isolate, "Framework"), frameworkObj).Check();
            v8::Local<v8::Object> coreObj = v8::Object::New(isolate);
            context->Global()->Set(context, v8pp::to_v8(isolate, "Core"), coreObj).Check();
            Exports::Register(isolate, context, frameworkObj, &manager);
            Environment::Register(isolate, context, coreObj, /*isClient*/ true);
        }

        // exports: lowercase, matching imports/messages; the capitalized name is gone.
        EQUALS(RunJSBool(engine, "typeof Framework.exports === 'object' && Framework.exports !== null"), true);
        EQUALS(RunJSBool(engine, "typeof Framework.exports.register === 'function'"), true);
        EQUALS(RunJSBool(engine, "typeof Framework.exports.get === 'function'"), true);
        EQUALS(RunJSBool(engine, "Framework.Exports === undefined"), true);

        // Environment flags: camelCase; the PascalCase names are gone.
        EQUALS(RunJSBool(engine, "Core.Environment.isClient === true"), true);
        EQUALS(RunJSBool(engine, "Core.Environment.isServer === false"), true);
        EQUALS(RunJSBool(engine, "Core.Environment.IsClient === undefined"), true);
        EQUALS(RunJSBool(engine, "Core.Environment.IsServer === undefined"), true);

        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // ========================================
    // RE-REGISTER SAFETY (callback-context reuse)
    // ========================================
    // A second Register() must not dangle the v8::Externals baked during the first one. Regression
    // guard for the double-Register use-after-free: externals captured before the second call must
    // still resolve to live memory afterwards.

    // An unsubscribe closure from the first Register() must still remove its handler after a second.
    IT("Second Register keeps a prior unsubscribe closure valid", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);

        // Stash the unsubscribe closure from the first Register().
        registerEvents(engine, manager);
        RunJS(engine, "globalThis.oldUnsub = Core.Events.on('persist', () => {}); 0");
        EQUALS(manager.GetEvents().GetListenerCount("persist"), (size_t)1);

        // Re-register on the same Events instance (the path that previously freed the context
        // out from under oldUnsub). Handler tables are untouched.
        registerEvents(engine, manager);
        EQUALS(manager.GetEvents().GetListenerCount("persist"), (size_t)1);

        // The old closure still reaches the live context and removes its handler.
        RunJS(engine, "globalThis.oldUnsub(); 0");
        EQUALS(manager.GetEvents().GetListenerCount("persist"), (size_t)0);

        cleanupResource(engine, manager);
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // The old Core.Events.on function (its template data holds the context) must still dispatch
    // into the same Events after a second Register(), and the new Core.Events must work too.
    IT("Second Register keeps prior function-template externals valid", {
        EventsTestHelper::Setup();
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        ResourceManagerConfig config;
        config.resourcesPath = EventsTestHelper::GetTestPath();
        ResourceManager manager(&engine, config);

        // Capture the on() function from the first Register(), then re-register over it.
        registerEvents(engine, manager);
        RunJS(engine, "globalThis.oldOn = Core.Events.on; 0");
        registerEvents(engine, manager);

        // The captured function still registers into the same Events instance.
        RunJS(engine, "globalThis.oldOn('again', () => {}); 0");
        EQUALS(manager.GetEvents().GetListenerCount("again"), (size_t)1);

        // And the freshly-installed Core.Events works too.
        RunJS(engine, "Core.Events.on('fresh', () => {}); 0");
        EQUALS(manager.GetEvents().GetListenerCount("fresh"), (size_t)1);

        cleanupResource(engine, manager);
        engine.Shutdown();
        EventsTestHelper::Cleanup();
    });

    // ========================================
    // CONSOLE BUILTIN TESTS
    // ========================================

    IT("console methods are callable without crash", {
        EventsTestHelper::Setup();

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

    IT("Console::FormatValue formats functions as [Function]", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            // Test arrow function
            v8::Local<v8::String> arrowCode = v8::String::NewFromUtf8(isolate, "() => {}").ToLocalChecked();
            v8::Local<v8::Value> arrowFn = v8::Script::Compile(context, arrowCode).ToLocalChecked()->Run(context).ToLocalChecked();
            std::string arrowResult = Console::FormatValue(isolate, arrowFn);
            STREQUALS(arrowResult.c_str(), "[Function]");

            // Test regular function
            v8::Local<v8::String> funcCode = v8::String::NewFromUtf8(isolate, "(function myFunc() {})").ToLocalChecked();
            v8::Local<v8::Value> func = v8::Script::Compile(context, funcCode).ToLocalChecked()->Run(context).ToLocalChecked();
            std::string funcResult = Console::FormatValue(isolate, func);
            STREQUALS(funcResult.c_str(), "[Function]");

            // Test native function (Math.max)
            v8::Local<v8::String> nativeCode = v8::String::NewFromUtf8(isolate, "Math.max").ToLocalChecked();
            v8::Local<v8::Value> nativeFn = v8::Script::Compile(context, nativeCode).ToLocalChecked()->Run(context).ToLocalChecked();
            std::string nativeResult = Console::FormatValue(isolate, nativeFn);
            STREQUALS(nativeResult.c_str(), "[Function]");
        }

        engine.Shutdown();
    });

    IT("Console::FormatValue formats primitive types correctly", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            // Test string
            v8::Local<v8::Value> strVal = v8::String::NewFromUtf8(isolate, "hello").ToLocalChecked();
            STREQUALS(Console::FormatValue(isolate, strVal).c_str(), "hello");

            // Test integer
            v8::Local<v8::Value> intVal = v8::Number::New(isolate, 42);
            STREQUALS(Console::FormatValue(isolate, intVal).c_str(), "42");

            // Test boolean true
            v8::Local<v8::Value> trueVal = v8::Boolean::New(isolate, true);
            STREQUALS(Console::FormatValue(isolate, trueVal).c_str(), "true");

            // Test boolean false
            v8::Local<v8::Value> falseVal = v8::Boolean::New(isolate, false);
            STREQUALS(Console::FormatValue(isolate, falseVal).c_str(), "false");

            // Test null
            v8::Local<v8::Value> nullVal = v8::Null(isolate);
            STREQUALS(Console::FormatValue(isolate, nullVal).c_str(), "null");

            // Test undefined
            v8::Local<v8::Value> undefVal = v8::Undefined(isolate);
            STREQUALS(Console::FormatValue(isolate, undefVal).c_str(), "undefined");
        }

        engine.Shutdown();
    });

    IT("Console::FormatValue formats arrays correctly", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            // Test empty array
            v8::Local<v8::String> emptyArrCode = v8::String::NewFromUtf8(isolate, "[]").ToLocalChecked();
            v8::Local<v8::Value> emptyArr = v8::Script::Compile(context, emptyArrCode).ToLocalChecked()->Run(context).ToLocalChecked();
            STREQUALS(Console::FormatValue(isolate, emptyArr).c_str(), "[Array(0)]");

            // Test array with 3 elements
            v8::Local<v8::String> arrCode = v8::String::NewFromUtf8(isolate, "[1, 2, 3]").ToLocalChecked();
            v8::Local<v8::Value> arr = v8::Script::Compile(context, arrCode).ToLocalChecked()->Run(context).ToLocalChecked();
            STREQUALS(Console::FormatValue(isolate, arr).c_str(), "[Array(3)]");
        }

        engine.Shutdown();
    });

    IT("Console::FormatValue formats objects as JSON", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            // Test simple object
            v8::Local<v8::String> objCode = v8::String::NewFromUtf8(isolate, "({x: 1, y: 2})").ToLocalChecked();
            v8::Local<v8::Value> obj = v8::Script::Compile(context, objCode).ToLocalChecked()->Run(context).ToLocalChecked();
            std::string objResult = Console::FormatValue(isolate, obj);
            // JSON output should contain x and y
            EQUALS(objResult.find("\"x\"") != std::string::npos, true);
            EQUALS(objResult.find("\"y\"") != std::string::npos, true);
        }

        engine.Shutdown();
    });

    IT("Console::FormatValue handles circular references safely", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            // Create circular reference
            v8::Local<v8::String> circularCode = v8::String::NewFromUtf8(isolate,
                "(function() { const obj = {}; obj.self = obj; return obj; })()").ToLocalChecked();
            v8::Local<v8::Value> circularObj = v8::Script::Compile(context, circularCode).ToLocalChecked()->Run(context).ToLocalChecked();

            // Should not crash, should return [Object] for circular
            std::string result = Console::FormatValue(isolate, circularObj);
            STREQUALS(result.c_str(), "[Object]");
        }

        engine.Shutdown();
    });

    IT("Console::FormatValue handles objects with function properties", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            // Object with function property - JSON.stringify returns {} for function values
            v8::Local<v8::String> objCode = v8::String::NewFromUtf8(isolate, "({fn: () => {}})").ToLocalChecked();
            v8::Local<v8::Value> obj = v8::Script::Compile(context, objCode).ToLocalChecked()->Run(context).ToLocalChecked();

            // Should not crash - JSON.stringify will omit the function
            std::string result = Console::FormatValue(isolate, obj);
            EQUALS(result.empty(), false);
        }

        engine.Shutdown();
    });

});
