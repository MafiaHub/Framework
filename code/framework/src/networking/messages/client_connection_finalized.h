/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages.h"

#include <BitStream.h>

#include <flecs/flecs.h>

namespace Framework::Networking::Messages {
    class ClientConnectionFinalized final: public IMessage {
      private:
        float _serverTickRate = 0.0f;
        flecs::entity_t _entityID = 0;

      public:
        uint8_t GetMessageID() const override {
            return GAME_CONNECTION_FINALIZED;
        }

        void FromParameters(float tickRate, flecs::entity_t entityID) {
            _serverTickRate = tickRate;
            _entityID       = entityID;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _serverTickRate);
            bs->Serialize(write, _entityID);
        }

        bool Valid() override {
            return _serverTickRate > 0.0f && _entityID > 0;
        }

        float GetServerTickRate() const {
            return _serverTickRate;
        }

        flecs::entity_t GetEntityID() const {
            return _entityID;
        }
    };
} // namespace Framework::Networking::Messages
