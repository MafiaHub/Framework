/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc.h"

#include <cstdint>
#include <string>

namespace Framework::Networking::RPC {
    // A chat line. Client->server carries outgoing text (server resolves the sender, ignores
    // every other field); server->client carries the parts of a line the client reconstructs.
    // author empty = notice line; color is packed 0xRRGGBBAA, 0 = client theme default.
    // '/'-prefixed lines are parsed into a command on the server.
    struct ChatMessage {
        static constexpr const char *kIdentifier = "Framework::ChatMessage";

        std::string text;
        std::string author;
        uint32_t color = 0;

        // The entity that said it, 0 for a notice or a line a resource sent. author is a name,
        // not an identity, so this is what a client keys a body off. Server->client only: inbound
        // the server resolves the sender from its connection and ignores this.
        uint64_t senderNetworkId = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, text);
            bs->Serialize(write, author);
            bs->Serialize(write, color);
            bs->Serialize(write, senderNetworkId);
        }
    };
} // namespace Framework::Networking::RPC
