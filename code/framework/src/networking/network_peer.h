/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages/messages.h"

#include <PacketPriority.h>
#include <RakPeerInterface.h>
#include <FileListTransfer.h>
#include <DirectoryDeltaTransfer.h>
#include <logging/logger.h>
#include <utils/lifecycle.h>
#include <unordered_map>
#include <utility>
#include <utils/hashing.h>
#include <vector>

namespace Framework::Networking {
    class NetworkPeer : public Lifecycle {
      protected:
        SLNet::RakPeerInterface *_peer = nullptr;
        SLNet::Packet *_packet         = nullptr;
        int _packetDataOffset          = 0; // Offset to skip timestamp prefix if present
        std::unordered_map<uint32_t, std::vector<Messages::PacketCallback>> _registeredRPCs;
        std::unordered_map<uint8_t, Messages::PacketCallback> _registeredMessageCallbacks;
        Messages::PacketCallback _onUnknownPacketCallback;
        mutable SLNet::DirectoryDeltaTransfer _assetStreamer;

      public:
        NetworkPeer();
        ~NetworkPeer();

        bool Send(Messages::IMessage &msg, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) const;

        bool Send(Messages::IMessage &msg, uint64_t guid = (uint64_t)-1, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED);

        void RegisterMessage(uint8_t message, Messages::PacketCallback callback);

        template <typename T>
        void RegisterMessage(uint8_t message, fu2::function<void(SLNet::RakNetGUID, T *) const> callback) {
            if (callback == nullptr) {
                return;
            }

            _registeredMessageCallbacks[message] = [this, callback, message](SLNet::Packet *p) {
                SLNet::BitStream bs(p->data + _packetDataOffset + 1, p->length - _packetDataOffset - 1, false);
                T msg = {};
                msg.SetPacket(p);
                msg.Serialize(&bs, false);
                msg.Serialize2(&bs, false);
                if (msg.Valid2()) {
                    if (msg.Valid()) {
                        callback(p->guid, &msg);
                    }
                    else {
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Message {} has failed to pass Valid() check, skipping!", message);
                    }
                }
                else {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("Message {} has failed to pass Valid2() check, skipping!", message);
                }
            };
        }

        template <typename T>
        void RegisterRPC(fu2::function<void(SLNet::RakNetGUID, T *) const> callback) {
            T _rpc = {};

            if (callback == nullptr) {
                return;
            }

            _registeredRPCs[_rpc.GetHashName()].push_back([this, callback, _rpc](SLNet::Packet *p) {
                SLNet::BitStream bs(p->data + _packetDataOffset + 5, p->length - _packetDataOffset - 5, false);
                T rpc = {};
                rpc.SetPacket(p);
                rpc.Serialize(&bs, false);
                if (rpc.Valid()) {
                    callback(p->guid, &rpc);
                }
                else {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("RPC {} ({}) has failed to pass Valid() check, skipping!", _rpc.GetName(), _rpc.GetHashName());
                }
            });
        }

        template <typename T>
        void RegisterGameRPC(fu2::function<void(SLNet::RakNetGUID, T *) const> callback) {
            T _rpc = {};

            if (callback == nullptr) {
                return;
            }

            _registeredRPCs[_rpc.GetHashName()].push_back([this, callback, _rpc](SLNet::Packet *p) {
                SLNet::BitStream bs(p->data + _packetDataOffset + 5, p->length - _packetDataOffset - 5, false);
                T rpc = {};
                rpc.SetPacket(p);
                rpc.Serialize(&bs, false);
                rpc.Serialize2(&bs, false);
                if (rpc.Valid2()) {
                    if (rpc.Valid()) {
                        callback(p->guid, &rpc);
                    }
                    else {
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("RPC {} has failed to pass Valid() check, skipping!", _rpc.GetHashName());
                    }
                }
                else {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->debug("RPC {} has failed to pass Valid2() check, skipping!", _rpc.GetHashName());
                }
            });
        }

        template <typename T>
        bool SendRPC(T &rpc, SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) {
            SLNet::BitStream bs;
            bs.Write(Messages::INTERNAL_RPC);
            bs.Write(rpc.GetHashName());
            rpc.Serialize(&bs, true);
            assert(!rpc.IsGameRPC() && "Game RPCs cannot be sent via SendRPC()");

            if (_peer->Send(&bs, priority, reliability, 0, guid, guid == SLNet::UNASSIGNED_RAKNET_GUID) <= 0) {
                return false;
            }
            return true;
        }

        void Update() override;
        virtual bool HandlePacket(uint8_t packetID, SLNet::Packet *packet) = 0;

        void SetUnknownPacketHandler(Messages::PacketCallback callback) {
            _onUnknownPacketCallback = std::move(callback);
        }

        SLNet::Packet *GetPacket() const {
            return _packet;
        }

        int GetPacketDataOffset() const {
            return _packetDataOffset;
        }

        SLNet::RakPeerInterface *GetPeer() const {
            return _peer;
        }

        static const char *GetStartupResultString(uint8_t id);
        static const char *GetConnectionAttemptString(uint8_t id);

        SLNet::DirectoryDeltaTransfer* GetAssetStreamer() const {
            return &_assetStreamer;
        }
    };
} // namespace Framework::Networking
