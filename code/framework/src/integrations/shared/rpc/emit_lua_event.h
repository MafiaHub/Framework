/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/rpc/rpc.h>

#include <string>

namespace Framework::Integrations::Shared::RPC {
    class EmitLuaEvent final: public Framework::Networking::RPC::IRPC<EmitLuaEvent> {
      private:
        MafiaNet::RakString _eventName;
        MafiaNet::RakString _payload;

      public:
        void FromParameters(const std::string &name, const std::string &payload) {
            _eventName = name.c_str();
            _payload = payload.c_str();
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _eventName);
            bs->Serialize(write, _payload);
        }

        bool Valid() const override {
            return !_eventName.IsEmpty();
        }

        std::string GetEventName() const {
            return _eventName.C_String();
        }

        std::string GetPayload() const {
            return _payload.C_String();
        }
    };
}
