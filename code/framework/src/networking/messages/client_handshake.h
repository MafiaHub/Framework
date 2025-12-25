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

namespace Framework::Networking::Messages {
    class ClientHandshake final: public IMessage {
      private:
        SLNet::RakString _clientVersion   = "";
        SLNet::RakString _fwVersion       = "";
        SLNet::RakString _gameVersion     = "";
        SLNet::RakString _gameName        = "";

      public:
        ClientHandshake() = default;

        ClientHandshake(const std::string &clientVersion, const std::string &fwVersion, const std::string &gameVersion, const std::string &gameName)
            : _clientVersion(clientVersion.c_str())
            , _fwVersion(fwVersion.c_str())
            , _gameVersion(gameVersion.c_str())
            , _gameName(gameName.c_str()) {}

        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_HANDSHAKE;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _fwVersion);
            bs->Serialize(write, _clientVersion);
            bs->Serialize(write, _gameVersion);
            bs->Serialize(write, _gameName);
        }

        bool Valid() const override {
            return _fwVersion.GetLength() > 0 && _clientVersion.GetLength() > 0 && _gameVersion.GetLength() > 0 && _gameName.GetLength() > 0;
        }

        std::string GetFWVersion() const {
            return _fwVersion.C_String();
        }

        std::string GetClientVersion() const {
            return _clientVersion.C_String();
        }

        std::string GetGameVersion() const {
            return _gameVersion.C_String();
        }

        std::string GetGameName() const {
            return _gameName.C_String();
        }
    };
} // namespace Framework::Networking::Messages
