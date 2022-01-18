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

namespace Framework::Networking::Messages {
    class ClientHandshake final: public IMessage {
      private:
        SLNet::RakString _playerName      = "";
        SLNet::RakString _playerSteamId   = "";
        SLNet::RakString _playerDiscordId = "";
        SLNet::RakString _clientVersion   = "";

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_HANDSHAKE;
        }

        void FromParameters(std::string playerName, std::string playerSteamId, std::string playerDiscordId, std::string clientVersion) {
            _playerName      = SLNet::RakString(playerName.c_str());
            _playerSteamId   = SLNet::RakString(playerSteamId.c_str());
            _playerDiscordId = SLNet::RakString(playerDiscordId.c_str());
            _clientVersion   = SLNet::RakString(clientVersion.c_str());
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _playerName);
            bs->Serialize(write, _playerSteamId);
            bs->Serialize(write, _playerDiscordId);
            bs->Serialize(write, _clientVersion);
        }

        bool Valid() override {
            return _playerName.GetLength() > 0 && (_playerSteamId.GetLength() > 0 || _playerDiscordId.GetLength() > 0) && _clientVersion.GetLength() > 0;
        }

        std::string GetPlayerName() {
            return _playerName.C_String();
        }

        std::string GetPlayerSteamID() {
            return _playerSteamId.C_String();
        }

        std::string GetPlayerDiscordID() {
            return _playerDiscordId.C_String();
        }

        std::string GetClientVersion() {
            return _clientVersion.C_String();
        }
    };
} // namespace Framework::Networking::Messages
