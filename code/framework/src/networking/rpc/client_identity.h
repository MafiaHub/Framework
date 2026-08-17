/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc.h"

#include <string>

namespace Framework::Networking::RPC {
    // Client -> server after assets download: announces the player. Only honoured for an
    // authenticated connection (NetworkServer::IsAuthenticated).
    struct ClientIdentity {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::ClientIdentity");

        std::string name;
        std::string steamId;
        std::string discordId;
        std::string hardwareId;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, name);
            bs->Serialize(write, steamId);
            bs->Serialize(write, discordId);
            bs->Serialize(write, hardwareId);
        }
    };
} // namespace Framework::Networking::RPC
