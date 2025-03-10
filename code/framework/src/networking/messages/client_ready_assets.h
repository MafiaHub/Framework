/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages.h"

#include <BitStream.h>
#include <RakString.h>

namespace Framework::Networking::Messages {
    class ClientReadyAssets final: public IMessage {
      private:
        SLNet::RakString _clientEntryPoint;

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_READY_ASSETS;
        }

        void FromParameters(const std::string& clientEntryPoint) {
            _clientEntryPoint = clientEntryPoint.c_str();
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _clientEntryPoint);
        }

        const std::string GetClientEntryPoint() const {
            return _clientEntryPoint.C_String();
        }

        bool Valid() const override {
            return true;
        }
    };
} // namespace Framework::Networking::Messages
