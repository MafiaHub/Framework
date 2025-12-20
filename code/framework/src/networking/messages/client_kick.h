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

#include <flecs/flecs.h>

namespace Framework::Networking::Messages {
    class ClientKick final: public IMessage {
      private:
        DisconnectionReason _reason = DisconnectionReason::UNKNOWN;
        SLNet::RakString _customReason;

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_KICKED;
        }

        void FromParameters(DisconnectionReason reason, const std::string customReason = "") {
            _reason = reason;
            _customReason = customReason.c_str();
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _reason);
            bs->Serialize(write, _customReason);
        }

        bool Valid() const override {
            if (_reason == DisconnectionReason::KICKED_CUSTOM && _customReason.GetLength() == 0) {
                return false;
            }
            return true;
        }

        DisconnectionReason GetDisconnectionReason() const {
            return _reason;
        }

        std::string GetCustomReason() const {
            return std::string(_customReason.C_String());
        }
    };
} // namespace Framework::Networking::Messages
