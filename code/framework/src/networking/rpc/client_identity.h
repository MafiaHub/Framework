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
    enum class ClientKind : uint8_t {
        Game = 0,
        Headless = 1,
    };

    namespace ClientCapability {
        inline constexpr uint32_t ScriptEvents = 1u << 0;
        inline constexpr uint32_t Replication  = 1u << 1;
        inline constexpr uint32_t GameWorld    = 1u << 2;
        inline constexpr uint32_t NativePlayer = 1u << 3;
        inline constexpr uint32_t UI           = 1u << 4;
        inline constexpr uint32_t Input        = 1u << 5;
        inline constexpr uint32_t KnownMask    = ScriptEvents | Replication | GameWorld | NativePlayer | UI | Input;
        inline constexpr uint32_t GameDefaults = KnownMask;
        inline constexpr uint32_t HeadlessDefaults = ScriptEvents | Replication;
    } // namespace ClientCapability

    // Client -> server after assets download: announces the player. Only honoured for an
    // authenticated connection (NetworkServer::IsAuthenticated).
    struct ClientIdentity {
        static constexpr const char *kIdentifier = "Framework::ClientIdentity";
        static constexpr uint16_t kCapabilityProtocolVersion = 1;

        std::string name;
        std::string steamId;
        std::string discordId;
        std::string hardwareId;
        uint16_t capabilityProtocolVersion = kCapabilityProtocolVersion;
        ClientKind clientKind = ClientKind::Game;
        uint32_t capabilities = ClientCapability::GameDefaults;

        bool HasCapability(uint32_t capability) const {
            return (capabilities & capability) == capability;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, name);
            bs->Serialize(write, steamId);
            bs->Serialize(write, discordId);
            bs->Serialize(write, hardwareId);
            bs->Serialize(write, capabilityProtocolVersion);
            uint8_t serializedKind = static_cast<uint8_t>(clientKind);
            bs->Serialize(write, serializedKind);
            if (!write) {
                clientKind = static_cast<ClientKind>(serializedKind);
            }
            bs->Serialize(write, capabilities);
        }
    };
} // namespace Framework::Networking::RPC
