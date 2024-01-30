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
        SLNet::RakString _fwVersion       = "";
        SLNet::RakString _gameVersion     = "";
        SLNet::RakString _gameName        = "";

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_HANDSHAKE;
        }

        void FromParameters(const std::string &playerName, const std::string &playerSteamId, const std::string &playerDiscordId, const std::string &clientVersion, const std::string &fwVersion, const std::string &gameVersion, const std::string &gameName) {
            Framework::Logging::GetLogger("dbg")->debug(playerName);
            _playerName      = SLNet::RakString(playerName.c_str());
            _playerSteamId   = SLNet::RakString(playerSteamId.c_str());
            _playerDiscordId = SLNet::RakString(playerDiscordId.c_str());
            _fwVersion       = SLNet::RakString(fwVersion.c_str());
            _clientVersion   = SLNet::RakString(clientVersion.c_str());
            _gameVersion     = SLNet::RakString(gameVersion.c_str());
            _gameName        = SLNet::RakString(gameName.c_str());
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _playerName);
            bs->Serialize(write, _playerSteamId);
            bs->Serialize(write, _playerDiscordId);
            bs->Serialize(write, _fwVersion);
            bs->Serialize(write, _clientVersion);
            bs->Serialize(write, _gameVersion);
            bs->Serialize(write, _gameName);
        }

        bool Valid() const override {
            return _playerName.GetLength() > 0 && (_playerSteamId.GetLength() > 0 || _playerDiscordId.GetLength() > 0) && _fwVersion.GetLength() > 0 && _clientVersion.GetLength() > 0 && _gameVersion.GetLength() > 0 && _gameName.GetLength() > 0;
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
