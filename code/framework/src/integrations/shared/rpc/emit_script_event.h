/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/BitStream.h>
#include <mafianet/string.h>

#include <string>

namespace Framework::Integrations::Shared::RPC {
    // RPC payload forwarding a named scripting event (with a JSON payload) to the other peer's
    // resource layer. See networking/rpc/rpc.h for the dispatch model.
    struct EmitScriptEvent {
        static constexpr const char *kIdentifier = "Framework::EmitScriptEvent";

        MafiaNet::RakString eventName;
        MafiaNet::RakString payload;

        void FromParameters(const std::string &name, const std::string &payloadJson) {
            eventName = name.c_str();
            payload   = payloadJson.c_str();
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, eventName);
            bs->Serialize(write, payload);
        }

        std::string GetEventName() const {
            return eventName.C_String();
        }

        std::string GetPayload() const {
            return payload.C_String();
        }
    };
} // namespace Framework::Integrations::Shared::RPC
