/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/rpc/game_rpc.h"
#include "world/modules/base.hpp"

namespace Framework::World::RPC {
    class SetTransform final: public Networking::RPC::IGameRPC<SetTransform> {
      private:
        World::Modules::Base::Transform _transform;
      public:
        void FromParameters(const World::Modules::Base::Transform& tr) {
            _transform = tr;
        }

        World::Modules::Base::Transform GetTransform() {
            return _transform;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _transform);
        }

        bool Valid() const override {
            return true;
        }
    };
} // namespace Framework::World::RPC
