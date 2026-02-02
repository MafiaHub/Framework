/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/node_engine.h"

MODULE(engine, {
    using namespace Framework::Scripting;

    IT("can allocate and deallocate a valid Node.js engine instance", {
        NodeEngineOptions options;
        options.processName = "test-server";
        NodeEngine *pEngine = new NodeEngine(options);

        EQUALS(pEngine->Init(), true);
        NEQUALS(pEngine->GetIsolate(), nullptr);
        EQUALS(pEngine->IsSandboxed(), false);

        pEngine->Shutdown();
        delete pEngine;
    });

    IT("returns context after initialization", {
        NodeEngine engine({});
        EQUALS(engine.Init(), true);

        // V8 scopes must exit before Shutdown() is called
        {
            v8::Isolate *isolate = engine.GetIsolate();
            NEQUALS(isolate, nullptr);

            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);

            v8::Local<v8::Context> context = engine.GetContext();
            EQUALS(context.IsEmpty(), false);
        }

        engine.Shutdown();
    });

    IT("invokes SDK register callback during init", {
        NodeEngine engine({});
        bool callbackInvoked = false;
        Engine *callbackEngine = nullptr;

        auto callback = [&callbackInvoked, &callbackEngine](Engine *eng) {
            callbackInvoked = true;
            callbackEngine = eng;
        };

        engine.SetSDKRegisterCallback(callback);
        EQUALS(engine.Init(), true);

        // SDK callback is invoked during InitFrameworkSDK
        engine.InitFrameworkSDK();

        EQUALS(callbackInvoked, true);
        NEQUALS(callbackEngine, nullptr);

        engine.Shutdown();
    });

    IT("can execute basic JavaScript code", {
        NodeEngine engine({});
        EQUALS(engine.Init(), true);

        int resultValue = 0;
        // V8 scopes must exit before Shutdown() is called
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            // Execute simple arithmetic
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, "2 + 2").ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

            EQUALS(result->IsNumber(), true);
            resultValue = result->Int32Value(context).FromJust();
        }

        EQUALS(resultValue, 4);
        engine.Shutdown();
    });

    IT("can execute JavaScript with variables and functions", {
        NodeEngine engine({});
        EQUALS(engine.Init(), true);

        int resultValue = 0;
        // V8 scopes must exit before Shutdown() is called
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            // Execute code with function
            const char *code = R"(
                function add(a, b) { return a + b; }
                add(3, 7);
            )";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

            EQUALS(result->IsNumber(), true);
            resultValue = result->Int32Value(context).FromJust();
        }

        EQUALS(resultValue, 10);
        engine.Shutdown();
    });

    IT("shutdown is idempotent", {
        NodeEngine engine({});
        EQUALS(engine.Init(), true);

        engine.Shutdown();
        engine.Shutdown(); // Should not crash
    });

    IT("IsInitialized returns correct state", {
        NodeEngine engine({});

        EQUALS(engine.IsInitialized(), false);

        EQUALS(engine.Init(), true);
        EQUALS(engine.IsInitialized(), true);

        engine.Shutdown();
        EQUALS(engine.IsInitialized(), false);
    });

    IT("GetLastError is empty on success", {
        NodeEngine engine({});
        EQUALS(engine.Init(), true);
        STREQUALS(engine.GetLastError().c_str(), "");
        engine.Shutdown();
    });

    IT("Tick processes event loop without crashing", {
        NodeEngine engine({});
        EQUALS(engine.Init(), true);

        // Tick should not crash
        engine.Tick();
        engine.Tick();
        engine.Tick();

        engine.Shutdown();
    });

    IT("can create sandboxed engine for client", {
        NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "test-client";
        NodeEngine engine(options);

        EQUALS(engine.Init(), true);
        EQUALS(engine.IsSandboxed(), true);
        NEQUALS(engine.GetIsolate(), nullptr);

        engine.Shutdown();
    });

    IT("sandboxed engine blocks fs module", {
        NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "test-sandbox";
        NodeEngine engine(options);
        EQUALS(engine.Init(), true);

        // Try to require fs - should throw
        bool threwError = false;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::TryCatch tryCatch(isolate);
            const char *code = "require('fs')";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::MaybeLocal<v8::Value> result = script->Run(context);

            if (tryCatch.HasCaught() || result.IsEmpty()) {
                threwError = true;
            }
        }

        EQUALS(threwError, true);
        engine.Shutdown();
    });

    IT("sandboxed engine blocks net module", {
        NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "test-sandbox";
        NodeEngine engine(options);
        EQUALS(engine.Init(), true);

        // Try to require net - should throw
        bool threwError = false;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::TryCatch tryCatch(isolate);
            const char *code = "require('net')";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::MaybeLocal<v8::Value> result = script->Run(context);

            if (tryCatch.HasCaught() || result.IsEmpty()) {
                threwError = true;
            }
        }

        EQUALS(threwError, true);
        engine.Shutdown();
    });

    IT("sandboxed engine blocks child_process module", {
        NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "test-sandbox";
        NodeEngine engine(options);
        EQUALS(engine.Init(), true);

        // Try to require child_process - should throw
        bool threwError = false;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::TryCatch tryCatch(isolate);
            const char *code = "require('child_process')";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::MaybeLocal<v8::Value> result = script->Run(context);

            if (tryCatch.HasCaught() || result.IsEmpty()) {
                threwError = true;
            }
        }

        EQUALS(threwError, true);
        engine.Shutdown();
    });

    IT("sandboxed engine allows safe modules like path", {
        NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "test-sandbox";
        NodeEngine engine(options);
        EQUALS(engine.Init(), true);

        // path module should work
        bool success = false;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::TryCatch tryCatch(isolate);
            const char *code = "const path = require('path'); path.join('a', 'b')";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::MaybeLocal<v8::Value> result = script->Run(context);

            if (!tryCatch.HasCaught() && !result.IsEmpty()) {
                success = true;
            }
        }

        EQUALS(success, true);
        engine.Shutdown();
    });

    IT("sandboxed engine blocks process.exit", {
        NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "test-sandbox";
        NodeEngine engine(options);
        EQUALS(engine.Init(), true);

        // process.exit should throw
        bool threwError = false;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::TryCatch tryCatch(isolate);
            const char *code = "process.exit(0)";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::MaybeLocal<v8::Value> result = script->Run(context);

            if (tryCatch.HasCaught() || result.IsEmpty()) {
                threwError = true;
            }
        }

        EQUALS(threwError, true);
        engine.Shutdown();
    });

    IT("sandboxed engine hides process.env", {
        NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "test-sandbox";
        NodeEngine engine(options);
        EQUALS(engine.Init(), true);

        // process.env should be empty frozen object
        int envKeyCount = -1;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            const char *code = "Object.keys(process.env).length";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

            envKeyCount = result->Int32Value(context).FromJust();
        }

        EQUALS(envKeyCount, 0);
        engine.Shutdown();
    });

    IT("non-sandboxed engine allows fs module", {
        NodeEngineOptions options;
        options.sandboxed = false;
        options.processName = "test-server";
        NodeEngine engine(options);
        EQUALS(engine.Init(), true);

        // fs module should work
        bool success = false;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::TryCatch tryCatch(isolate);
            const char *code = "const fs = require('fs'); typeof fs.readFileSync";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::MaybeLocal<v8::Value> result = script->Run(context);

            if (!tryCatch.HasCaught() && !result.IsEmpty()) {
                success = true;
            }
        }

        EQUALS(success, true);
        engine.Shutdown();
    });
})
