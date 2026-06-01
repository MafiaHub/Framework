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

#include <mafianet/string.h>

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

        void Serialize(MafiaNet::BitStream *bs, bool write) override {
            // Frame holds a std::string (modelName) and so is not trivially-copyable;
            // MafiaNet's BitStream refuses to raw-copy such types. Serialize the members
            // explicitly and route the string through RakString, matching how the
            // networking message classes put strings on the wire.
            bs->Serialize(write, _frame.modelHash);
            bs->Serialize(write, _frame.scale);
            MafiaNet::RakString modelName(_frame.modelName.c_str());
            bs->Serialize(write, modelName);
            if (!write) {
                _frame.modelName = modelName.C_String();
            }
        }

        bool Valid() const override {
            return true;
        }
    };
} // namespace Framework::World::RPC
