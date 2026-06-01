/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages.h"

#include <mafianet/BitStream.h>

namespace Framework::Networking::Messages {
    class ClientRequestStreamer final: public IMessage {
      private:
        MafiaNet::RakString _playerName      = "";
        MafiaNet::RakString _playerSteamId   = "";
        MafiaNet::RakString _playerDiscordId = "";
        MafiaNet::RakString _playerHardwareId = "";

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_REQUEST_STREAMER;
        }

        void FromParameters(const std::string &playerName, const std::string &playerSteamId, const std::string &playerDiscordId, const std::string &playerHardwareId = "") {
            _playerName       = MafiaNet::RakString(playerName.c_str());
            _playerSteamId    = MafiaNet::RakString(playerSteamId.c_str());
            _playerDiscordId  = MafiaNet::RakString(playerDiscordId.c_str());
            _playerHardwareId = MafiaNet::RakString(playerHardwareId.c_str());
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _playerName);
            bs->Serialize(write, _playerSteamId);
            bs->Serialize(write, _playerDiscordId);
            bs->Serialize(write, _playerHardwareId);
        }

        bool Valid() const override {
            return _playerName.GetLength() > 0 && (_playerSteamId.GetLength() > 0 || _playerDiscordId.GetLength() > 0);
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

        std::string GetPlayerHardwareID() const {
            return _playerHardwareId.C_String();
        }
    };
} // namespace Framework::Networking::Messages
