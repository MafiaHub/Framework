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
#include <unordered_map>
#include <utility>
#include <utils/hashing.h>
#include <vector>

namespace Framework::Networking {
    // Forward declarations for fluent router API
    class NetworkPeer;

    /**
     * Binder for IMessage handlers. The handler instance must outlive the NetworkPeer.
     */
    template <typename T>
    class MessageBinder {
      private:
        NetworkPeer *_peer;

      public:
        explicit MessageBinder(NetworkPeer *peer): _peer(peer) {}

        template <typename Instance>
        void handle(Instance *inst, void (Instance::*method)(SLNet::RakNetGUID, T *));
    };

    /**
     * Binder for IRPC handlers. The handler instance must outlive the NetworkPeer.
     */
    template <typename T>
    class RPCBinder {
      private:
        NetworkPeer *_peer;

      public:
        explicit RPCBinder(NetworkPeer *peer): _peer(peer) {}

        template <typename Instance>
        void handle(Instance *inst, void (Instance::*method)(SLNet::RakNetGUID, T *));
    };

    /**
     * Binder for IGameRPC handlers. The handler instance must outlive the NetworkPeer.
     */
    template <typename T>
    class GameRPCBinder {
      private:
        NetworkPeer *_peer;

      public:
        explicit GameRPCBinder(NetworkPeer *peer): _peer(peer) {}

        template <typename Instance>
        void handle(Instance *inst, void (Instance::*method)(SLNet::RakNetGUID, T *));
    };

    /**
     * Fluent API for registering message and RPC handlers.
     * Usage: net->router().on<MessageType>().handle(this, &Class::Handler);
     */
    class MessageRouter {
      private:
        NetworkPeer *_peer;

      public:
        explicit MessageRouter(NetworkPeer *peer): _peer(peer) {}

        template <typename T>
        MessageBinder<T> on() {
            return MessageBinder<T>(_peer);
        }

        template <typename T>
        RPCBinder<T> onRPC() {
            return RPCBinder<T>(_peer);
        }

        template <typename T>
        GameRPCBinder<T> onGameRPC() {
            return GameRPCBinder<T>(_peer);
        }
    };

    class NetworkPeer {
      protected:
        SLNet::RakPeerInterface *_peer = nullptr;
        SLNet::Packet *_packet         = nullptr;
        std::unordered_map<uint32_t, std::vector<Messages::PacketCallback>> _registeredRPCs;
        std::unordered_map<uint8_t, Messages::PacketCallback> _registeredMessageCallbacks;
        Messages::PacketCallback _onUnknownPacketCallback;
        SLNet::DirectoryDeltaTransfer _assetStreamer;

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

            _registeredMessageCallbacks[message] = [callback, message](SLNet::Packet *p) {
                SLNet::BitStream bs(p->data + 1, p->length, false);
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

            _registeredRPCs[_rpc.GetHashName()].push_back([callback, _rpc](SLNet::Packet *p) {
                SLNet::BitStream bs(p->data + 5, p->length, false);
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

            _registeredRPCs[_rpc.GetHashName()].push_back([callback, _rpc](SLNet::Packet *p) {
                SLNet::BitStream bs(p->data + 5, p->length, false);
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

        virtual void Update();
        virtual bool HandlePacket(uint8_t packetID, SLNet::Packet *packet) = 0;

        void SetUnknownPacketHandler(Messages::PacketCallback callback) {
            _onUnknownPacketCallback = std::move(callback);
        }

        SLNet::Packet *GetPacket() const {
            return _packet;
        }

        SLNet::RakPeerInterface *GetPeer() const {
            return _peer;
        }

        static const char *GetStartupResultString(uint8_t id);
        static const char *GetConnectionAttemptString(uint8_t id);

        SLNet::DirectoryDeltaTransfer* GetAssetStreamer() {
            return &_assetStreamer;
        }

        MessageRouter router() {
            return MessageRouter(this);
        }

        template <typename T, typename... Args>
        bool send(SLNet::RakNetGUID guid, Args&&... args) {
            T msg(std::forward<Args>(args)...);
            return Send(msg, guid);
        }

        template <typename T, typename... Args>
        bool send(uint64_t guid, Args&&... args) {
            T msg(std::forward<Args>(args)...);
            return Send(msg, guid);
        }

        template <typename T, typename... Args>
        bool sendRPC(SLNet::RakNetGUID guid, Args&&... args) {
            T rpc(std::forward<Args>(args)...);
            return SendRPC(rpc, guid);
        }

        static inline NetworkPeer *_networkRef = nullptr;
    };

    // Binder implementations (must be after NetworkPeer is fully defined)
    template <typename T>
    template <typename Instance>
    void MessageBinder<T>::handle(Instance *inst, void (Instance::*method)(SLNet::RakNetGUID, T *)) {
        T tmp {};
        _peer->RegisterMessage<T>(tmp.GetMessageID(), [inst, method](SLNet::RakNetGUID guid, T *msg) {
            (inst->*method)(guid, msg);
        });
    }

    template <typename T>
    template <typename Instance>
    void RPCBinder<T>::handle(Instance *inst, void (Instance::*method)(SLNet::RakNetGUID, T *)) {
        _peer->RegisterRPC<T>([inst, method](SLNet::RakNetGUID guid, T *rpc) {
            (inst->*method)(guid, rpc);
        });
    }

    template <typename T>
    template <typename Instance>
    void GameRPCBinder<T>::handle(Instance *inst, void (Instance::*method)(SLNet::RakNetGUID, T *)) {
        _peer->RegisterGameRPC<T>([inst, method](SLNet::RakNetGUID guid, T *rpc) {
            (inst->*method)(guid, rpc);
        });
    }
} // namespace Framework::Networking
