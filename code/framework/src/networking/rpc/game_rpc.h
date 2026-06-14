/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/BitStream.h>
#include <mafianet/MessageIdentifiers.h>
#include <mafianet/types.h>
#include <string>

#include "rpc_identity.h"

#include "world/modules/base.hpp"

namespace Framework::Networking::RPC {
    template <class T>
    class IGameRPC {
      private:
        MafiaNet::Packet *packet {};
        uint32_t _hashName = 0;

      protected:
        flecs::entity_t _serverID = 0;

      public:
        // Identity comes from a compiler-independent type name (NOT typeid().name(),
        // which differs MSVC vs GCC and breaks cross-platform RPC routing); cached
        // per type in RPCHash/RPCName.
        IGameRPC(): _hashName(RPCHash<T>()) {};
        void SetServerID(flecs::entity_t serverID) {
            _serverID = serverID;
        }

        flecs::entity_t GetServerID() const {
            return _serverID;
        }

        const std::string& GetName() const {
            return RPCName<T>();
        }

        virtual void Serialize(MafiaNet::BitStream *bs, bool write) = 0;
        virtual bool Valid() const                               = 0;

        void Serialize2(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, _serverID);
        };

        inline bool ValidServerID() const {
            return _serverID > 0;
        }

        /**
         * Validates if the server id was set.
         * @return
         */
        bool Valid2() const {
            return ValidServerID();
        }

        uint32_t GetHashName() const {
            return _hashName;
        }

        void SetPacket(MafiaNet::Packet *p) {
            packet = p;
        }

        MafiaNet::Packet *GetPacket() const {
            return packet;
        }

        bool IsGameRPC() const {
            return true;
        }
    };
} // namespace Framework::Networking::RPC
