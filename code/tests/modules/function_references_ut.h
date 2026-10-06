/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/engine_helpers.h"
#include "scripting/function_references.h"
#include "scripting/node_engine.h"
#include "scripting/node_runtime.h"
#include "scripting/value_transfer.h"

#include "node_test_helpers.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace FunctionReferencesTest {
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
                if (NodeTest::Eval(*caller, condition) == "true") {
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

    // Installs a native function as globalThis[name] in the runtime, with `data` behind it.
    static void Install(Framework::Scripting::NodeRuntime &runtime, const char *name, v8::FunctionCallback callback, void *data) {
        v8::Isolate *isolate = runtime.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = runtime.GetContext();
        v8::Context::Scope contextScope(context);
        v8::Local<v8::Function> function = v8::Function::New(context, callback, v8::External::New(isolate, data)).ToLocalChecked();
        context->Global()->Set(context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), function).Check();
    }

    // Calls the owner's globalThis.fns[name] from the caller with no arguments, keeps the result as globalThis.out in
    // the caller and returns it as text, or "<threw> " and the exception.
    static std::string CallOwner(Pair &pair, const char *name, Framework::Scripting::FunctionReferences::Returned returned) {
        v8::Global<v8::Function> function;
        {
            v8::Isolate *isolate = pair.owner->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = pair.owner->GetContext();
            v8::Context::Scope contextScope(context);
            v8::Local<v8::Value> fns = context->Global()->Get(context, v8::String::NewFromUtf8Literal(isolate, "fns")).ToLocalChecked();
            function.Reset(isolate, fns.As<v8::Object>()->Get(context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked()).ToLocalChecked().As<v8::Function>());
        }

        v8::Isolate *isolate = pair.caller->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = pair.caller->GetContext();
        v8::Context::Scope contextScope(context);
        v8::TryCatch tryCatch(isolate);
        v8::Local<v8::Value> result;
        if (!pair.references->Call(isolate, pair.owner->GetIsolate(), function, std::vector<v8::Local<v8::Value>> {}, returned).ToLocal(&result)) {
            return "<threw> " + Framework::Scripting::ToUtf8(isolate, tryCatch.Exception());
        }
        context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "out"), result).Check();
        return Framework::Scripting::ToUtf8(isolate, result);
    }
} // namespace FunctionReferencesTest

MODULE(function_references, {
    using namespace Framework::Scripting;

    IT("calls a function in the runtime that owns it", {
        FunctionReferencesTest::Pair pair;
        NodeTest::Eval(*pair.owner, "globalThis.secret = 40");
        STREQUALS(FunctionReferencesTest::Export(pair, "{ add(n) { return { total: secret + n, where: typeof secret } } }").c_str(), "");
        STREQUALS(NodeTest::Eval(*pair.caller, "const r = api.add(2); [r.total, r.where, typeof secret].join('|')").c_str(), "42|number|undefined");
    });

    IT("passes callbacks both ways and brings a function home as itself", {
        FunctionReferencesTest::Pair pair;
        NodeTest::Eval(*pair.owner, "globalThis.marker = function marker() {}");
        STREQUALS(FunctionReferencesTest::Export(pair, "{ each(list, visit) { return list.map(visit) }, marker, isMarker(fn) { return fn === marker } }").c_str(), "");
        STREQUALS(NodeTest::Eval(*pair.caller, "api.each([1, 2], n => n * 10).join() + '|' + api.isMarker(api.marker)").c_str(), "10,20|true");
    });

    IT("rethrows on the caller's side", {
        FunctionReferencesTest::Pair pair;
        STREQUALS(FunctionReferencesTest::Export(pair, "{ fail() { throw new RangeError('out of stock') } }").c_str(), "");
        STREQUALS(NodeTest::Eval(*pair.caller, "try { api.fail(); 'no' } catch (e) { [e instanceof Error, e.name, e.message].join('|') }").c_str(), "true|RangeError|out of stock");
    });

    IT("settles a returned promise on the caller's side", {
        FunctionReferencesTest::Pair pair;
        STREQUALS(FunctionReferencesTest::Export(pair, "{ async load(id) { await null; return { id, name: 'sword' } }, async refuse() { throw new Error('locked') } }").c_str(), "");
        NodeTest::Eval(*pair.caller, "globalThis.out = []; api.load(7).then(v => out.push(v.id + ':' + v.name)); api.refuse().catch(e => out.push(e.message))");
        EQUALS(pair.TickUntil("out.length === 2"), true);
        STREQUALS(NodeTest::Eval(*pair.caller, "out.sort().join('|')").c_str(), "7:sword|locked");
    });

    IT("throws once the owner has stopped, and rejects what it still owed", {
        FunctionReferencesTest::Pair pair;
        STREQUALS(FunctionReferencesTest::Export(pair, "{ ping() { return 'pong' }, never() { return new Promise(() => {}) } }").c_str(), "");
        NodeTest::Eval(*pair.caller, "globalThis.out = ''; api.never().catch(e => out = e.message)");
        pair.StopOwner();
        STREQUALS(NodeTest::Eval(*pair.caller, "try { api.ping() } catch (e) { e.message }").c_str(), "The function belongs to a resource that has stopped");
        EQUALS(pair.TickUntil("out !== ''"), true);
        STREQUALS(NodeTest::Eval(*pair.caller, "out").c_str(), "owner stopped before the call finished");
        EQUALS(pair.references->GetExportCount(), size_t(0));
    });

    IT("releases an export once its stand-in is collected", {
        FunctionReferencesTest::Pair pair;
        STREQUALS(FunctionReferencesTest::Export(pair, "function handler() {}").c_str(), "");
        EQUALS(pair.references->GetExportCount(), size_t(1));
        EQUALS(pair.references->GetStandInCount(), size_t(1));

        NodeTest::Eval(*pair.caller, "delete globalThis.api");
        {
            v8::Isolate *isolate = pair.caller->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            isolate->LowMemoryNotification();
        }
        // Collection only marks the stand-in: V8 allows no other work inside a weak callback.
        EQUALS(pair.references->GetStandInCount(), size_t(1));
        EQUALS(pair.references->GetExportCount(), size_t(1));

        pair.references->ReleaseCollected();
        EQUALS(pair.references->GetStandInCount(), size_t(0));
        EQUALS(pair.references->GetExportCount(), size_t(0));
    });

    IT("exports a function sent repeatedly once", {
        FunctionReferencesTest::Pair pair;
        NodeTest::Eval(*pair.owner, "globalThis.onTick = () => {}");
        for (int i = 0; i < 3; ++i) {
            STREQUALS(FunctionReferencesTest::Export(pair, "onTick").c_str(), "");
        }
        EQUALS(pair.references->GetExportCount(), size_t(1));
    });
    IT("hands a runtime the same stand-in each time it receives the same function", {
        FunctionReferencesTest::Pair pair;
        NodeTest::Eval(*pair.owner, "globalThis.onTick = () => {}");
        STREQUALS(FunctionReferencesTest::Export(pair, "onTick").c_str(), "");
        STREQUALS(FunctionReferencesTest::Send(pair, *pair.owner, pair.ownerId, *pair.caller, pair.callerId, "onTick", "again").c_str(), "");
        STREQUALS(NodeTest::Eval(*pair.caller, "api === again").c_str(), "true");
        EQUALS(pair.references->GetStandInCount(), size_t(1));
    });

    IT("throws instead of calling a stopped owner when copying the arguments stopped it", {
        FunctionReferencesTest::Pair pair;
        STREQUALS(FunctionReferencesTest::Export(pair, "{ ping() { return 'pong' } }").c_str(), "");
        FunctionReferencesTest::Install(*pair.caller, "stopOwner", [](const v8::FunctionCallbackInfo<v8::Value> &info) { static_cast<FunctionReferencesTest::Pair *>(info.Data().As<v8::External>()->Value())->StopOwner(); }, &pair);
        // The argument's getter runs while the call copies it, and frees the owner's runtime under the call.
        STREQUALS(NodeTest::Eval(*pair.caller, "try { api.ping({ get trigger() { stopOwner(); return 1; } }) } catch (e) { e.message }").c_str(), "The function belongs to a resource that has stopped");
        EQUALS(pair.references->GetExportCount(), size_t(0));
    });

    IT("brings back only the outcome when the caller wants no value", {
        using Returned = FunctionReferences::Returned;
        FunctionReferencesTest::Pair pair;
        NodeTest::Eval(*pair.owner, "globalThis.fns = { timer: () => setTimeout(() => {}, 60000), veto: () => false, fail: () => { throw new Error('refused') }, later: async () => { const loop = {}; loop.self = loop; return loop; } }");

        // A Node timer links back to itself: as a value it cannot cross, as an outcome it is never copied.
        const std::string asValue = FunctionReferencesTest::CallOwner(pair, "timer", Returned::Value);
        EQUALS(asValue.find("refers back to itself") != std::string::npos, true);
        STREQUALS(FunctionReferencesTest::CallOwner(pair, "timer", Returned::Outcome).c_str(), "undefined");

        STREQUALS(FunctionReferencesTest::CallOwner(pair, "veto", Returned::Outcome).c_str(), "false");
        STREQUALS(FunctionReferencesTest::CallOwner(pair, "fail", Returned::Outcome).c_str(), "<threw> Error: refused");

        STREQUALS(FunctionReferencesTest::CallOwner(pair, "later", Returned::Outcome).c_str(), "[object Promise]");
        NodeTest::Eval(*pair.caller, "globalThis.settled = ''; out.then(v => settled = 'resolved:' + v, e => settled = 'rejected:' + e.message)");
        EQUALS(pair.TickUntil("settled !== ''"), true);
        STREQUALS(NodeTest::Eval(*pair.caller, "settled").c_str(), "resolved:undefined");
    });

    IT("throws in the caller when the owner is terminated during the call", {
        FunctionReferencesTest::Pair pair;
        FunctionReferencesTest::Install(*pair.owner, "halt", [](const v8::FunctionCallbackInfo<v8::Value> &info) { info.GetIsolate()->TerminateExecution(); }, nullptr);
        // V8 acts on a termination at its next stack check, a function entry: the call after halt() is where it lands, as
        // the call after process.reallyExit() is in Node.
        STREQUALS(FunctionReferencesTest::Export(pair, "{ run() { halt(); (() => {})(); return 'still running'; } }").c_str(), "");
        STREQUALS(NodeTest::Eval(*pair.caller, "try { api.run(); 'returned' } catch (e) { e.message }").c_str(), "owner stopped during the call");
        // Once the termination has unwound, the owner runs again.
        STREQUALS(NodeTest::Eval(*pair.owner, "1 + 1").c_str(), "2");
    });
});
