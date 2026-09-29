/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc.h"

namespace Framework::Networking::RPC {
    // Client -> server once assets are downloaded and client resources are running: build my avatar
    // and start replicating to me. Honoured only for a build-verified connection. Admission is not
    // checked here because it cannot be missing: a connection only exists once the server accepted
    // its session request (ClientIdentity), so a refused peer has nothing to send this over.
    struct ClientJoin {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::ClientJoin");

        void Serialize(MafiaNet::BitStream *, bool) {}
    };
} // namespace Framework::Networking::RPC
