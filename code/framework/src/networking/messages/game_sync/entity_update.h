/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../messages.h"
#include "world/modules/base.hpp"

#include <BitStream.h>

namespace Framework::Networking::Messages {
    class GameSyncEntityUpdate final: public IMessage {
      private:
        World::Modules::Base::Transform _transform;

      public:
        uint8_t GetMessageID() const override {
            return GAME_SYNC_ENTITY_UPDATE;
        }

        void FromParameters(World::Modules::Base::Transform tr) {
            _transform = tr;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _transform);
        }

        bool Valid() override {
            // TODO is there anything to validate for transform data?
            return true;
        }

        World::Modules::Base::Transform GetTransform() {
            return _transform;
        }
    };
} // namespace Framework::Networking::Messages
