/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages.h"

#include <BitStream.h>

#include <flecs/flecs.h>

namespace Framework::Networking::Messages {
    class ClientKick final: public IMessage {
      private:
        DisconnectionReason _reason;

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_KICKED;
        }

        void FromParameters(DisconnectionReason reason) {
            _reason = reason;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _reason);
        }

        bool Valid() override {
            return true;
        }

        DisconnectionReason GetDisconnectionReason() const {
            return _reason;
        }
    };
} // namespace Framework::Networking::Messages
