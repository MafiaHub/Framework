/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc.h"

#include <mafianet/network_id_manager.h>

namespace Framework::Networking::RPC {
    // Client -> server: the sender cannot run this entity right now, or can again. A client speaks
    // only for itself, so the sender's guid is the one declined.
    struct DeclineSimulation {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Framework::DeclineSimulation");

        MafiaNet::NetworkID networkId {};
        bool declined = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->SerializeCompressed(write, networkId);
            bs->Serialize(write, declined);
        }
    };
} // namespace Framework::Networking::RPC
