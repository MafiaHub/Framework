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

#include <flecs/flecs.h>

namespace Framework::Networking::Messages {
    class ClientInitPlayer final: public IMessage {
      public:
        uint8_t GetMessageID() const override {
            return GAME_INIT_PLAYER;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {}

        bool Valid() const override {
            return true;
        }
    };
} // namespace Framework::Networking::Messages
