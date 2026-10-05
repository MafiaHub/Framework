/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/builtins/builtins.h"
#include "scripting/node_engine.h"
#include "scripting/node_runtime.h"
#include "scripting/value_transfer.h"

#include <memory>
#include <string>

namespace ValueTransferTest {
    // Two runtimes with the builtin value types registered, as two resources would have.
    struct Pair {
        Framework::Scripting::NodeEngine engine;
        std::unique_ptr<Framework::Scripting::NodeRuntime> source;
        std::unique_ptr<Framework::Scripting::NodeRuntime> target;

        Pair() {
            engine.Init();
            Framework::Scripting::Builtins::RegisterTransferTypes();
            std::string error;
            source = engine.CreateRuntime(error);
            target = engine.CreateRuntime(error);
            for (auto *runtime : {source.get(), target.get()}) {
                v8::Isolate *isolate = runtime->GetIsolate();
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                v8::HandleScope handleScope(isolate);
                v8::Local<v8::Context> context = runtime->GetContext();
                v8::Context::Scope contextScope(context);
                Framework::Scripting::Builtins::RegisterValueTypes(isolate, context->Global());
            }
        }

        ~Pair() {
            // The builtin class caches are keyed by isolate address, which a later isolate may reuse.
            for (auto *runtime : {source.get(), target.get()}) {
                Framework::Scripting::Builtins::UnregisterAll(runtime->GetIsolate());
            }
            source.reset();
            target.reset();
            engine.Shutdown();
        }
    };

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

    // Copies `expression` out of the source runtime and stores what arrives as globalThis.received in the target.
    // Returns the copy's error, or "" when it crossed.
    static std::string Send(Pair &pair, const std::string &expression, const Framework::Scripting::TransferFunctions &functions = {}) {
        Framework::Scripting::TransferredValue copied;
        {
            v8::Isolate *isolate = pair.source->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = pair.source->GetContext();
            v8::Context::Scope contextScope(context);

            v8::Local<v8::Value> value = v8::Script::Compile(context, v8::String::NewFromUtf8(isolate, ("(" + expression + ")").c_str()).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();
            auto result = Framework::Scripting::ValueTransfer::Copy(isolate, context, value, functions);
            if (!result) {
                return result.GetError();
            }
            copied = result.GetValue();
        }

        v8::Isolate *isolate = pair.target->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = pair.target->GetContext();
        v8::Context::Scope contextScope(context);
        v8::TryCatch tryCatch(isolate);

        v8::Local<v8::Value> rebuilt;
        if (!Framework::Scripting::ValueTransfer::Rebuild(isolate, context, copied, functions).ToLocal(&rebuilt)) {
            v8::String::Utf8Value text(isolate, tryCatch.Exception());
            return std::string("rebuild threw: ") + (*text ? *text : "");
        }
        context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "received"), rebuilt).Check();
        return "";
    }
} // namespace ValueTransferTest

MODULE(value_transfer, {
    using namespace Framework::Scripting;

    IT("copies primitives, arrays and plain objects", {
        ValueTransferTest::Pair pair;
        STREQUALS(ValueTransferTest::Send(pair, "{ n: 1.5, s: 'text', b: true, z: null, u: undefined, big: 10n ** 20n, list: [1, [2, 'three']] }").c_str(), "");
        STREQUALS(ValueTransferTest::Eval(*pair.target, "JSON.stringify({ ...received, big: String(received.big) })").c_str(), "{\"n\":1.5,\"s\":\"text\",\"b\":true,\"z\":null,\"big\":\"100000000000000000000\",\"list\":[1,[2,\"three\"]]}");
        STREQUALS(ValueTransferTest::Eval(*pair.target, "'u' in received && received.u === undefined && typeof received.big").c_str(), "bigint");
    });

    IT("copies dates, regular expressions, maps and sets", {
        ValueTransferTest::Pair pair;
        STREQUALS(ValueTransferTest::Send(pair, "{ when: new Date(86400000), re: /ab+c/gi, map: new Map([['k', { v: 1 }]]), set: new Set([1, 'two']) }").c_str(), "");
        STREQUALS(ValueTransferTest::Eval(*pair.target, "[received.when instanceof Date, received.when.getTime(), received.re.source, received.re.flags, received.map.get('k').v, [...received.set].join()].join('|')").c_str(), "true|86400000|ab+c|gi|1|1,two");
    });

    IT("copies binary data and keeps a Buffer a Buffer", {
        ValueTransferTest::Pair pair;
        STREQUALS(ValueTransferTest::Send(pair, "{ raw: new Uint8Array([1, 2, 3]).buffer, floats: new Float64Array([0.5, 2]), view: new Uint8Array([9, 8, 7]).subarray(1), buf: Buffer.from('hi') }").c_str(), "");
        STREQUALS(ValueTransferTest::Eval(*pair.target, "[received.raw.byteLength, received.floats instanceof Float64Array, received.floats[1], [...received.view].join(), Buffer.isBuffer(received.buf), received.buf.toString()].join('|')").c_str(), "3|true|2|8,7|true|hi");
    });

    IT("copies an error with its name, message, stack and own fields", {
        ValueTransferTest::Pair pair;
        STREQUALS(ValueTransferTest::Send(pair, "Object.assign(new TypeError('bad input'), { code: 'E_BAD' })").c_str(), "");
        STREQUALS(ValueTransferTest::Eval(*pair.target, "[received instanceof Error, received.name, received.message, received.code, received.stack.includes('bad input')].join('|')").c_str(), "true|TypeError|bad input|E_BAD|true");
    });

    IT("rebuilds framework value types as themselves", {
        ValueTransferTest::Pair pair;
        STREQUALS(ValueTransferTest::Send(pair, "{ at: new Vector3(1, 2, 3), turn: new Quaternion(1, 0, 0, 0) }").c_str(), "");
        STREQUALS(ValueTransferTest::Eval(*pair.target, "[received.at instanceof Vector3, received.at.x, received.at.y, received.at.z, received.turn instanceof Quaternion].join('|')").c_str(), "true|1|2|3|true");
    });

    IT("turns a class instance into a plain object", {
        ValueTransferTest::Pair pair;
        STREQUALS(ValueTransferTest::Send(pair, "new (class Account { constructor() { this.balance = 10; } deposit() {} })()").c_str(), "");
        STREQUALS(ValueTransferTest::Eval(*pair.target, "[Object.getPrototypeOf(received) === Object.prototype, received.balance, typeof received.deposit].join('|')").c_str(), "true|10|undefined");
    });

    IT("sends a copy, not the object", {
        ValueTransferTest::Pair pair;
        ValueTransferTest::Eval(*pair.source, "globalThis.state = { count: 1 }");
        STREQUALS(ValueTransferTest::Send(pair, "state").c_str(), "");
        ValueTransferTest::Eval(*pair.target, "received.count = 99");
        STREQUALS(ValueTransferTest::Eval(*pair.source, "state.count").c_str(), "1");
    });

    IT("refuses what cannot cross and says where", {
        ValueTransferTest::Pair pair;
        STREQUALS(ValueTransferTest::Send(pair, "(() => { const loop = { inner: {} }; loop.inner.back = loop; return loop; })()").c_str(), "Cannot copy value.inner.back: it refers back to itself");
        STREQUALS(ValueTransferTest::Send(pair, "{ list: [1, { onDone() {} }] }").c_str(), "Cannot copy value.list[1].onDone: a function cannot be copied here");
        STREQUALS(ValueTransferTest::Send(pair, "{ tag: Symbol('x') }").c_str(), "Cannot copy value.tag: a symbol cannot be copied");
        STREQUALS(ValueTransferTest::Send(pair, "{ later: Promise.resolve(1) }").c_str(), "Cannot copy value.later: a promise cannot be copied");
        STREQUALS(ValueTransferTest::Send(pair, "{ counters: new Int32Array(new SharedArrayBuffer(4)) }").c_str(), "Cannot copy value.counters: shared memory cannot cross between resources");
    });

    IT("hands functions to the export and import hooks", {
        ValueTransferTest::Pair pair;
        TransferFunctions functions;
        functions.exportFunction = [](v8::Isolate *, v8::Local<v8::Function>, TransferredValue &out) { out.reference = 7; };
        functions.importFunction = [](v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t reference) -> v8::MaybeLocal<v8::Function> {
            return v8::Function::New(context, [](const v8::FunctionCallbackInfo<v8::Value> &info) { info.GetReturnValue().Set(info.Data()); }, v8::Number::New(isolate, static_cast<double>(reference)));
        };
        STREQUALS(ValueTransferTest::Send(pair, "{ callback() {} }", functions).c_str(), "");
        STREQUALS(ValueTransferTest::Eval(*pair.target, "received.callback()").c_str(), "7");
    });
});
