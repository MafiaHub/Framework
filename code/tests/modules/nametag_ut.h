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
#include <mafianet/ReplicaManager3.h>
#include <mafianet/types.h>

#include <cstdint>
#include <string>

// Player.setNametag* (MafiaHub/Framework#296) through the per-tick path: the owner's state channel into
// the server's avatar and the server's into a viewer, each via NetworkEntity::Serialize/Deserialize.
// A client that joins later builds its copy from SerializeConstruction.
class NametagRig {
  public:
    // `health` is owner-written, so every owner update carries something the server visibly applies.
    struct Avatar final: Framework::Networking::Replication::NetworkEntity {
        uint8_t health = 100;
        Framework::Networking::Replication::NametagState nametag;

        Framework::Networking::Replication::NametagState *GetNametag() override {
            return &nametag;
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(health);
            nametag.Serialize(fields);
        }
    };

    // Unstarted peers, like replication_authority_ut: the role is the manager's, not the peer's.
    Framework::Networking::NetworkServer serverPeer;
    Framework::Networking::NetworkServer ownerPeer;
    Framework::Networking::NetworkServer viewerPeer;
    Avatar avatar; // the server's copy
    Avatar owner;  // the owning client's copy
    Avatar viewer; // another client's copy
    MafiaNet::Connection_RM3 *ownerConnection = nullptr;

    explicit NametagRig(bool clientsInWorld = true) {
        Server()->Init(&serverPeer, true);
        ownerPeer.GetReplicationManager()->Init(&ownerPeer, false);
        viewerPeer.GetReplicationManager()->Init(&viewerPeer, false);
        owner.replicaManager  = ownerPeer.GetReplicationManager();
        viewer.replicaManager = viewerPeer.GetReplicationManager();

        avatar.SetNetworkID(1);
        avatar.ownerGUID = ownerPeer.GetReplicationManager()->GetMyGUID();
        Server()->Reference(&avatar);
        ownerConnection = Server()->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, MafiaNet::ToGuid(avatar.ownerGUID));
        Framework::CoreModules::SetReplication(Server());
        if (clientsInWorld) {
            Construct(owner);
            Construct(viewer);
        }
    }

    Framework::Networking::Replication::ReplicationManager *Server() {
        return serverPeer.GetReplicationManager();
    }

    Framework::Scripting::Builtins::Player Handle() {
        return Framework::Scripting::Builtins::Player(avatar.GetNetworkID());
    }

    // What a client that constructs the avatar now (on join, or after streaming it in) starts from.
    void Construct(Avatar &into) {
        MafiaNet::BitStream bs;
        avatar.SerializeConstruction(&bs, nullptr);
        into.DeserializeConstruction(&bs, nullptr);
    }

    // The owner's next update, received on the owner's connection.
    void OwnerTick() {
        Deliver(owner, avatar, ownerConnection, _ownerSent);
    }

    // The server's next update to the viewer.
    void ServerTick() {
        Deliver(avatar, viewer, nullptr, _viewerSent);
    }

    ~NametagRig() {
        Framework::CoreModules::SetReplication(nullptr);
        Server()->DeallocConnection(ownerConnection);
    }

  private:
    bool _ownerSent  = false;
    bool _viewerSent = false;

    // Both channels, serialized as the sending peer writes them; the first send is a full one.
    static void Deliver(Avatar &from, Avatar &to, MafiaNet::Connection_RM3 *source, bool &sent) {
        MafiaNet::SerializeParameters sp;
        for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
            sp.lastSentBitstream[i] = nullptr;
        }
        sp.whenLastSerialized = sent ? 1 : 0;
        sp.curTime            = 1;
        from.OnUserReplicaPreSerializeTick();
        from.Serialize(&sp);
        sent = true;

        MafiaNet::DeserializeParameters dp;
        for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
            dp.bitstreamWrittenTo[i] = sp.outputBitstream[i].GetNumberOfBitsUsed() > 0;
            if (dp.bitstreamWrittenTo[i]) {
                dp.serializationBitstream[i].Write(&sp.outputBitstream[i]);
            }
        }
        dp.timeStamp        = 0;
        dp.sourceConnection = source;
        to.Deserialize(&dp);
    }
};

MODULE(nametag, {
    using Rig = NametagRig;
    using Framework::Networking::Replication::NametagComponent;
    using Framework::Networking::Replication::NametagState;

    IT("hides the name of a player whose client already has its avatar", {
        Rig rig;
        rig.Handle().SetNametagVisible(false);
        rig.OwnerTick();
        rig.ServerTick();

        EQUALS(int64_t(rig.viewer.nametag.Has(NametagComponent::Name)), int64_t(0));
    });

    IT("keeps both of two setters called back to back", {
        Rig rig;
        auto player = rig.Handle();
        player.SetNametagVisible(false);
        player.SetNametagHealthVisible(false);
        rig.OwnerTick();
        rig.ServerTick();

        EQUALS(int64_t(rig.viewer.nametag.Has(NametagComponent::Name)), int64_t(0));
        EQUALS(int64_t(rig.viewer.nametag.Has(NametagComponent::Health)), int64_t(0));
    });

    IT("keeps the text when the colour is set right after it", {
        Rig rig;
        auto player = rig.Handle();
        player.SetNametagText("Ministry");
        player.SetNametagColor(0xFF33CC99);
        rig.OwnerTick();
        rig.ServerTick();

        STREQUALS(rig.viewer.nametag.text.c_str(), "Ministry");
        EQUALS(int64_t(rig.viewer.nametag.color), int64_t(0xFF33CC99));
    });

    IT("hides a name set before the owning client has its avatar", {
        Rig rig(false); // the setter runs in the tick the server creates the avatar
        rig.Handle().SetNametagVisible(false);
        rig.Construct(rig.owner);
        rig.Construct(rig.viewer);
        rig.OwnerTick();
        rig.ServerTick();

        EQUALS(int64_t(rig.viewer.nametag.Has(NametagComponent::Name)), int64_t(0));
    });

    IT("keeps a hidden name hidden when the owning client sends its own", {
        Rig rig;
        rig.Handle().SetNametagVisible(false);
        rig.OwnerTick();
        rig.ServerTick();
        EQUALS(int64_t(rig.viewer.nametag.Has(NametagComponent::Name)), int64_t(0));

        // A modified client turns its own name back on, in an update the server does apply.
        rig.owner.nametag.components = NametagState::kAllComponents;
        rig.owner.health             = 42;
        rig.OwnerTick();
        rig.ServerTick();

        EQUALS(int64_t(rig.avatar.health), int64_t(42));
        EQUALS(int64_t(rig.viewer.health), int64_t(42));
        EQUALS(int64_t(rig.viewer.nametag.Has(NametagComponent::Name)), int64_t(0));
    });

    IT("cuts the text to kMaxTextBytes without splitting a character", {
        Rig rig;
        // The two-byte "\xC3\xAD" straddles the cap, so the cut backs off to before it.
        std::string text(NametagState::kMaxTextBytes - 1, 'a');
        rig.Handle().SetNametagText(text + "\xC3\xAD" + "tail");
        rig.OwnerTick();
        rig.ServerTick();

        STREQUALS(rig.viewer.nametag.text.c_str(), text.c_str());
    });
});
