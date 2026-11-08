/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// First, as in every header in this directory: BitStream.h reaches windows.h, whose quoted
// #include "rpc.h" MSVC resolves through the directories of the headers already open -- this one
// included -- and would land on ours while BitStream is still half-declared.
#include "rpc.h"

#include <mafianet/BitStream.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Framework::Networking::RPC {
    // Who is asking to join. Not an RPC: it is the client's MafiaNet session payload, carried in the
    // connection request itself (ID_SESSION_CONFIG_REQUEST). The server's admission gate
    // (playerConnecting) decides on it before MafiaNet reports a connection on either side, so a
    // refused player never holds a player slot, never counts as online and is sent nothing.
    struct ClientIdentity {
        // Longest ticket the server keeps; anything longer is dropped rather than cut, so a script
        // never validates a prefix of what the launcher issued.
        static constexpr std::size_t kMaxTicketLength = 2048;

        std::string name;
        std::string steamId;
        std::string discordId;
        std::string hardwareId;
        // Client-reported Epic account id, not authenticated by the server; empty when absent, else
        // 32 lowercase hex chars once sanitized. Ordered to match Decode(), which refuses a payload
        // missing any field.
        std::string epicId;

        // Opaque string the client was launched with (a launcher-issued join ticket, typically).
        // The framework neither reads nor verifies it; it is handed to playerConnecting as is.
        std::string ticket;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, name);
            bs->Serialize(write, steamId);
            bs->Serialize(write, discordId);
            bs->Serialize(write, hardwareId);
            bs->Serialize(write, epicId);
            bs->Serialize(write, ticket);
        }

        // The session payload, as MafiaNet carries it.
        std::string Encode() {
            MafiaNet::BitStream bs;
            Serialize(&bs, true);
            return std::string(reinterpret_cast<const char *>(bs.GetData()), bs.GetNumberOfBytesUsed());
        }

        // Nullopt when the bytes do not hold a whole identity: a client from another build, or one that
        // is not a framework client at all.
        static std::optional<ClientIdentity> Decode(std::string_view payload) {
            if (payload.empty()) {
                return std::nullopt;
            }
            MafiaNet::BitStream bs(reinterpret_cast<unsigned char *>(const_cast<char *>(payload.data())), static_cast<unsigned int>(payload.size()), false);
            ClientIdentity identity;
            if (!bs.Read(identity.name) || !bs.Read(identity.steamId) || !bs.Read(identity.discordId) || !bs.Read(identity.hardwareId) || !bs.Read(identity.epicId) || !bs.Read(identity.ticket)) {
                return std::nullopt;
            }
            return identity;
        }

        // What the server does to a received identity before retaining it or showing it to scripts:
        // nickname length-capped, decimal ids kept only as digit strings, the Epic id kept only as 32
        // hex chars and lowercased, a ticket too long to be one dropped rather than cut. Returns true
        // when the ticket was dropped.
        bool Sanitize() {
            if (name.size() > 64) {
                name.resize(64);
            }
            const auto digits = [](std::string &value, std::size_t max) {
                if (value.size() > max || value.find_first_not_of("0123456789") != std::string::npos) {
                    value.clear();
                }
            };
            digits(steamId, 32);
            digits(discordId, 32);
            digits(hardwareId, 128);
            if (!epicId.empty() && (epicId.size() != 32 || epicId.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)) {
                epicId.clear();
            }
            std::ranges::transform(epicId, epicId.begin(), [](char c) {
                return (c >= 'A' && c <= 'F') ? static_cast<char>(c - 'A' + 'a') : c;
            });
            if (ticket.size() > kMaxTicketLength) {
                ticket.clear();
                return true;
            }
            return false;
        }
    };
} // namespace Framework::Networking::RPC
