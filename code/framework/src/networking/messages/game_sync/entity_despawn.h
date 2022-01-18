/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../messages.h"
#include "message.h"
#include "world/modules/base.hpp"

#include <BitStream.h>

namespace Framework::Networking::Messages {
    class GameSyncEntityDespawn final: public GameSyncMessage {
      public:
        uint8_t GetMessageID() const override {
            return GAME_SYNC_ENTITY_DESPAWN;
        }

        void FromParameters(flecs::entity_t serverID) {
            _serverID = serverID;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _serverID);
        }

        bool Valid() override {
            return ValidServerID();
        }
    };
} // namespace Framework::Networking::Messages
