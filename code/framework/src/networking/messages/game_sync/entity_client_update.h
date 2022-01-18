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
    class GameSyncEntityClientUpdate final: public GameSyncMessage {
      private:
        World::Modules::Base::Transform _transform;

      public:
        uint8_t GetMessageID() const override {
            return GAME_SYNC_ENTITY_CLIENT_UPDATE;
        }

        void FromParameters(flecs::entity_t serverID, World::Modules::Base::Transform tr) {
            _serverID  = serverID;
            _transform = tr;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _serverID);
            bs->Serialize(write, _transform);
        }

        bool Valid() override {
            return ValidServerID();
        }

        World::Modules::Base::Transform GetTransform() {
            return _transform;
        }
    };
} // namespace Framework::Networking::Messages
