/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "callback_scope.h"
#include "engine.h"
#include "script_gate.h"

#include <v8.h>

#include <memory>
#include <optional>

namespace Framework::Scripting {

    // Enters an engine's isolate and context for its lifetime: locker, isolate scope, handle scope,
    // context scope, in the order V8 requires, then the runtime's callback scope, so what the host
    // runs inside queues runs as the scope closes. Valid() is false, and nothing is entered, for a
    // null engine or one with no isolate yet; whether it must also be initialized is the caller's call.
    //
    // On a server, scripts run on a thread of their own, and off that thread engine->GetIsolate() is an isolate
    // of the calling side's own: a scope opened on the tick thread waits for no script. Values built in it are
    // copied to the script thread by whatever is given them (an emitted event, say).
    class EngineScope final {
      public:
        explicit EngineScope(Engine *engine) {
            if (engine == nullptr || engine->GetIsolate() == nullptr) {
                return;
            }
            _isolate = engine->GetIsolate();
            _locker.emplace(_isolate);
            _isolateScope.emplace(_isolate);
            _handleScope.emplace(_isolate);
            _context = engine->GetContext();
            _contextScope.emplace(_context);
            _callbackScope = OpenCallbackScope(_isolate);
        }
        EngineScope(const EngineScope &)            = delete;
        EngineScope &operator=(const EngineScope &) = delete;

        bool Valid() const {
            return _contextScope.has_value();
        }

        v8::Isolate *Isolate() const {
            return _isolate;
        }

        v8::Local<v8::Context> Context() const {
            return _context;
        }

      private:
        // First in, last out: nothing of V8 is entered before it or left after it.
        RuntimeEntry _entry {RuntimeEntry::Into::OwnIsolate};
        v8::Isolate *_isolate = nullptr;
        // Declaration order is construction order; the optionals unwind in reverse, as V8 requires.
        std::optional<v8::Locker> _locker;
        std::optional<v8::Isolate::Scope> _isolateScope;
        std::optional<v8::HandleScope> _handleScope;
        v8::Local<v8::Context> _context;
        std::optional<v8::Context::Scope> _contextScope;
        std::unique_ptr<CallbackScope> _callbackScope;
    };

} // namespace Framework::Scripting
