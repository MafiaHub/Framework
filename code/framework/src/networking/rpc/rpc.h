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

namespace Framework::Networking::RPC {
    template <class T>
    class IRPC {
      private:
        MafiaNet::Packet *packet {};
        uint32_t _hashName = 0;

      public:
        virtual ~IRPC() = default;
        // Identity comes from a compiler-independent type name (NOT typeid().name(),
        // which differs MSVC vs GCC and breaks cross-platform RPC routing); cached
        // per type in RPCHash/RPCName.
        IRPC(): _hashName(RPCHash<T>()) {};

        virtual void Serialize(MafiaNet::BitStream *bs, bool write) = 0;
        virtual bool Valid() const                               = 0;

        uint32_t GetHashName() const {
            return _hashName;
        }

        const std::string &GetName() const {
            return RPCName<T>();
        }

        void SetPacket(MafiaNet::Packet *p) {
            packet = p;
        }

        MafiaNet::Packet *GetPacket() const {
            return packet;
        }

        bool IsGameRPC() const {
            return false;
        }
    };
} // namespace Framework::Networking::RPC
