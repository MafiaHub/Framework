/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "channels.h"
#include "connection.h"
#include "rpc/rpc.h"

#include <mafianet/message_identifiers.h>
#include <mafianet/packet_priority.h>
#include <mafianet/peer_interface.h>
#include <mafianet/rpc4_plugin.h>
#include <mafianet/ready_event.h>
#include <mafianet/two_way_authentication.h>
#include <mafianet/network_id_manager.h>
#include <logging/logger.h>
#include <utils/lifecycle.h>
#include <utils/version.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <utils/hashing.h>
#include <vector>

namespace Framework::Networking::Replication {
    class ReplicationManager;
} // namespace Framework::Networking::Replication

namespace Framework::Networking::RPC {
    struct ClientIdentity;
} // namespace Framework::Networking::RPC

namespace Framework::Networking {
    class NetworkPeer : public Lifecycle {
      protected:
        MafiaNet::RakPeerInterface *_peer = nullptr;
        MafiaNet::Packet *_packet         = nullptr;
        int _packetDataOffset          = 0; // Offset to skip timestamp prefix if present
        fu2::function<bool(MafiaNet::Packet *) const> _onUnknownPacketCallback;

        // RPC4 dispatches remote-procedure calls by identifier to C handlers. NetworkIDManager hands
        // out the cross-network object handles used by replicas.
        MafiaNet::RPC4 _rpc;
        MafiaNet::NetworkIDManager _networkIDManager;

        // Connection gate: TwoWayAuthentication proves an identical build token without sending it;
        // ReadyEvent is the per-connection spawn barrier. Flow in network_{server,client}.cpp.
        MafiaNet::TwoWayAuthentication _twoWayAuth;
        MafiaNet::ReadyEvent _readyEvent;
        std::string _buildToken;
        // Token currently registered with TwoWayAuthentication, so an unchanged re-registration can
        // be skipped instead of clearing the plugin (which drops in-flight challenges).
        std::string _registeredToken;

        // Owns the replicated entity world. The concrete peer's Init() attaches it and sets its role.
        std::unique_ptr<Replication::ReplicationManager> _replicationManager;

        // Decoded RPC handlers registered via RegisterRPC<T>. Each is kept alive here for the peer's
        // lifetime; its address is the context RPC4 hands back to DispatchRPC, so handlers can capture.
        // The identifier rides along so a salt change can re-key the slot (see SetIdentifierSalt).
        using RPCSlot = fu2::function<void(MafiaNet::BitStream *, MafiaNet::Packet *)>;
        struct RegisteredRPC {
            RPC::Identifier identifier;
            std::unique_ptr<RPCSlot> slot;
        };
        std::vector<RegisteredRPC> _rpcHandlers;

        // Deployment salt mixed into every RPC4 slot key (see rpc/rpc_identifier.h), and the tokens
        // derived from it so a send costs a lookup, not a hash. Keyed by digest; node-stable, so the
        // C strings handed to RPC4 stay valid for the peer's lifetime.
        std::uint64_t _identifierSalt = RPC::kDefaultIdentifierSalt;
        std::unordered_map<std::uint64_t, RPC::Token> _identifierTokens;

        static void DispatchRPC(MafiaNet::BitStream *bs, MafiaNet::Packet *packet, void *context) {
            auto *slot = static_cast<RPCSlot *>(context);
            if (slot && *slot) {
                (*slot)(bs, packet);
            }
        }

        bool RegisterBuildToken();

      public:
        // TwoWayAuthentication identifier under which the build token is registered/challenged.
        static constexpr const char *kBuildChallengeId = "Framework::Build";

        // Fixed token both peers register when verifyBuildToken is off; challenge still passes.
        static constexpr const char *kBuildVerificationDisabledToken = "Framework::BuildVerificationDisabled";

        // How long the build challenge and the nonce it answers stay valid (TwoWayAuthentication::SetTimeout).
        static constexpr MafiaNet::Time kBuildVerificationTimeoutMs = 30000;

        NetworkPeer();
        ~NetworkPeer();

        // Single source of truth for the gated build identity — must produce the same string on both
        // peers or the challenge fails.
        //
        // The Framework and mod versions are reduced to their major component: by the documented
        // semantics only a major bump moves the wire format, so a patch or minor release on either
        // must not lock out peers that have not rebuilt. The game version is kept whole — it tracks
        // the game executable, not our release cadence, and a different game build shifts the
        // pattern addresses the mod is compiled against.
        //
        // The RPC identifier salt rides along (see rpc/rpc_identifier.h): peers running different
        // salts derive different slot keys, and a mismatch has no other error path.
        static std::string BuildToken(const std::string &gameName, const std::string &gameVersion, const std::string &fwVersion, const std::string &modVersion, std::uint64_t identifierSalt) {
            return gameName + '|' + gameVersion + '|' + Utils::Version::Major(fwVersion) + '|' + Utils::Version::Major(modVersion) + '|' + RPC::IdentifierSaltTag(identifierSalt).c_str();
        }

        // Register the local build token (see BuildToken). Call before connecting/accepting.
        void SetBuildToken(const std::string &token);

        // Set the deployment salt every RPC4 slot key is derived from. Slots already registered are
        // re-keyed, so the order against RegisterRPC does not matter; call before any peer connects.
        void SetIdentifierSalt(std::uint64_t salt);

        std::uint64_t GetIdentifierSalt() const {
            return _identifierSalt;
        }

        // The RPC4 slot key for an identifier under the current salt, valid for the peer's lifetime.
        const char *ResolveIdentifier(RPC::Identifier identifier);

        // Register a handler for RPC payload type T (see networking/rpc/rpc.h). The handler receives
        // the already-decoded payload and the raw packet, and may capture (e.g. the owning instance).
        // A decode wrapper around RegisterRawRPC below, which owns the slot mechanics. Matches the
        // Signal() send below.
        template <RPC::Payload T>
        void RegisterRPC(fu2::function<void(const T &payload, MafiaNet::Packet *packet) const> handler) {
            RegisterRawRPC(T::kIdentifier, [cb = std::move(handler)](MafiaNet::BitStream *bs, MafiaNet::Packet *packet) {
                cb(RPC::Read<T>(bs), packet);
            });
        }

        // Send an RPC payload to every connected system, optionally excluding one (in broadcast mode
        // RakNet treats the guid as the system to SKIP — e.g. don't echo a client's own update back).
        // `channel` is the ordering channel: Events, the construction channel, unless the message
        // belongs to a stream that must not queue behind unrelated traffic.
        template <RPC::Payload T>
        void BroadcastRPC(T &payload, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered, MafiaNet::RakNetGUID except = MafiaNet::UNASSIGNED_RAKNET_GUID, Channel channel = Channel::Events) {
            MafiaNet::BitStream bs;
            payload.Serialize(&bs, true);
            _rpc.Signal(ResolveIdentifier(T::kIdentifier), &bs, priority, reliability, ToOrderingChannel(channel), except, true, false);
        }

        // Send an RPC payload to a single system, on the ordering channel `channel`.
        template <RPC::Payload T>
        void SendRPC(T &payload, MafiaNet::RakNetGUID guid, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered, Channel channel = Channel::Events) {
            MafiaNet::BitStream bs;
            payload.Serialize(&bs, true);
            _rpc.Signal(ResolveIdentifier(T::kIdentifier), &bs, priority, reliability, ToOrderingChannel(channel), guid, false, false);
        }

        // Raw variant of RegisterRPC for handlers that decode the bitstream themselves (e.g. a
        // polymorphic tail). Prefer the typed RegisterRPC<T> for fixed-shape payloads. The callable
        // is stored for the peer's lifetime and reached through RPC4's per-slot context, so no
        // file-static handler pointers are needed.
        void RegisterRawRPC(RPC::Identifier identifier, fu2::function<void(MafiaNet::BitStream *, MafiaNet::Packet *)> handler) {
            auto slot     = std::make_unique<RPCSlot>(std::move(handler));
            void *context = slot.get();
            _rpcHandlers.push_back(RegisteredRPC {identifier, std::move(slot)});
            _rpc.RegisterSlot(ResolveIdentifier(identifier), &NetworkPeer::DispatchRPC, context, 0);
        }

        // Send a pre-encoded bitstream under a raw identifier (pairs with RegisterRawRPC).
        void SendRawRPC(RPC::Identifier identifier, MafiaNet::BitStream &bs, MafiaNet::RakNetGUID guid, MafiaNet::Priority priority = MafiaNet::Priority::High, MafiaNet::Reliability reliability = MafiaNet::Reliability::ReliableOrdered) {
            _rpc.Signal(ResolveIdentifier(identifier), &bs, priority, reliability, ToOrderingChannel(Channel::Events), guid, false, false);
        }

        void Update() override;
        virtual bool HandlePacket(uint8_t packetID, MafiaNet::Packet *packet) = 0;

        // Byte offset of the packet id in a datagram, skipping an optional ID_TIMESTAMP + 8-byte
        // MafiaNet::Time prefix. Returns -1 if too short. Pure + tested so the skip width can't drift
        // from what RakNet writes (a wrong width reads the id mid-timestamp — phantom control packets).
        static int ResolvePacketDataOffset(const uint8_t *data, uint32_t length);

        // True for ReplicaManager3 message ids. The plugin fully processes these but intentionally
        // returns RR_CONTINUE_PROCESSING so applications can observe replication traffic; Update()
        // uses this to keep them off the unknown-packet path. A subclass that wants one (e.g.
        // ID_REPLICA_MANAGER_DOWNLOAD_COMPLETE) can still claim it in its HandlePacket switch.
        static bool IsReplicationPacket(uint8_t packetID);

        // Server-only; base no-op lets shared code kick through a NetworkPeer* without a cast.
        virtual void KickPlayer(MafiaNet::RakNetGUID, DisconnectionReason, const std::string & = "") {}

        // Server-only; the identity the peer announced via ClientIdentity (unverified), else nullptr.
        virtual const RPC::ClientIdentity *GetPeerIdentity(MafiaNet::RakNetGUID) const {
            return nullptr;
        }

        // Server-only; the connection's average round-trip time in ms, or -1 if unavailable.
        virtual int GetPing(MafiaNet::RakNetGUID) const {
            return -1;
        }

        // Server-only; the connection's remote IP address (no port), or empty if unavailable.
        virtual std::string GetAddress(MafiaNet::RakNetGUID) const {
            return "";
        }

        // Handles packets not claimed by the peer or replication plugin. Return true when the
        // packet id is recognized; only packets declined by this handler are logged as unknown.
        void SetUnknownPacketHandler(fu2::function<bool(MafiaNet::Packet *) const> callback) {
            _onUnknownPacketCallback = std::move(callback);
        }

        MafiaNet::Packet *GetPacket() const noexcept {
            return _packet;
        }

        int GetPacketDataOffset() const noexcept {
            return _packetDataOffset;
        }

        // --- Escape hatches ---
        // The accessors below hand out the raw MafiaNet objects for features the framework doesn't
        // wrap. Prefer the typed RPC API above (RegisterRPC<T>/BroadcastRPC<T>/SendRPC<T>) and
        // ReplicationManager's typed entity accessors; reach for these only when those don't cover it.
        MafiaNet::RakPeerInterface *GetPeer() const noexcept {
            return _peer;
        }

        static const char *GetStartupResultString(uint8_t id);
        static const char *GetConnectionAttemptString(uint8_t id);

        MafiaNet::RPC4 *GetRPC() noexcept {
            return &_rpc;
        }

        MafiaNet::NetworkIDManager *GetNetworkIDManager() noexcept {
            return &_networkIDManager;
        }

        MafiaNet::TwoWayAuthentication *GetTwoWayAuth() noexcept {
            return &_twoWayAuth;
        }

        MafiaNet::ReadyEvent *GetReadyEvent() noexcept {
            return &_readyEvent;
        }

        Replication::ReplicationManager *GetReplicationManager() const noexcept {
            return _replicationManager.get();
        }
    };
} // namespace Framework::Networking
