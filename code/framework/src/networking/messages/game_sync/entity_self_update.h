/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../messages.h"
#include "world/modules/base.hpp"

#include <BitStream.h>

namespace Framework::Networking::Messages {
    class GameSyncEntitySelfUpdate final: public IMessage {
      public:
        uint8_t GetMessageID() const override {
            return GAME_SYNC_ENTITY_SELF_UPDATE;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {

        }

        bool Valid() override {
            return true;
        }
    };
} // namespace Framework::Networking::Messages
