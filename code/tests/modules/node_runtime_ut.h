/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/node_engine.h"
#include "scripting/node_runtime.h"

#include "node_test_helpers.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace NodeRuntimeTest {
    // Ticks the runtime until `condition` holds or `timeoutMs` passes.
    template <typename Condition>
    static bool TickUntil(Framework::Scripting::NodeRuntime &runtime, Condition &&condition, int timeoutMs = 5000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            runtime.Tick();
            if (condition()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }

    static std::uintmax_t FileSize(const std::filesystem::path &path) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        return ec ? 0 : size;
    }

    // Enters the runtime passed as data from inside another runtime's call and evaluates its first argument there.
    static void EvalInOther(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *other = static_cast<Framework::Scripting::NodeRuntime *>(info.Data().As<v8::External>()->Value());
        v8::String::Utf8Value source(info.GetIsolate(), info[0]);
        const std::string result = NodeTest::Eval(*other, *source);
        info.GetReturnValue().Set(v8::String::NewFromUtf8(info.GetIsolate(), result.c_str()).ToLocalChecked());
    }

    // Calls a global function straight through V8, as an event handler or another runtime's call enters, and compiles
    // nothing: a compile can post a platform task, and the platform runs those in a callback scope that drains ticks.
    static std::string CallGlobal(Framework::Scripting::NodeRuntime &runtime, const char *name) {
        v8::Isolate *isolate = runtime.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = runtime.GetContext();
        v8::Context::Scope contextScope(context);

        v8::Local<v8::Value> fn = context->Global()->Get(context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked()).ToLocalChecked();
        v8::Local<v8::Value> result;
        if (!fn->IsFunction() || !fn.As<v8::Function>()->Call(context, v8::Undefined(isolate), 0, nullptr).ToLocal(&result)) {
            return "<failed>";
        }
        v8::String::Utf8Value text(isolate, result);
        return *text ? *text : "";
    }
} // namespace NodeRuntimeTest

MODULE(node_runtime, {
    using namespace Framework::Scripting;

    IT("gives every runtime its own globals", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        std::string error;
        auto first  = engine.CreateRuntime(error);
        auto second = engine.CreateRuntime(error);
        NEQUALS(first.get(), nullptr);
        NEQUALS(second.get(), nullptr);
        NEQUALS(first->GetIsolate(), second->GetIsolate());

        STREQUALS(NodeTest::Eval(*first, "globalThis.owner = 'first'; owner").c_str(), "first");
        STREQUALS(NodeTest::Eval(*second, "typeof globalThis.owner").c_str(), "undefined");
        STREQUALS(NodeTest::Eval(*second, "typeof require('node:worker_threads').Worker").c_str(), "function");

        first.reset();
        second.reset();
        engine.Shutdown();
    });

    IT("leaves process state to the engine's own environment", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        std::string error;
        auto runtime = engine.CreateRuntime(error);
        NEQUALS(runtime.get(), nullptr);

        STREQUALS(NodeTest::Eval(*runtime, "try { process.chdir(process.cwd()); 'changed' } catch (e) { e.code }").c_str(), "ERR_WORKER_UNSUPPORTED_OPERATION");

        runtime.reset();
        engine.Shutdown();
    });

    IT("lets one runtime call into another on the same thread", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        std::string error;
        auto caller = engine.CreateRuntime(error);
        auto callee = engine.CreateRuntime(error);
        NEQUALS(caller.get(), nullptr);
        NEQUALS(callee.get(), nullptr);

        NodeTest::Eval(*callee, "globalThis.base = 40");
        {
            v8::Isolate *isolate = caller->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = caller->GetContext();
            v8::Context::Scope contextScope(context);
            auto fn = v8::FunctionTemplate::New(isolate, NodeRuntimeTest::EvalInOther, v8::External::New(isolate, callee.get()))->GetFunction(context).ToLocalChecked();
            context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "evalInOther"), fn).Check();
        }

        STREQUALS(NodeTest::Eval(*caller, "evalInOther('base + 2') + ':' + typeof globalThis.base").c_str(), "42:undefined");

        caller.reset();
        callee.reset();
        engine.Shutdown();
    });

    IT("runs on its next tick the nextTick callbacks a call from outside Node queues", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        std::string error;
        auto runtime = engine.CreateRuntime(error);
        NEQUALS(runtime.get(), nullptr);

        NodeTest::Eval(*runtime, "globalThis.order = [];"
                                 "globalThis.queue = () => {"
                                 "  process.nextTick(() => order.push('tick'));"
                                 "  new Promise((resolve) => process.nextTick(resolve)).then(() => order.push('then'));"
                                 "};"
                                 "globalThis.read = () => order.join();");
        // Let the tasks bootstrapping and compiling posted run first, so none drains the queue by accident below.
        NodeRuntimeTest::TickUntil(*runtime, [] { return false; }, 200);

        // The loop has nothing pending, as in a resource whose pooled MySQL connection sits idle: mysql2 hands it out
        // through process.nextTick, so another resource's query stalled until unrelated I/O woke the runtime.
        NodeRuntimeTest::CallGlobal(*runtime, "queue");
        runtime->Tick();
        STREQUALS(NodeRuntimeTest::CallGlobal(*runtime, "read").c_str(), "tick,then");

        runtime.reset();
        engine.Shutdown();
    });

    IT("stops a running worker thread when its runtime is destroyed", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        const auto heartbeat = std::filesystem::temp_directory_path() / "fw_node_runtime_worker_heartbeat.txt";
        std::filesystem::remove(heartbeat);

        std::string error;
        auto runtime = engine.CreateRuntime(error);
        NEQUALS(runtime.get(), nullptr);

        // The worker appends to the file every few milliseconds for as long as its thread lives.
        const std::string path = heartbeat.generic_string();
        NodeTest::Eval(*runtime, "const { Worker } = require('node:worker_threads');"
                                        "globalThis.worker = new Worker(\"const fs = require('node:fs'); setInterval(() => fs.appendFileSync('" + path + "', '.'), 5);\", { eval: true });");

        EQUALS(NodeRuntimeTest::TickUntil(*runtime, [&] { return NodeRuntimeTest::FileSize(heartbeat) > 5; }), true);

        runtime.reset();

        // Allow a write that was already in flight to land, then the file must stop growing.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto sizeAfterDestroy = NodeRuntimeTest::FileSize(heartbeat);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EQUALS(NodeRuntimeTest::FileSize(heartbeat), sizeAfterDestroy);

        std::filesystem::remove(heartbeat);
        engine.Shutdown();
    });
});
