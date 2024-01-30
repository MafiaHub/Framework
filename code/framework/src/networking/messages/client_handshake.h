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
        SLNet::RakString _playerName      = "";
        SLNet::RakString _playerSteamId   = "";
        SLNet::RakString _playerDiscordId = "";
        SLNet::RakString _clientVersion   = "";
        SLNet::RakString _mpClientVersion = "";
        SLNet::RakString _mpClientGame    = "";

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_HANDSHAKE;
        }

        void FromParameters(const std::string &playerName, const std::string &playerSteamId, const std::string &playerDiscordId, const std::string &clientVersion, const std::string &mpClientVersion, const std::string &mpClientGame) {
            Framework::Logging::GetLogger("dbg")->debug(playerName);
            _playerName      = SLNet::RakString(playerName.c_str());
            _playerSteamId   = SLNet::RakString(playerSteamId.c_str());
            _playerDiscordId = SLNet::RakString(playerDiscordId.c_str());
            _clientVersion   = SLNet::RakString(clientVersion.c_str());
            _mpClientVersion = SLNet::RakString(mpClientVersion.c_str());
            _mpClientGame    = SLNet::RakString(mpClientGame.c_str());
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _playerName);
            bs->Serialize(write, _playerSteamId);
            bs->Serialize(write, _playerDiscordId);
            bs->Serialize(write, _clientVersion);
            bs->Serialize(write, _mpClientVersion);
            bs->Serialize(write, _mpClientGame);
        }

        bool Valid() const override {
            return _playerName.GetLength() > 0 && (_playerSteamId.GetLength() > 0 || _playerDiscordId.GetLength() > 0) && _clientVersion.GetLength() > 0 && _mpClientVersion.GetLength() > 0 && _mpClientGame.GetLength() > 0;
        }

        std::string GetPlayerName() const {
            return _playerName.C_String();
        }

        std::string GetPlayerSteamID() const {
            return _playerSteamId.C_String();
        }

        std::string GetPlayerDiscordID() const {
            return _playerDiscordId.C_String();
        }

        std::string GetClientVersion() const {
            return _clientVersion.C_String();
        }

        std::string GetMPClientVersion() const {
            return _mpClientVersion.C_String();
        }

        std::string GetMPClientGame() const {
            return _mpClientGame.C_String();
        }
    };
} // namespace Framework::Networking::Messages
