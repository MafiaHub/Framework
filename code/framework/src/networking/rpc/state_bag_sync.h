/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../replication/state_bag.h"
#include "rpc.h"

#include <mafianet/NetworkIDManager.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Framework::Networking::RPC {
    // Server -> client state-bag deltas, coalesced once per tick.
    //
    // Rides Channel::Events, which shares an ordering channel with Channel::Construction, so a change
    // naming an entity arrives after the construction that seeded its bag. That is why bags carry no
    // sequence number of their own.
    //
    // Never broadcast: ReplicationManager sends it only to connections that have the entity
    // constructed, which is what makes bags inherit interest, virtual worlds and budgets.
    struct StateBagSync {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::StateBagSync");

        // The sender chunks to this, and the receiver rejects anything above it.
        static constexpr uint16_t kMaxChanges = 256;

        struct Change {
            MafiaNet::NetworkID networkId {};
            std::string key;
            Replication::StateValue value;
            bool removed = false;
        };

        std::vector<Change> changes;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            auto count = static_cast<uint16_t>(std::min<size_t>(changes.size(), kMaxChanges));
            bs->Serialize(write, count);
            if (!write) {
                // The read consumes a full uint16 -- the value written above is not a bound on what
                // comes back, so a corrupt or hostile sender can name 65535 here. The sender never
                // writes more than kMaxChanges, so anything above it is malformed: drop the payload
                // rather than allocate on a number off the wire.
                if (count > kMaxChanges) {
                    changes.clear();
                    return;
                }
                changes.assign(count, Change {});
            }
            else if (changes.size() > count) {
                // Unreachable from the flush, which chunks; the announced length and the body must
                // not be able to disagree.
                changes.resize(count);
            }

            for (auto &change : changes) {
                bs->SerializeCompressed(write, change.networkId);
                bs->Serialize(write, change.key);
                bs->Serialize(write, change.removed);
                change.value.Serialize(bs, write);
            }
        }
    };
} // namespace Framework::Networking::RPC
