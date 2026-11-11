/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/engine_scope.h"
#include "scripting/node_engine.h"

#include <csignal>
#include <string>

namespace EngineTest {
    // Runs `source` in the engine's current runtime through raw V8, with no callback scope, as a call past the
    // framework's entries does, and returns the result as text, or "<threw> " and the exception.
    static std::string Eval(Framework::Scripting::Engine &engine, const std::string &source) {
        v8::Isolate *isolate = engine.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine.GetContext();
        v8::Context::Scope contextScope(context);
        v8::TryCatch tryCatch(isolate);

        v8::Local<v8::Script> script;
        v8::Local<v8::Value> result;
        if (!v8::Script::Compile(context, v8::String::NewFromUtf8(isolate, source.c_str()).ToLocalChecked()).ToLocal(&script) || !script->Run(context).ToLocal(&result)) {
            v8::String::Utf8Value text(isolate, tryCatch.Exception());
            return std::string("<threw> ") + (*text ? *text : "");
        }
        v8::String::Utf8Value text(isolate, result);
        return *text ? *text : "";
    }

    // Calls globalThis[name] inside an EngineScope the host holds, as native code calling into script does.
    static void CallGlobal(const Framework::Scripting::EngineScope &scope, const char *name) {
        v8::Isolate *isolate           = scope.Isolate();
        v8::Local<v8::Context> context = scope.Context();
        v8::Local<v8::Value> fn        = context->Global()->Get(context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked()).ToLocalChecked();
        (void)fn.As<v8::Function>()->Call(context, v8::Undefined(isolate), 0, nullptr);
    }
} // namespace EngineTest

MODULE(engine, {
    using namespace Framework::Scripting;

    IT("can allocate and deallocate a valid Node.js engine instance", {
        NodeEngineOptions options;
        options.processName = "test-server";
        NodeEngine *pEngine = new NodeEngine(options);

        const auto shutdownHandler = +[](int) {};
        const auto previousHandler = std::signal(SIGTERM, shutdownHandler);
        EQUALS(pEngine->Init(), ScriptingError::SCRIPTING_NONE);
        const bool shutdownHandlerPreserved = std::signal(SIGTERM, previousHandler) == shutdownHandler;
        EQUALS(shutdownHandlerPreserved, true);
        NEQUALS(pEngine->GetIsolate(), nullptr);
        EQUALS(pEngine->IsSandboxed(), false);

        pEngine->Shutdown();
        delete pEngine;
    });

    IT("returns context after initialization", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        // SDK callback is invoked during InitFrameworkSDK
        engine.InitFrameworkSDK();

        EQUALS(callbackInvoked, true);
        NEQUALS(callbackEngine, nullptr);

        engine.Shutdown();
    });

    IT("can execute basic JavaScript code", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        engine.Shutdown();
        engine.Shutdown(); // Should not crash
    });

    IT("IsInitialized returns correct state", {
        NodeEngine engine({});

        EQUALS(engine.IsInitialized(), false);

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        EQUALS(engine.IsInitialized(), true);

        engine.Shutdown();
        EQUALS(engine.IsInitialized(), false);
    });

    IT("GetLastError is empty on success", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        STREQUALS(engine.GetLastError().c_str(), "");
        engine.Shutdown();
    });

    IT("runs what a script queued as Execute returns", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        EQUALS(engine.Execute("globalThis.order = ['script']; process.nextTick(() => order.push('tick')); Promise.resolve().then(() => order.push('micro')); order.push('end');", "<queue>"), true);
        // Nothing ticked: the script ran inside Node's callback scope, which drained the queues on its way out, the
        // nextTick callbacks before the microtasks as Node orders them.
        STREQUALS(EngineTest::Eval(engine, "order.join()").c_str(), "script,end,tick,micro");

        engine.Shutdown();
    });

    IT("attributes a nextTick callback's throw to the runtime, not to the script that queued it", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        {
            EngineScope scope(&engine);
            engine.InstallUncaughtExceptionHandler("");
        }

        EQUALS(engine.Execute("process.nextTick(() => { throw new Error('later'); });", "<queue>"), true);
        STREQUALS(engine.GetLastError().c_str(), "");
        const auto errors = engine.DrainPendingErrors();
        EQUALS(errors.size(), size_t(1));
        EQUALS(errors.empty() || errors[0].errorMessage.contains("later"), true);

        engine.Shutdown();
    });

    IT("runs what the host queued inside an EngineScope as the scope closes", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        EngineTest::Eval(engine, "globalThis.order = [];"
                                 "globalThis.a = () => { order.push('a'); process.nextTick(() => order.push('a-tick')); Promise.resolve().then(() => order.push('a-micro')); };"
                                 "globalThis.b = () => order.push('b');");

        // Two calls inside one scope are one callback into the runtime: it drains once, as the scope closes.
        {
            EngineScope scope(&engine);
            EngineTest::CallGlobal(scope, "a");
            EngineTest::CallGlobal(scope, "b");
        }
        STREQUALS(EngineTest::Eval(engine, "order.splice(0).join()").c_str(), "a,b,a-tick,a-micro");

        // A scope per call: each drains as its scope closes, before the host makes the next call.
        {
            EngineScope scope(&engine);
            EngineTest::CallGlobal(scope, "a");
        }
        {
            EngineScope scope(&engine);
            EngineTest::CallGlobal(scope, "b");
        }
        STREQUALS(EngineTest::Eval(engine, "order.splice(0).join()").c_str(), "a,a-tick,a-micro,b");

        engine.Shutdown();
    });

    IT("Tick processes event loop without crashing", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        EQUALS(engine.IsSandboxed(), true);
        NEQUALS(engine.GetIsolate(), nullptr);

        engine.Shutdown();
    });

    IT("sandboxed engine blocks fs module", {
        NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "test-sandbox";
        NodeEngine engine(options);
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

    IT("sandboxed engine does not expose require internals", {
        NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "test-sandbox";
        NodeEngine engine(options);
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        bool internalsHidden = false;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            const char *code = "typeof require.main === 'undefined' && typeof require.cache === 'undefined'";
            v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
            v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

            internalsHidden = result->BooleanValue(isolate);
        }

        EQUALS(internalsHidden, true);
        engine.Shutdown();
    });

    IT("non-sandboxed engine allows fs module", {
        NodeEngineOptions options;
        options.sandboxed = false;
        options.processName = "test-server";
        NodeEngine engine(options);
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

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

    // ========================================
    // ERROR FORMATTING TESTS
    // ========================================

    IT("Execute returns error with stack trace for TypeError", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        // Code that causes a TypeError - call undefined as function
        const char *code = "const x = undefined; x();";

        bool result = engine.Execute(code, "test-error.js");
        EQUALS(result, false);

        std::string error = engine.GetLastError();
        // Should contain the error type
        EQUALS(error.contains("TypeError"), true);
        // Should contain the filename from ScriptOrigin
        EQUALS(error.contains("test-error.js"), true);
        // Should contain line number (format: filename:line)
        EQUALS(error.contains("test-error.js:1"), true);

        engine.Shutdown();
    });

    IT("Execute returns error with stack trace for ReferenceError", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        // Code that causes a ReferenceError
        const char *code = "undefinedVariable.foo()";

        bool result = engine.Execute(code, "reference-test.js");
        EQUALS(result, false);

        std::string error = engine.GetLastError();
        // Should contain the error type
        EQUALS(error.contains("ReferenceError"), true);
        // Should contain filename
        EQUALS(error.contains("reference-test.js"), true);
        // Should contain "at" keyword indicating stack trace
        EQUALS(error.contains("at "), true);

        engine.Shutdown();
    });

    IT("Execute returns error with correct line number for multi-line code", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        // Multi-line code with error on line 5 (1 empty + 3 const + 1 error)
        const char *code =
            "const a = 1;\n"
            "const b = 2;\n"
            "const c = 3;\n"
            "nonExistentFunction();";

        bool result = engine.Execute(code, "multiline-test.js");
        EQUALS(result, false);

        std::string error = engine.GetLastError();
        // Should contain ReferenceError
        EQUALS(error.contains("ReferenceError"), true);
        // Should contain filename
        EQUALS(error.contains("multiline-test.js"), true);
        // Should contain line 4 (the error line)
        EQUALS(error.contains("multiline-test.js:4"), true);

        engine.Shutdown();
    });

    IT("Execute returns syntax error with location info", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        // Code with syntax error
        const char *code = "function( { broken syntax";

        bool result = engine.Execute(code, "syntax-test.js");
        EQUALS(result, false);

        std::string error = engine.GetLastError();
        // Should contain SyntaxError
        EQUALS(error.contains("SyntaxError"), true);
        // Should contain some location info (line number indicator via colon)
        // Syntax errors may not always include filename but should have line/column
        EQUALS(error.contains(":"), true);

        engine.Shutdown();
    });

    IT("Execute returns stack trace with nested function calls", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        // Code with nested function calls to test stack depth
        const char *code =
            "function level1() { level2(); }\n"
            "function level2() { level3(); }\n"
            "function level3() { throw new Error('deep error'); }\n"
            "level1();";

        bool result = engine.Execute(code, "nested-test.js");
        EQUALS(result, false);

        std::string error = engine.GetLastError();
        // Should contain the error message
        EQUALS(error.contains("deep error"), true);
        // Should contain filename
        EQUALS(error.contains("nested-test.js"), true);
        // Should show multiple stack frames (multiple "at" occurrences)
        size_t firstAt = error.find("at ");
        EQUALS(firstAt != std::string::npos, true);
        size_t secondAt = error.find("at ", firstAt + 1);
        EQUALS(secondAt != std::string::npos, true);
        // Should contain function names in stack
        EQUALS(error.contains("level3"), true);
        EQUALS(error.contains("level2"), true);
        EQUALS(error.contains("level1"), true);

        engine.Shutdown();
    });
})
