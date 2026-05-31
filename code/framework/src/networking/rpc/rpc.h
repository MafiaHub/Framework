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
#include <utils/hashing.h>

#include <typeinfo>

namespace Framework::Networking::RPC {
    template <class T>
    class IRPC {
      private:
        MafiaNet::Packet *packet {};
        uint32_t _hashName = 0;
        std::string _rpcName;

      public:
        virtual ~IRPC() = default;
        IRPC(): _rpcName(typeid(T).name()), _hashName(Utils::Hashing::CalculateCRC32(typeid(T).name())) {};

        virtual void Serialize(MafiaNet::BitStream *bs, bool write) = 0;
        virtual bool Valid() const                               = 0;

        uint32_t GetHashName() const {
            return _hashName;
        }

        const std::string &GetName() const {
            return _rpcName;
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
