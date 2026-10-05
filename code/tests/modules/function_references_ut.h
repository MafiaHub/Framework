/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/function_references.h"
#include "scripting/node_engine.h"
#include "scripting/node_runtime.h"
#include "scripting/value_transfer.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace FunctionReferencesTest {
    static std::string Eval(Framework::Scripting::NodeRuntime &runtime, const std::string &source) {
        v8::Isolate *isolate = runtime.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = runtime.GetContext();
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

    // Two runtimes taking part in calls, as an exporting and a calling resource would.
    struct Pair {
        Framework::Scripting::NodeEngine engine;
        std::unique_ptr<Framework::Scripting::NodeRuntime> owner;
        std::unique_ptr<Framework::Scripting::NodeRuntime> caller;
        std::unique_ptr<Framework::Scripting::FunctionReferences> references;
        uint32_t ownerId  = 0;
        uint32_t callerId = 0;

        Pair() {
            engine.Init();
            std::string error;
            owner      = engine.CreateRuntime(error);
            caller     = engine.CreateRuntime(error);
            references = std::make_unique<Framework::Scripting::FunctionReferences>();
            ownerId    = Join(*owner, "owner");
            callerId   = Join(*caller, "caller");
        }

        ~Pair() {
            if (owner) {
                references->RemoveRuntime(ownerId);
            }
            references->RemoveRuntime(callerId);
            references.reset();
            owner.reset();
            caller.reset();
            engine.Shutdown();
        }

        uint32_t Join(Framework::Scripting::NodeRuntime &runtime, const char *name) {
            v8::Isolate *isolate = runtime.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            return references->AddRuntime(isolate, runtime.GetContext(), name);
        }

        // The owner stops: it leaves the references, then its environment goes.
        void StopOwner() {
            references->RemoveRuntime(ownerId);
            owner.reset();
        }

        // Ticks both runtimes until `condition` holds in the caller.
        bool TickUntil(const std::string &condition) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline) {
                if (owner) {
                    owner->Tick();
                }
                caller->Tick();
                if (Eval(*caller, condition) == "true") {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        }
    };

    // Copies `expression` out of `from` and stores what arrives as globalThis[name] in `to`.
    static std::string Send(Pair &pair, Framework::Scripting::NodeRuntime &from, uint32_t fromId, Framework::Scripting::NodeRuntime &to, uint32_t toId, const std::string &expression, const char *name) {
        Framework::Scripting::TransferredValue copied;
        {
            v8::Isolate *isolate = from.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = from.GetContext();
            v8::Context::Scope contextScope(context);
            v8::Local<v8::Value> value = v8::Script::Compile(context, v8::String::NewFromUtf8(isolate, ("(" + expression + ")").c_str()).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();
            auto result                = Framework::Scripting::ValueTransfer::Copy(isolate, context, value, pair.references->For(fromId));
            if (!result) {
                return result.GetError();
            }
            copied = result.GetValue();
        }

        v8::Isolate *isolate = to.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = to.GetContext();
        v8::Context::Scope contextScope(context);
        v8::Local<v8::Value> rebuilt;
        if (!Framework::Scripting::ValueTransfer::Rebuild(isolate, context, copied, pair.references->For(toId)).ToLocal(&rebuilt)) {
            return "rebuild failed";
        }
        context->Global()->Set(context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), rebuilt).Check();
        return "";
    }

    static std::string Export(Pair &pair, const std::string &expression) {
        return Send(pair, *pair.owner, pair.ownerId, *pair.caller, pair.callerId, expression, "api");
    }
} // namespace FunctionReferencesTest

MODULE(function_references, {
    using namespace Framework::Scripting;

    IT("calls a function in the runtime that owns it", {
        FunctionReferencesTest::Pair pair;
        FunctionReferencesTest::Eval(*pair.owner, "globalThis.secret = 40");
        STREQUALS(FunctionReferencesTest::Export(pair, "{ add(n) { return { total: secret + n, where: typeof secret } } }").c_str(), "");
        STREQUALS(FunctionReferencesTest::Eval(*pair.caller, "const r = api.add(2); [r.total, r.where, typeof secret].join('|')").c_str(), "42|number|undefined");
    });

    IT("passes callbacks both ways and brings a function home as itself", {
        FunctionReferencesTest::Pair pair;
        FunctionReferencesTest::Eval(*pair.owner, "globalThis.marker = function marker() {}");
        STREQUALS(FunctionReferencesTest::Export(pair, "{ each(list, visit) { return list.map(visit) }, marker, isMarker(fn) { return fn === marker } }").c_str(), "");
        STREQUALS(FunctionReferencesTest::Eval(*pair.caller, "api.each([1, 2], n => n * 10).join() + '|' + api.isMarker(api.marker)").c_str(), "10,20|true");
    });

    IT("rethrows on the caller's side", {
        FunctionReferencesTest::Pair pair;
        STREQUALS(FunctionReferencesTest::Export(pair, "{ fail() { throw new RangeError('out of stock') } }").c_str(), "");
        STREQUALS(FunctionReferencesTest::Eval(*pair.caller, "try { api.fail(); 'no' } catch (e) { [e instanceof Error, e.name, e.message].join('|') }").c_str(), "true|RangeError|out of stock");
    });

    IT("settles a returned promise on the caller's side", {
        FunctionReferencesTest::Pair pair;
        STREQUALS(FunctionReferencesTest::Export(pair, "{ async load(id) { await null; return { id, name: 'sword' } }, async refuse() { throw new Error('locked') } }").c_str(), "");
        FunctionReferencesTest::Eval(*pair.caller, "globalThis.out = []; api.load(7).then(v => out.push(v.id + ':' + v.name)); api.refuse().catch(e => out.push(e.message))");
        EQUALS(pair.TickUntil("out.length === 2"), true);
        STREQUALS(FunctionReferencesTest::Eval(*pair.caller, "out.sort().join('|')").c_str(), "7:sword|locked");
    });

    IT("throws once the owner has stopped, and rejects what it still owed", {
        FunctionReferencesTest::Pair pair;
        STREQUALS(FunctionReferencesTest::Export(pair, "{ ping() { return 'pong' }, never() { return new Promise(() => {}) } }").c_str(), "");
        FunctionReferencesTest::Eval(*pair.caller, "globalThis.out = ''; api.never().catch(e => out = e.message)");
        pair.StopOwner();
        STREQUALS(FunctionReferencesTest::Eval(*pair.caller, "try { api.ping() } catch (e) { e.message }").c_str(), "The function belongs to a resource that has stopped");
        EQUALS(pair.TickUntil("out !== ''"), true);
        STREQUALS(FunctionReferencesTest::Eval(*pair.caller, "out").c_str(), "owner stopped before the call finished");
        EQUALS(pair.references->GetExportCount(), size_t(0));
    });

    IT("releases an export once its stand-in is collected", {
        FunctionReferencesTest::Pair pair;
        STREQUALS(FunctionReferencesTest::Export(pair, "function handler() {}").c_str(), "");
        EQUALS(pair.references->GetExportCount(), size_t(1));
        EQUALS(pair.references->GetStandInCount(), size_t(1));

        FunctionReferencesTest::Eval(*pair.caller, "delete globalThis.api");
        {
            v8::Isolate *isolate = pair.caller->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            isolate->LowMemoryNotification();
        }
        EQUALS(pair.references->GetStandInCount(), size_t(0));
        EQUALS(pair.references->GetExportCount(), size_t(0));
    });

    IT("exports a function sent repeatedly once", {
        FunctionReferencesTest::Pair pair;
        FunctionReferencesTest::Eval(*pair.owner, "globalThis.onTick = () => {}");
        for (int i = 0; i < 3; ++i) {
            STREQUALS(FunctionReferencesTest::Export(pair, "onTick").c_str(), "");
        }
        EQUALS(pair.references->GetExportCount(), size_t(1));
    });
});
