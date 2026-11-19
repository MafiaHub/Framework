/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/rpc/client_identity.h>

#include <v8.h>

#include <cstdint>
#include <string>

namespace Framework::Integrations::Server::Scripting::Builtins {

    // The connection handed to playerConnecting handlers: who is asking to join, and the calls
    // that answer them. A plain object rather than a Player: there is no body, no entity and no
    // network id yet, and there may never be one.
    //
    // Its methods reach the ConnectionGate through CoreModules and address the connection by its
    // guid, so an object kept past its decision (or its player leaving) simply stops doing
    // anything, as a stale handle does everywhere else.
    class PendingConnection final {
      public:
        // Declares the type and the playerConnecting event in the catalog; installs no global.
        static void Register(v8::Isolate *isolate);

        static v8::Local<v8::Object> Create(v8::Isolate *isolate, v8::Local<v8::Context> context, uint64_t guid, const Framework::Networking::RPC::ClientIdentity &identity, const std::string &address);

      private:
        static void JS_Reject(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Update(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsPending(const v8::FunctionCallbackInfo<v8::Value> &info);
    };

} // namespace Framework::Integrations::Server::Scripting::Builtins
