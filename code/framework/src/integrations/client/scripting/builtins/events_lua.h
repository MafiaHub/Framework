/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <sol/sol.hpp>

#include "networking/network_client.h"

#include "integrations/shared/rpc/emit_lua_event.h"

#include "scripting/utils/table_conversions.h"

#include "core_modules.h"

namespace Framework::Integrations::Scripting {
    class EventsClient {
      private:
        static void EmitEvent(std::string eventName, sol::object payload) {
            Framework::Integrations::Shared::RPC::EmitLuaEvent rpc;
            try {
                nlohmann::json jsonPayload = Framework::Scripting::Utils::SolToJson(payload);
                rpc.FromParameters(eventName, jsonPayload.dump());
                CoreModules::GetNetworkPeer()->SendRPC(rpc);
            }
            catch (const std::exception &e) {
                throw std::runtime_error(fmt::format("Error in EventsClient::EmitEvent: {}", e.what()));
            }
        }

        static void On(const std::string name, const sol::function fnc) {
            Framework::CoreModules::GetScriptingEngine()->ListenRemoteEvent(name, fnc);
        }

      public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<EventsClient> cls = luaEngine->new_usertype<EventsClient>("Client");

            cls["emit"] = &EventsClient::EmitEvent;
            cls["on"]   = &EventsClient::On;
        }
    };
} // namespace Framework::Integrations::Scripting
