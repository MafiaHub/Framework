/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "callable.h"

#include "../value_transfer.h"

#include <v8.h>

#include <memory>

// The JavaScript side of ScriptCallable and Deferred (callable.h).
//
// What is kept for an isolate, its callables, the calls that wait on a promise and the deferred results, is dropped
// when the isolate goes: V8RuntimeDisposing, which the listener of WatchRuntimes and Builtins::UnregisterAll call.
namespace Framework::Scripting::Bind {
    // The callable of a JavaScript function: every callable whose language is JavaScript is one. For a caller that
    // runs in the same isolate and calls the function there, by reference and with no copy.
    class V8Callable: public ScriptCallable {
      public:
        // nullptr once the isolate is gone.
        virtual v8::Isolate *Isolate() const = 0;

        // The function, for a caller that holds the scopes of Isolate(). Empty once the isolate is gone.
        virtual v8::Local<v8::Function> Function() const = 0;
    };

    // The callable of `function`: the one it has while anyone holds one, a new one otherwise, and for a function
    // that stands for a callable of another runtime, that callable. `context` is the context it is called in. The
    // caller holds the scopes of the isolate.
    std::shared_ptr<ScriptCallable> V8ToCallable(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Function> function);

    // A function of the isolate that invokes `callable` (Call::FromTransfer says how), or the function itself
    // when the isolate owns it. Empty where V8 cannot make a function, as in an isolate that is being terminated.
    // Call with `context` entered.
    v8::MaybeLocal<v8::Function> V8FunctionOf(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::shared_ptr<ScriptCallable> &callable);

    // What ValueTransfer::Copy is given to copy a function as its callable. It imports nothing: a function is
    // rebuilt from the callable it carries.
    const TransferFunctions &V8CallableTransfer();

    // Marks dead what is kept for `isolate`: its callables let go of their functions, the calls that wait on a
    // promise of it are told kRuntimeStopped, its deferred results let go of their promises. Call it while the
    // isolate is alive, before it is disposed of; a second call does nothing.
    void V8RuntimeDisposing(v8::Isolate *isolate);

    namespace Detail {
        // The deferred result of a JavaScript call: what settles the promise the script is given.
        class V8Deferred: public DeferredState {
          public:
            // The function has returned, and the script is about to get the promise: one that was settled
            // meanwhile is settled now, in the call itself. Call with `context` entered.
            virtual void Returned(v8::Isolate *isolate, v8::Local<v8::Context> context) = 0;

            // The call threw: the script never sees the promise, and nothing is to settle it.
            virtual void Drop() = 0;
        };

        // Makes the deferred result of a call and gives its promise, for the call to return. nullptr where V8
        // cannot make a promise, as in an isolate that is being terminated. Call with `context` entered.
        std::shared_ptr<V8Deferred> V8Defer(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Promise> &promise);
    } // namespace Detail
} // namespace Framework::Scripting::Bind
