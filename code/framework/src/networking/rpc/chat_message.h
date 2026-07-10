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
    // author/color); server->client carries the parts of a line the client reconstructs. author
    // empty = notice line; color is packed 0xRRGGBBAA, 0 = client theme default. '/'-prefixed lines
    // are parsed into a command on the server.
    struct ChatMessage {
        static constexpr const char *kIdentifier = "Framework::ChatMessage";

        std::string text;
        std::string author;
        uint32_t color = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, text);
            bs->Serialize(write, author);
            bs->Serialize(write, color);
        }
    };
} // namespace Framework::Networking::RPC
