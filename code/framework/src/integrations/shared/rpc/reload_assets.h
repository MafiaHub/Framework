/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/rpc/rpc.h>

#include <string>

namespace Framework::Integrations::Shared::RPC {
    class ReloadAssets final: public Framework::Networking::RPC::IRPC<ReloadAssets> {
      public:
        void Serialize(SLNet::BitStream *bs, bool write) override {
            /* no op */
        }

        bool Valid() const override {
            return true;
        }
    };
} // namespace Framework::Integrations::Shared::RPC
