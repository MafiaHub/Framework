/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "events_lua.h"

#include "networking/network_server.h"

#include "integrations/shared/rpc/emit_lua_event.h"

#include "scripting/utils/table_conversions.h"
#include "scripting/resource/resource_manager.h"

#include "core_modules.h"

namespace Framework::Integrations::Scripting {
    void EventsServer::EmitEvent(std::string eventName, sol::object payload) {
        Framework::Integrations::Shared::RPC::EmitLuaEvent rpc;
        try {
            nlohmann::json jsonPayload = Framework::Scripting::Utils::SolToJson(payload);
            rpc.FromParameters(eventName, jsonPayload.dump());
            CoreModules::GetNetworkPeer()->SendRPC(rpc);
        }
        catch (const std::exception &e) {
            throw std::runtime_error(fmt::format("Error in EventsServer::EmitEvent: {}", e.what()));
        }
    }

    void EventsServer::On(const std::string name, const sol::protected_function fnc) {
        const auto resourceManager = Framework::CoreModules::GetResourceManager();
        if (resourceManager) {
            resourceManager->RegisterGlobalEventHandler(name, fnc);
        }
    }

    void EventsServer::Register(sol::state *luaEngine) {
        sol::usertype<EventsServer> cls = luaEngine->new_usertype<EventsServer>("Server");

        cls["emit"] = &EventsServer::EmitEvent;
        cls["on"]   = &EventsServer::On;
    }
} // namespace Framework::Integrations::Scripting
