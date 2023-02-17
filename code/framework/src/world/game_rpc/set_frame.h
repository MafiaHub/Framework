/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/rpc/game_rpc.h"
#include "world/modules/base.hpp"

namespace Framework::World::RPC {
    class SetFrame final: public Networking::RPC::IGameRPC<SetFrame> {
      private:
        World::Modules::Base::Frame _frame;

      public:
        void FromParameters(const World::Modules::Base::Frame &fr) {
            _frame = fr;
        }

        World::Modules::Base::Frame GetFrame() {
            return _frame;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _frame);
        }

        bool Valid() const override {
            return true;
        }
    };
} // namespace Framework::World::RPC
