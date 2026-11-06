/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>

#include <function2/function2.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Framework::Scripting {

    /**
     * What a call into a runtime from outside its JavaScript runs inside.
     *
     * Node runs a runtime's process.nextTick queue, its microtasks and its unhandled rejections when the outermost
     * scope on it closes, as it does after its own callbacks, so what a call made inside one queues runs as the call
     * returns, before the host does anything else. A scope opened inside another on the same runtime leaves that to
     * the outer one, so the entries that nest (a handler emitting to its own runtime, a builtin entering the engine
     * from inside a script) drain once, when the outermost returns. A bare V8 runtime has no such queue, and no scope.
     */
    class CallbackScope {
      public:
        virtual ~CallbackScope() = default;

        CallbackScope(const CallbackScope &)            = delete;
        CallbackScope &operator=(const CallbackScope &) = delete;

      protected:
        CallbackScope() = default;
    };

    using CallbackScopeOpener = fu2::function<std::unique_ptr<CallbackScope>() const>;

    namespace detail {
        inline std::mutex &CallbackScopeMutex() {
            static std::mutex mutex;
            return mutex;
        }

        inline std::unordered_map<v8::Isolate *, CallbackScopeOpener> &CallbackScopeOpeners() {
            static std::unordered_map<v8::Isolate *, CallbackScopeOpener> openers;
            return openers;
        }
    } // namespace detail

    /**
     * How to open a scope on an isolate's runtime. The engine registers it before the runtime runs anything on the
     * host's behalf and removes it before the isolate is disposed.
     */
    inline void RegisterCallbackScope(v8::Isolate *isolate, CallbackScopeOpener opener) {
        std::scoped_lock lock(detail::CallbackScopeMutex());
        detail::CallbackScopeOpeners()[isolate] = std::move(opener);
    }

    inline void UnregisterCallbackScope(v8::Isolate *isolate) {
        std::scoped_lock lock(detail::CallbackScopeMutex());
        detail::CallbackScopeOpeners().erase(isolate);
    }

    /**
     * Open the scope registered for the isolate, or nothing for a runtime without one. The caller holds the locker and
     * the isolate, handle and context scopes, and keeps the result until it leaves them: closing it runs script.
     */
    inline std::unique_ptr<CallbackScope> OpenCallbackScope(v8::Isolate *isolate) {
        CallbackScopeOpener opener;
        {
            std::scoped_lock lock(detail::CallbackScopeMutex());
            const auto &openers = detail::CallbackScopeOpeners();
            const auto it       = openers.find(isolate);
            if (it == openers.end()) {
                return nullptr;
            }
            opener = it->second;
        }
        return opener();
    }

} // namespace Framework::Scripting
