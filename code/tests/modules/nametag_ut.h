/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "core_modules.h"
#include "networking/network_server.h"
#include "networking/replication/nametag_state.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "scripting/builtins/player.h"

#include <mafianet/BitStream.h>

#include <cstdint>

// Player.setNametag* (MafiaHub/Framework#296): the real setters on the server's copy. The owner and a
// late viewer build theirs from its construction, and the owner's upstream update goes through the
// entity's own SerializeFields. What every other player sees is the server's copy.
class NametagRig {
  public:
    struct Avatar final: Framework::Networking::Replication::NetworkEntity {
        Framework::Networking::Replication::NametagState nametag;

        Framework::Networking::Replication::NametagState *GetNametag() override {
            return &nametag;
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            nametag.Serialize(fields);
        }
    };

    // Unstarted, like replication_authority_ut: the setters only need the server's copy resolvable.
    Framework::Networking::NetworkServer registry;
    Avatar avatar; // the server's copy
    Avatar owner;  // the owning client's copy

    explicit NametagRig(bool ownerInWorld = true) {
        registry.GetReplicationManager()->Init(&registry, true);
        avatar.SetNetworkID(1);
        registry.GetReplicationManager()->Reference(&avatar);
        Framework::CoreModules::SetReplication(registry.GetReplicationManager());
        if (ownerInWorld) {
            Construct(owner);
        }
    }

    Framework::Scripting::Builtins::Player Handle() {
        return Framework::Scripting::Builtins::Player(avatar.GetNetworkID());
    }

    // The owner's next upstream state update.
    void OwnerUpdates() {
        MafiaNet::BitStream bs;
        Framework::Networking::Replication::FieldSerializer out(&bs, true, false);
        owner.SerializeFields(out);
        Framework::Networking::Replication::FieldSerializer in(&bs, false, false);
        avatar.SerializeFields(in);
    }

    // What a client that constructs the avatar now (the owner on join, or a late viewer) starts from.
    void Construct(Avatar &into) {
        MafiaNet::BitStream bs;
        avatar.SerializeConstruction(&bs, nullptr);
        into.DeserializeConstruction(&bs, nullptr);
    }

    ~NametagRig() {
        Framework::CoreModules::SetReplication(nullptr);
    }
};

MODULE(nametag, {
    using Rig = NametagRig;
    using Framework::Networking::Replication::NametagComponent;

    IT("hides the name of a player whose client already has its avatar", {
        Rig rig;
        rig.Handle().SetNametagVisible(false);
        rig.OwnerUpdates();

        EQUALS(int64_t(rig.avatar.nametag.Has(NametagComponent::Name)), int64_t(0));
    });

    IT("keeps both of two setters called back to back", {
        Rig rig;
        auto player = rig.Handle();
        player.SetNametagVisible(false);
        player.SetNametagHealthVisible(false);
        rig.OwnerUpdates();

        EQUALS(int64_t(rig.avatar.nametag.Has(NametagComponent::Name)), int64_t(0));
        EQUALS(int64_t(rig.avatar.nametag.Has(NametagComponent::Health)), int64_t(0));
    });

    IT("keeps the text when the colour is set right after it", {
        Rig rig;
        auto player = rig.Handle();
        player.SetNametagText("Ministry");
        player.SetNametagColor(0xFF33CC99);
        rig.OwnerUpdates();

        STREQUALS(rig.avatar.nametag.text.c_str(), "Ministry");
        EQUALS(int64_t(rig.avatar.nametag.color), int64_t(0xFF33CC99));
    });

    IT("hides a name set before the owning client has its avatar", {
        Rig rig(false); // the setter runs in the tick the server creates the avatar
        rig.Handle().SetNametagVisible(false);
        rig.Construct(rig.owner);
        rig.OwnerUpdates();

        Rig::Avatar lateViewer;
        rig.Construct(lateViewer);
        EQUALS(int64_t(lateViewer.nametag.Has(NametagComponent::Name)), int64_t(0));
    });

    IT("keeps a hidden name hidden when the owning client sends its own", {
        Rig rig;
        rig.Handle().SetNametagVisible(false);
        rig.OwnerUpdates();
        EQUALS(int64_t(rig.avatar.nametag.Has(NametagComponent::Name)), int64_t(0));

        // A modified client turns its own name back on.
        rig.owner.nametag.components = Framework::Networking::Replication::NametagState::kAllComponents;
        rig.OwnerUpdates();

        EQUALS(int64_t(rig.avatar.nametag.Has(NametagComponent::Name)), int64_t(0));
    });
});
