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

#include <mafianet/bit_stream.h>

#include <cstddef>
#include <cstdint>
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
        std::string discordName;
        std::string hardwareId;
        // Authenticated Epic account id; empty when the game was not launched through Epic. Ordered
        // to match Decode(), which is strict: a payload missing any field is refused outright.
        std::string epicId;

        // Opaque string the client was launched with (a launcher-issued join ticket, typically).
        // The framework neither reads nor verifies it; it is handed to playerConnecting as is.
        std::string ticket;

        // Proof of the account (networking/account_key.h). Everything above is a claim the client
        // makes about itself; these let the server check one thing for certain: the request was
        // signed by the holder of publicKey, recently, and only once.
        static constexpr std::size_t kPublicKeyLength = 32; // Ed25519
        static constexpr std::size_t kNonceLength     = 16;
        static constexpr std::size_t kSignatureLength = 64;

        std::string publicKey; // raw bytes
        uint64_t issuedAt = 0; // Unix seconds, the client's clock
        std::string nonce;     // raw bytes, fresh for every request
        std::string signature; // raw bytes, over SigningPayload()

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, name);
            bs->Serialize(write, steamId);
            bs->Serialize(write, discordId);
            bs->Serialize(write, discordName);
            bs->Serialize(write, hardwareId);
            bs->Serialize(write, epicId);
            bs->Serialize(write, ticket);
            bs->Serialize(write, publicKey);
            bs->Serialize(write, issuedAt);
            bs->Serialize(write, nonce);
            bs->Serialize(write, signature);
        }

        // The bytes the signature covers: a tag naming the protocol, then every field but the
        // signature, in wire order. Recomputed from the decoded values on the server, so any field
        // changed on the way invalidates it.
        std::string SigningPayload() const {
            static constexpr char kTag[] = "MafiaHub-ClientIdentity-v1";
            MafiaNet::BitStream bs;
            std::string tag(kTag);
            bs.Write(tag);
            for (const std::string *field : {&name, &steamId, &discordId, &discordName, &hardwareId, &epicId, &ticket, &publicKey}) {
                bs.Write(*field);
            }
            bs.Write(issuedAt);
            bs.Write(nonce);
            return std::string(reinterpret_cast<const char *>(bs.GetData()), bs.GetNumberOfBytesUsed());
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
            if (!bs.Read(identity.name) || !bs.Read(identity.steamId) || !bs.Read(identity.discordId) || !bs.Read(identity.discordName) || !bs.Read(identity.hardwareId) || !bs.Read(identity.epicId) || !bs.Read(identity.ticket) || !bs.Read(identity.publicKey)
                || !bs.Read(identity.issuedAt) || !bs.Read(identity.nonce) || !bs.Read(identity.signature)) {
                return std::nullopt;
            }
            return identity;
        }
    };
} // namespace Framework::Networking::RPC
