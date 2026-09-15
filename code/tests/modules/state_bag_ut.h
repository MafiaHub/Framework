/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_server.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "networking/replication/state_bag.h"
#include "networking/rpc/state_bag_sync.h"

#include <mafianet/BitStream.h>
#include <mafianet/types.h>

#include <string>
#include <vector>

// Entity state bags: storage rules, limits, the construction seed, and the wire payload.
//
// Not covered: the per-connection routing in ReplicationManager::FlushStateBags. It asks
// Connection_RM3::HasReplicaConstructed, which the base ReplicaManager3 only knows after a real
// construction handshake over a bound socket, so a test here could only assert against a stub of the
// thing under test. What feeds that routing is pinned instead — which keys are dirty, the scope each
// carries, and what a seed writes for an owner versus for everyone else.
MODULE(state_bag, {
    using Framework::Networking::NetworkServer;
    using Framework::Networking::Replication::NetworkEntity;
    using Framework::Networking::Replication::StateBag;
    using Framework::Networking::Replication::StateChange;
    using Framework::Networking::Replication::StateScope;
    using Framework::Networking::Replication::StateValue;
    using Framework::Networking::RPC::StateBagSync;

    // Same shape as replication_authority_ut: an unstarted peer whose manager is in server mode.
    // Nothing sends; the manager is here because an entity reaches its change callback and dirty
    // list through it.
    NetworkServer serverPeer;
    auto *serverManager = serverPeer.GetReplicationManager();
    serverManager->Init(&serverPeer, true);

    auto text = [](const std::string &value) {
        StateValue out;
        out.type = StateValue::Type::String;
        out.text = value;
        return out;
    };
    auto number = [](double value) {
        StateValue out;
        out.type   = StateValue::Type::Number;
        out.number = value;
        return out;
    };

    IT("stores and reads back a value", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        EQUALS(entity.state.Set("job", text("blacksmith")) == StateBag::WriteResult::Applied, true);
        EQUALS(entity.state.Has("job"), true);
        EQUALS(entity.state.Get("job")->text, std::string("blacksmith"));
        EQUALS(entity.state.Size(), size_t(1));
    });

    IT("reads an unset key as nothing rather than as a default", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        EQUALS(entity.state.Get("missing") == nullptr, true);
        EQUALS(entity.state.Has("missing"), false);
    });

    IT("reports an unchanged write instead of resending it", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Set("job", text("blacksmith"));
        entity.state.ClearDirty();
        // A script polling its own state into the bag every tick would otherwise fill the wire with
        // identical packets.
        EQUALS(entity.state.Set("job", text("blacksmith")) == StateBag::WriteResult::Unchanged, true);
        EQUALS(entity.state.HasDirty(), false);
    });

    IT("treats a scope change on the same value as a real change", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Set("cuffed", text("yes"), StateScope::Broadcast);
        entity.state.ClearDirty();
        // The value is identical but its audience is not, so this still has to go out.
        EQUALS(entity.state.Set("cuffed", text("yes"), StateScope::Owner) == StateBag::WriteResult::Applied, true);
        EQUALS(entity.state.Dirty().at("cuffed") == StateScope::Owner, true);
    });

    IT("refuses a key longer than the limit", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        const std::string key(StateBag::kMaxKeyLength + 1, 'k');
        EQUALS(entity.state.Set(key, text("x")) == StateBag::WriteResult::KeyTooLong, true);
        EQUALS(entity.state.Has(key), false);
    });

    IT("refuses an empty key", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        EQUALS(entity.state.Set("", text("x")) == StateBag::WriteResult::KeyTooLong, true);
    });

    IT("accepts a key of exactly the limit", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        const std::string key(StateBag::kMaxKeyLength, 'k');
        EQUALS(entity.state.Set(key, text("x")) == StateBag::WriteResult::Applied, true);
    });

    IT("refuses a value larger than the limit", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        EQUALS(entity.state.Set("blob", text(std::string(StateBag::kMaxValueBytes + 1, 'v'))) == StateBag::WriteResult::ValueTooLarge, true);
        EQUALS(entity.state.Has("blob"), false);
    });

    IT("refuses a new key once the bag is full but still updates an existing one", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        for (size_t i = 0; i < StateBag::kMaxKeys; ++i) {
            entity.state.Set("k" + std::to_string(i), number(static_cast<double>(i)));
        }
        EQUALS(entity.state.Size(), StateBag::kMaxKeys);
        EQUALS(entity.state.Set("overflow", number(1.0)) == StateBag::WriteResult::TooManyKeys, true);
        // A full bag must not become read-only; overwriting an existing key adds nothing.
        EQUALS(entity.state.Set("k0", number(99.0)) == StateBag::WriteResult::Applied, true);
    });

    IT("removes a key and reports whether there was one", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Set("job", text("blacksmith"));
        EQUALS(entity.state.Remove("job"), true);
        EQUALS(entity.state.Has("job"), false);
        EQUALS(entity.state.Remove("job"), false);
    });

    IT("keeps a Server-scoped key off the wire entirely", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Set("secret", text("hidden"), StateScope::Server);
        // Readable locally but never dirty, so the flush has nothing to send.
        EQUALS(entity.state.Get("secret")->text, std::string("hidden"));
        EQUALS(entity.state.HasDirty(), false);
    });

    IT("records the scope a removal has to travel with", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Set("cuffed", text("yes"), StateScope::Owner);
        entity.state.ClearDirty();
        entity.state.Remove("cuffed");
        // The entry is gone, so the dirty record is the only thing that still knows the removal is
        // owner-only. Without it the flush would broadcast the removal of a key nobody else was sent.
        EQUALS(entity.state.Dirty().at("cuffed") == StateScope::Owner, true);
    });

    IT("lists keys in a stable order", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Set("zulu", number(1.0));
        entity.state.Set("alpha", number(2.0));
        entity.state.Set("mike", number(3.0));

        const std::vector<std::string> keys = entity.state.Keys();
        EQUALS(keys.size(), size_t(3));
        // The backing store's order is arbitrary and unstable, and scripts enumerate this.
        EQUALS(keys[0], std::string("alpha"));
        EQUALS(keys[1], std::string("mike"));
        EQUALS(keys[2], std::string("zulu"));
    });

    IT("raises a change with the previous value and whether there was one", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        std::vector<StateChange> seen;
        serverManager->SetOnStateChanged([&seen](const StateChange &change) {
            seen.push_back(change);
        });

        entity.state.Set("job", text("farmer"));
        entity.state.Set("job", text("blacksmith"));
        entity.state.Remove("job");

        EQUALS(seen.size(), size_t(3));
        // Null is a value a script may store, so "was null" and "was never set" are told apart by
        // hadPrevious rather than by the value.
        EQUALS(seen[0].hadPrevious, false);
        EQUALS(seen[0].value.text, std::string("farmer"));
        EQUALS(seen[1].hadPrevious, true);
        EQUALS(seen[1].previous.text, std::string("farmer"));
        EQUALS(seen[1].value.text, std::string("blacksmith"));
        EQUALS(seen[2].removed, true);
        EQUALS(seen[2].previous.text, std::string("blacksmith"));
        EQUALS(seen[2].value.type == StateValue::Type::Null, true);

        serverManager->SetOnStateChanged(nullptr);
    });

    IT("raises nothing for a write that changed nothing", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Set("job", text("blacksmith"));

        size_t raised = 0;
        serverManager->SetOnStateChanged([&raised](const StateChange &) {
            ++raised;
        });
        entity.state.Set("job", text("blacksmith"));
        EQUALS(raised, size_t(0));

        serverManager->SetOnStateChanged(nullptr);
    });

    IT("seeds a non-owner with broadcast keys only", {
        NetworkEntity source;
        source.replicaManager = serverManager;
        source.state.Set("job", text("blacksmith"), StateScope::Broadcast);
        source.state.Set("cuffed", text("yes"), StateScope::Owner);
        source.state.Set("secret", text("hidden"), StateScope::Server);

        MafiaNet::BitStream bs;
        source.state.SerializeSeed(&bs, false);

        NetworkEntity destination;
        destination.replicaManager                = serverManager;
        const std::vector<std::string> seededKeys = destination.state.DeserializeSeed(&bs);

        EQUALS(seededKeys.size(), size_t(1));
        EQUALS(destination.state.Has("job"), true);
        // One belongs to the owner, one never leaves the server.
        EQUALS(destination.state.Has("cuffed"), false);
        EQUALS(destination.state.Has("secret"), false);
    });

    IT("seeds the owner with its own keys as well", {
        NetworkEntity source;
        source.replicaManager = serverManager;
        source.state.Set("job", text("blacksmith"), StateScope::Broadcast);
        source.state.Set("cuffed", text("yes"), StateScope::Owner);
        source.state.Set("secret", text("hidden"), StateScope::Server);

        MafiaNet::BitStream bs;
        source.state.SerializeSeed(&bs, true);

        NetworkEntity destination;
        destination.replicaManager = serverManager;
        destination.state.DeserializeSeed(&bs);

        EQUALS(destination.state.Has("job"), true);
        EQUALS(destination.state.Has("cuffed"), true);
        // Server scope is absolute: not even the owner is sent it.
        EQUALS(destination.state.Has("secret"), false);
    });

    IT("survives a seed round trip with every value type", {
        NetworkEntity source;
        source.replicaManager = serverManager;

        StateValue flag;
        flag.type    = StateValue::Type::Boolean;
        flag.boolean = true;
        StateValue nothing;
        StateValue document;
        document.type = StateValue::Type::Json;
        document.text = R"({"a":1})";

        source.state.Set("flag", flag);
        source.state.Set("count", number(42.5));
        source.state.Set("name", text("Henry"));
        source.state.Set("nothing", nothing);
        source.state.Set("document", document);

        MafiaNet::BitStream bs;
        source.state.SerializeSeed(&bs, false);

        NetworkEntity destination;
        destination.replicaManager = serverManager;
        destination.state.DeserializeSeed(&bs);

        EQUALS(destination.state.Size(), size_t(5));
        EQUALS(destination.state.Get("flag")->boolean, true);
        EQUALS(destination.state.Get("count")->number, 42.5);
        EQUALS(destination.state.Get("name")->text, std::string("Henry"));
        EQUALS(destination.state.Get("nothing")->type == StateValue::Type::Null, true);
        EQUALS(destination.state.Get("document")->text, std::string(R"({"a":1})"));
    });

    IT("replaces the whole bag when a seed arrives", {
        NetworkEntity destination;
        destination.replicaManager = serverManager;
        // State from a previous stream-in. A re-seed is a fresh snapshot, so anything the server no
        // longer holds must not survive it.
        destination.state.Set("stale", text("old"));

        NetworkEntity source;
        source.replicaManager = serverManager;
        source.state.Set("job", text("blacksmith"));

        MafiaNet::BitStream bs;
        source.state.SerializeSeed(&bs, false);
        destination.state.DeserializeSeed(&bs);

        EQUALS(destination.state.Has("stale"), false);
        EQUALS(destination.state.Has("job"), true);
    });

    IT("seeds an empty bag as an empty bag", {
        NetworkEntity source;
        source.replicaManager = serverManager;

        MafiaNet::BitStream bs;
        source.state.SerializeSeed(&bs, false);

        NetworkEntity destination;
        destination.replicaManager = serverManager;
        EQUALS(destination.state.DeserializeSeed(&bs).size(), size_t(0));
        EQUALS(destination.state.Empty(), true);
    });

    IT("applies an inbound change on the receiving side", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Apply("job", text("blacksmith"), false);
        EQUALS(entity.state.Get("job")->text, std::string("blacksmith"));

        entity.state.Apply("job", StateValue {}, true);
        EQUALS(entity.state.Has("job"), false);
    });

    IT("does not dirty the bag when applying an inbound change", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Apply("job", text("blacksmith"), false);
        // Apply is the receiving path; dirtying it would send the server's own change back to it.
        EQUALS(entity.state.HasDirty(), false);
    });

    IT("lists the owner-scoped keys an ownership change has to re-send", {
        NetworkEntity entity;
        entity.replicaManager = serverManager;

        entity.state.Set("job", text("blacksmith"), StateScope::Broadcast);
        entity.state.Set("cuffed", text("yes"), StateScope::Owner);
        entity.state.Set("warrant", text("open"), StateScope::Owner);
        entity.state.Set("secret", text("hidden"), StateScope::Server);

        const std::vector<std::string> ownerKeys = entity.state.OwnerKeys();
        // Exactly what a new owner has not been sent: it already has the broadcast keys from
        // construction and is never getting the server one.
        EQUALS(ownerKeys.size(), size_t(2));
        EQUALS(ownerKeys[0], std::string("cuffed"));
        EQUALS(ownerKeys[1], std::string("warrant"));
    });

    IT("round-trips the sync payload", {
        StateBagSync out;
        StateBagSync::Change first;
        first.networkId = 7;
        first.key       = "job";
        first.value     = text("blacksmith");
        StateBagSync::Change second;
        second.networkId = 9;
        second.key       = "cuffed";
        second.removed   = true;
        out.changes      = {first, second};

        MafiaNet::BitStream bs;
        out.Serialize(&bs, true);

        StateBagSync in;
        in.Serialize(&bs, false);

        EQUALS(in.changes.size(), size_t(2));
        EQUALS(in.changes[0].networkId == MafiaNet::NetworkID(7), true);
        EQUALS(in.changes[0].key, std::string("job"));
        EQUALS(in.changes[0].value.text, std::string("blacksmith"));
        EQUALS(in.changes[0].removed, false);
        EQUALS(in.changes[1].networkId == MafiaNet::NetworkID(9), true);
        EQUALS(in.changes[1].key, std::string("cuffed"));
        EQUALS(in.changes[1].removed, true);
    });

    IT("round-trips an empty sync payload", {
        StateBagSync out;
        MafiaNet::BitStream bs;
        out.Serialize(&bs, true);

        StateBagSync in;
        in.Serialize(&bs, false);
        EQUALS(in.changes.size(), size_t(0));
    });

    IT("reads an unknown value tag as null instead of trusting it", {
        MafiaNet::BitStream bs;
        // A tag past the end of the enum, as a newer peer or a corrupt stream would produce.
        bs.Write(static_cast<uint8_t>(200));

        StateValue value;
        value.Serialize(&bs, false);
        EQUALS(value.type == StateValue::Type::Null, true);
    });
})
