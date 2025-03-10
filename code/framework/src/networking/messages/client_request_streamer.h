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
    class ClientRequestStreamer final: public IMessage {
      private:
        SLNet::RakString _playerName      = "";
        SLNet::RakString _playerSteamId   = "";
        SLNet::RakString _playerDiscordId = "";

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_REQUEST_STREAMER;
        }

        void FromParameters(const std::string &playerName, const std::string &playerSteamId, const std::string &playerDiscordId) {
            Framework::Logging::GetLogger("dbg")->debug(playerName);
            _playerName      = SLNet::RakString(playerName.c_str());
            _playerSteamId   = SLNet::RakString(playerSteamId.c_str());
            _playerDiscordId = SLNet::RakString(playerDiscordId.c_str());
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _playerName);
            bs->Serialize(write, _playerSteamId);
            bs->Serialize(write, _playerDiscordId);
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
    };
} // namespace Framework::Networking::Messages
