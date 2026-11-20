/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/rpc/rpc.h>
#include <networking/rpc/rpc_identifier.h>

#include <mafianet/bit_stream.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Framework::Integrations::Shared::RPC {
    // RPC payload forwarding a named scripting event (with a JSON payload) to the other peer's
    // resource layer. See networking/rpc/rpc.h for the dispatch model.
    //
    // Both strings go as a 32-bit length and the bytes. RakString's 16-bit length cut anything past
    // 64 KiB without a word, which the receiver then saw as an empty or malformed payload.
    struct EmitScriptEvent {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Framework::EmitScriptEvent");

        static constexpr size_t kMaxNameBytes    = 1024;
        static constexpr size_t kMaxPayloadBytes = size_t {1} << 20;
        // A server decodes under RPC::ReadChecked, which notices a read past the end only this far out.
        static_assert(kMaxPayloadBytes <= Framework::Networking::RPC::detail::kReadPadBytes);

        std::string eventName;
        std::string payload;

        // Whether the event can be sent: a sender checks this and refuses, rather than have the
        // receiver drop it.
        static bool Fits(const std::string &name, const std::string &payloadJson) {
            return name.size() <= kMaxNameBytes && payloadJson.size() <= kMaxPayloadBytes;
        }

        void FromParameters(const std::string &name, const std::string &payloadJson) {
            eventName = name;
            payload   = payloadJson;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            // A field over its limit leaves both empty, which every receiver drops on the empty name.
            if (!SerializeField(bs, write, eventName, kMaxNameBytes) || !SerializeField(bs, write, payload, kMaxPayloadBytes)) {
                eventName.clear();
                payload.clear();
            }
        }

        const std::string &GetEventName() const {
            return eventName;
        }

        const std::string &GetPayload() const {
            return payload;
        }

      private:
        static bool SerializeField(MafiaNet::BitStream *bs, bool write, std::string &value, size_t limit) {
            if (write) {
                // Senders check Fits first; this keeps the announced length and the body in step.
                const auto length = static_cast<uint32_t>(value.size() <= limit ? value.size() : 0);
                bs->Write(length);
                // Only when there are bytes: an empty write still aligns, an empty read does not.
                if (length > 0) {
                    bs->WriteAlignedBytes(reinterpret_cast<const unsigned char *>(value.data()), length);
                }
                return length == value.size();
            }
            uint32_t length = 0;
            if (!bs->Read(length) || length > limit) {
                return false;
            }
            value.resize(length);
            return length == 0 || bs->ReadAlignedBytes(reinterpret_cast<unsigned char *>(value.data()), length);
        }
    };
} // namespace Framework::Integrations::Shared::RPC
