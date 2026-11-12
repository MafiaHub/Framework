/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "value_transfer.h"

#include <v8.h>

#include <cstdint>
#include <memory>
#include <optional>
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
     * stand-ins held. A runtime gets one stand-in per export, so the same function arrives as the same stand-in.
     * A function sent as a property of an object runs with that object, in its owner, as `this`.
     *
     * Calls are made on the scripting thread, or by a thread that has the world in its place: a call nests the owner's
     * isolate inside the caller's, and runtimes are added and removed there too. Copying arguments out of a runtime
     * (CopyArguments(), For()) and letting go of a copy may happen on any thread, at the same time as each other and
     * as a call: a thread with a runtime of its own (see NodeEngine::CreateOffThreadRuntime) does both. The tables are
     * behind a mutex for that, which is never held while script runs or while an isolate is waited for. A function a
     * runtime exported is only ever touched in its isolate: released from elsewhere, its handle waits for the
     * runtime's next export, import or removal.
     */
    class FunctionReferences final {
      public:
        // What a call brings back to the caller.
        enum class Returned : uint8_t {
            // A copy of what the function returned; a promise settles with a copy of its value.
            Value,

            // Only what the caller can act on without the value: a boolean arrives (a handler's veto), anything else
            // arrives as undefined and is never copied, a promise settles with undefined. Throws and rejections still
            // carry their reason. For callers that discard the result, such as event dispatch.
            Outcome,
        };

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
         * Copy call arguments out of `caller`, whose scopes the caller holds, once for any number of calls. On failure
         * an exception naming the argument is pending in the caller and the result is empty.
         */
        std::optional<std::vector<TransferredValue>> CopyArguments(v8::Isolate *caller, const std::vector<v8::Local<v8::Value>> &arguments);
        // The other half, for copies that crossed threads on their own: builds them again in `target`, a runtime
        // this object knows, inside its current context. Empty, with the exception pending there, on failure.
        std::optional<std::vector<v8::Local<v8::Value>>> RebuildArguments(v8::Isolate *target, const std::vector<TransferredValue> &arguments);

        /**
         * Call a function that lives in `owner` from inside `caller`, whose scopes the caller holds. The arguments are
         * copied out of the caller, the function runs inside its owner, and what it returns arrives in the caller as
         * `returned` says. On a throw, on arguments that cannot cross, or when either runtime takes no part in calls,
         * an exception is pending in the caller and the result is empty.
         */
        v8::MaybeLocal<v8::Value> Call(v8::Isolate *caller, v8::Isolate *owner, const v8::Global<v8::Function> &function, const std::vector<v8::Local<v8::Value>> &arguments, Returned returned = Returned::Value);

        // As above, with the arguments already copied out of the caller: one emit to many runtimes copies them once.
        v8::MaybeLocal<v8::Value> Call(v8::Isolate *caller, v8::Isolate *owner, const v8::Global<v8::Function> &function, const std::vector<TransferredValue> &arguments, Returned returned = Returned::Value);

        /**
         * Bring a value that lives in `owner` into `caller`, whose scopes the caller holds: `produce` runs inside the
         * owner and returns the value, which arrives as a copy. On failure, or when either runtime takes no part in
         * calls, an exception is pending in the caller.
         */
        v8::MaybeLocal<v8::Value> Fetch(v8::Isolate *caller, v8::Isolate *owner, fu2::function_view<v8::MaybeLocal<v8::Value>(v8::Isolate *, v8::Local<v8::Context>)> produce);

        /**
         * Release what collected stand-ins held. Garbage collection only marks them, because V8 allows nothing else
         * inside a weak callback; call this regularly, outside any collection, such as once per tick.
         */
        void ReleaseCollected();

        /**
         * Name a function registered as an export, for the resource monitor's slow reports: one with no name of its
         * own, such as an arrow function passed straight to Exports.register(), is reported by this one.
         */
        static void NameExport(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Function> function, const std::string &name);

        // Diagnostics and tests: live exports, and stand-ins still tracked.
        size_t GetExportCount() const;
        size_t GetStandInCount() const;

        struct State;

      private:
        std::shared_ptr<State> _state;
    };

} // namespace Framework::Scripting
