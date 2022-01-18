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
    class GameSyncEntityUpdate final: public GameSyncMessage {
      private:
        World::Modules::Base::Transform _transform;
        uint64_t _owner;

      public:
        uint8_t GetMessageID() const override {
            return GAME_SYNC_ENTITY_UPDATE;
        }

        void FromParameters(flecs::entity_t serverID, World::Modules::Base::Transform tr, uint64_t owner) {
            _serverID  = serverID;
            _transform = tr;
            _owner     = owner;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _serverID);
            bs->Serialize(write, _transform);
            bs->Serialize(write, _owner);
        }

        bool Valid() override {
            return _owner != SLNet::UNASSIGNED_RAKNET_GUID.g && ValidServerID();
        }

        World::Modules::Base::Transform GetTransform() {
            return _transform;
        }

        uint64_t GetOwner() const {
            return _owner;
        }
    };
} // namespace Framework::Networking::Messages
