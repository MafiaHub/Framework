/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../script_engine.h"
#include "../transferred_value.h"

#include <function2/function2.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Framework::Scripting {
    class Engine;
    class LuaEngine;
} // namespace Framework::Scripting

namespace Framework::Scripting::Bind {
    // How a call to a script function ended: what it returned, or with `threw` the text of what it threw or raised,
    // with the stack or the traceback where the language gave one.
    struct CallOutcome final {
        bool threw = false;
        std::string error;
        TransferredValue value;
    };

    // Told the outcome of an Invoke, once. It must not throw.
    using Completion = fu2::unique_function<void(CallOutcome &&)>;

    // What an Invoke brings back.
    enum class ReturnMode : uint8_t {
        // A copy of what the function returned, with the functions in it (TransferredValue::callable). A result that
        // cannot cross fails the call: `threw`, with what the transfer said.
        Value,
        // Only what the caller can act on without the value, as FunctionReferences::Returned::Outcome: a boolean
        // arrives (a handler's veto), anything else arrives as undefined and is never copied, so a handler may
        // return what cannot cross. What it threw still arrives.
        Outcome
    };

    // The error of a call to a function whose runtime is gone.
    inline constexpr const char *kRuntimeStopped = "runtime stopped";

    // A script function kept beyond the call that received it (Value::ToCallable): an event handler, a key bind, an
    // exported function. The host invokes it later, whatever language owns it.
    //
    // A callable keeps its function alive in the runtime that owns it, for as long as it exists itself. A function
    // has one callable for as long as anyone holds it, so taking it twice gives the same one, and a function that
    // crossed into another runtime and is taken there is the callable of the original.
    //
    // When its runtime goes the callable is dead: it lets go of the function, IsAlive is false, and every Invoke
    // that had not ended yet, like every one made from then on, is told `threw` and kRuntimeStopped. The runtime
    // says so through its engine, which WatchRuntimes listens to.
    //
    // Everything here runs on the scripting thread.
    class ScriptCallable {
      public:
        virtual ~ScriptCallable() = default;

        virtual Language GetLanguage() const = 0;

        // The resource the function belongs to. In Lua, the one of its runtime. In JavaScript, what the resource
        // manager of the runtime answered when the callable was made (Call::ResourceName), since one isolate may
        // serve many resources: empty when it could not tell.
        virtual std::string ResourceName() const = 0;

        // False once its runtime is gone.
        virtual bool IsAlive() const = 0;

        // Whether `other` is the callable of the same function of the same runtime.
        virtual bool Equals(const ScriptCallable &other) const = 0;

        // Runs the function in its runtime with `args` built there, and tells `done` how that went. `done` runs
        // exactly once, on the scripting thread: before Invoke returns when the function finished at once, later
        // when it returned a promise (JavaScript) or waited (Lua), which is when the promise settles or the
        // coroutine ends. A JavaScript promise that settles as the call leaves its runtime has told `done` by the
        // time Invoke returns.
        //
        // The function is called on nothing: `this` is undefined in JavaScript and Lua gets no receiver, also
        // for a function that was copied as a property of an object. Only the function references of JavaScript
        // (function_references.h) call a method on the object it was sent with.
        //
        // `done` may invoke callables, this one included, and destroy runtimes through their engines. An empty
        // `done` is one that nobody waits on: the function runs all the same.
        virtual void Invoke(const std::vector<TransferredValue> &args, ReturnMode mode, Completion done) = 0;
    };

    namespace Detail {
        // What the copies of a Deferred share: one per language.
        class DeferredState {
          public:
            DeferredState()                                 = default;
            DeferredState(const DeferredState &)            = delete;
            DeferredState &operator=(const DeferredState &) = delete;
            virtual ~DeferredState()                        = default;

            void Resolve(TransferredValue &&value) {
                if (!std::exchange(_settled, true)) {
                    OnResolve(std::move(value));
                }
            }

            void Reject(std::string &&message) {
                if (!std::exchange(_settled, true)) {
                    OnReject(std::move(message));
                }
            }

            bool Settled() const {
                return _settled;
            }

          protected:
            virtual void OnResolve(TransferredValue &&value) = 0;
            virtual void OnReject(std::string &&message)     = 0;

          private:
            bool _settled = false;
        };
    } // namespace Detail

    // The result of a call that is not known yet when the function returns (Call::Defer). A copy is the same
    // deferred. Whoever keeps one settles it later, on the scripting thread: the first Resolve or Reject counts and
    // the others do nothing.
    //
    // JavaScript: the call returned a promise, which Resolve fulfils with `value` built in its runtime and Reject
    // rejects with an Error carrying `message`. Lua: the coroutine that called waits, and goes on with the value as
    // the result of the call, or with `message` raised from it. A value the runtime cannot be given is the error of
    // the call instead. Settled before the function returned, a deferred makes a Lua call return or raise at once,
    // with no wait.
    //
    // A deferred whose runtime is gone, or whose call threw, takes its settlement and does nothing with it.
    class Deferred final {
      public:
        // Of no call: settled, and settling it does nothing.
        Deferred() = default;

        explicit Deferred(std::shared_ptr<Detail::DeferredState> state): _state(std::move(state)) {}

        void Resolve(TransferredValue value = {}) {
            // Held here: what settling runs may drop the last copy.
            if (const std::shared_ptr<Detail::DeferredState> state = _state) {
                state->Resolve(std::move(value));
            }
        }

        void Reject(std::string message) {
            if (const std::shared_ptr<Detail::DeferredState> state = _state) {
                state->Reject(std::move(message));
            }
        }

        bool Settled() const {
            return _state == nullptr || _state->Settled();
        }

      private:
        std::shared_ptr<Detail::DeferredState> _state;
    };

    // Has the callables and the deferred results of the runtimes of `engine` told when one of them is destroyed,
    // through the one disposing listener this adds to the engine. Call it once for each engine, before a script
    // runs in it.
    //
    // Two runtimes need no such call. An isolate whose engine has no listener for it, the engine's own or the one
    // isolate every resource of a shared engine runs in, is spoken for by Builtins::UnregisterAll, which every
    // engine calls before it disposes of an isolate. A Lua state no engine owns says so itself as it closes.
    void WatchRuntimes(Engine &engine);
    void WatchRuntimes(LuaEngine &engine);
} // namespace Framework::Scripting::Bind
