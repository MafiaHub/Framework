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
    // A chat line. Client->server carries a player's outgoing text (the server resolves the sender
    // from the connection); server->client carries a line to display. Lines starting with '/' are
    // parsed into a command and arguments on the server.
    struct ChatMessage {
        static constexpr const char *kIdentifier = "Framework::ChatMessage";

        std::string text;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            // std::string is a first-class wire type in MafiaNet (length-prefixed, same format as
            // RakString), so this stays a single symmetric line.
            bs->Serialize(write, text);
        }
    };
} // namespace Framework::Networking::RPC
