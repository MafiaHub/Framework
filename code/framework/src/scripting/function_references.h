/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "value_transfer.h"

#include <cerrno>

#include <v8.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Framework::Scripting {

    /**
     * Calls between runtimes.
     *
     * A function copied out of one runtime is exported: kept alive in a table under the runtime that owns it. What
     * arrives in the other runtime is a stand-in that, when called, enters the owner, rebuilds the arguments there,
     * calls the function and copies the result back. A thrown value is rethrown on the caller's side; a returned promise
     * becomes a promise on the caller's side that settles when the original does.
     *
     * An export lives while a copy of it exists or a stand-in for it is reachable. Removing a runtime drops what it
     * exported, so its stand-ins elsewhere throw instead of calling into a dead isolate, and releases what its own
     * stand-ins held.
     *
     * Every runtime is entered from the one scripting thread, so a call nests the owner's isolate inside the caller's.
     */
    class FunctionReferences final {
      public:
        FunctionReferences();
        ~FunctionReferences();

        FunctionReferences(const FunctionReferences &)            = delete;
        FunctionReferences &operator=(const FunctionReferences &) = delete;

        /**
         * Take part in calls. `name` is used in errors, such as the one a stand-in throws once its owner is gone. The
         * caller holds the isolate's scopes.
         * @return The id the runtime is known by here.
         */
        uint32_t AddRuntime(v8::Isolate *isolate, v8::Local<v8::Context> context, std::string name);

        /**
         * Leave: drop everything the runtime exported and release what its stand-ins held. Call while the isolate is
         * still alive, before it is disposed.
         */
        void RemoveRuntime(uint32_t runtime);

        /**
         * The hooks that make functions cross for one runtime, on whichever side of a copy it is: functions copied out
         * of it are exported from it, and functions rebuilt in it arrive as stand-ins (or as themselves, when they came
         * from it in the first place).
         */
        TransferFunctions For(uint32_t runtime) const;

        /**
         * The runtime an isolate belongs to, or 0 when it takes no part in calls.
         */
        uint32_t FindRuntime(v8::Isolate *isolate) const;

        /**
         * Call a function that lives in `owner` from inside `caller`, whose scopes the caller holds. The arguments are
         * copied out of the caller, the function runs inside its owner, and what it returns arrives in the caller: a
         * promise becomes a promise there that settles with the owner's. On a throw, or on arguments that cannot cross,
         * an exception is pending in the caller and the result is empty.
         */
        v8::MaybeLocal<v8::Value> Call(uint32_t caller, uint32_t owner, const v8::Global<v8::Function> &function, const std::vector<v8::Local<v8::Value>> &arguments);

        // As above, with the arguments already copied out of the caller: one emit to many runtimes copies them once.
        v8::MaybeLocal<v8::Value> Call(uint32_t caller, uint32_t owner, const v8::Global<v8::Function> &function, const std::vector<TransferredValue> &arguments);

        /**
         * Bring a value that lives in `owner` into `caller`, whose scopes the caller holds: `produce` runs inside the
         * owner and returns the value, which arrives as a copy. On failure an exception is pending in the caller.
         */
        v8::MaybeLocal<v8::Value> Fetch(uint32_t caller, uint32_t owner, fu2::function_view<v8::MaybeLocal<v8::Value>(v8::Isolate *, v8::Local<v8::Context>)> produce);

        // As above, naming the runtimes by isolate. Throws in the caller when either takes no part in calls.
        v8::MaybeLocal<v8::Value> Fetch(v8::Isolate *caller, v8::Isolate *owner, fu2::function_view<v8::MaybeLocal<v8::Value>(v8::Isolate *, v8::Local<v8::Context>)> produce);

        /**
         * Enter a runtime for the lifetime of the object: the lock, the isolate, a handle scope and its context.
         */
        class Scope final {
          public:
            Scope(const FunctionReferences &references, uint32_t runtime);

            v8::Isolate *GetIsolate() const {
                return _isolate;
            }

            v8::Local<v8::Context> GetContext() const {
                return _context;
            }

          private:
            v8::Isolate *_isolate;
            v8::Locker _locker;
            v8::Isolate::Scope _isolateScope;
            v8::HandleScope _handleScope;
            v8::Local<v8::Context> _context;
            v8::Context::Scope _contextScope;
        };

        // Diagnostics and tests: live exports, and stand-ins still tracked.
        size_t GetExportCount() const;
        size_t GetStandInCount() const;

        struct State;

      private:
        std::shared_ptr<State> _state;
    };

} // namespace Framework::Scripting
