/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../messages.h"
#include "message.h"

#include <BitStream.h>

namespace Framework::Networking::Messages {
    class GameSyncEntityOwnerUpdate final : public GameSyncMessage {
      private:
        uint64_t _owner = SLNet::UNASSIGNED_RAKNET_GUID.g;

      public:
        uint8_t GetMessageID() const override {
            return GAME_SYNC_ENTITY_OWNER_UPDATE;
        }

        void FromParameters(uint64_t owner) {
            _owner = owner;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _owner);
        }

        bool Valid() const override {
            return _owner != SLNet::UNASSIGNED_RAKNET_GUID.g;
        }

        uint64_t GetOwner() const {
            return _owner;
        }
    };
} // namespace Framework::Networking::Messages
