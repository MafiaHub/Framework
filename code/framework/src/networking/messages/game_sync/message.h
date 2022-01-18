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
    class GameSyncMessage: public IMessage {
      protected:
        flecs::entity_t _serverID;

      public:
        flecs::entity_t GetServerID() {
            return _serverID;
        }

        inline bool ValidServerID() const {
            return _serverID > 0;
        }
    };
} // namespace Framework::Networking::Messages
