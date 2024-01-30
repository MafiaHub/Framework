/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
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
        flecs::entity_t _serverID = 0;

      public:
        void SetServerID(flecs::entity_t serverID) {
            _serverID = serverID;
        }

        flecs::entity_t GetServerID() const {
            return _serverID;
        }

        void Serialize2(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _serverID);
        };

        inline bool ValidServerID() const {
            return _serverID > 0;
        }

        /**
         * Validates if the server id was set.
         * @return
         */
        bool Valid2() const override {
            return ValidServerID();
        }
    };
} // namespace Framework::Networking::Messages
