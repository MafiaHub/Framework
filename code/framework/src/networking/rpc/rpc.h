/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <RakNetTypes.h>
#include <string>
#include <utils/hashing.h>

#include <typeinfo>

namespace Framework::Networking::RPC {
    template <class T>
    class IRPC {
      private:
        SLNet::Packet *packet {};
        uint32_t _hashName = 0;

      public:
        virtual ~IRPC() = default;
        IRPC(): _hashName(Utils::Hashing::CalculateCRC32(typeid(T).name())) {};

        virtual void Serialize(SLNet::BitStream *bs, bool write) = 0;
        virtual bool Valid() const                               = 0;

        uint32_t GetHashName() const {
            return _hashName;
        }

        void SetPacket(SLNet::Packet *p) {
            packet = p;
        }

        SLNet::Packet *GetPacket() const {
            return packet;
        }

        bool IsGameRPC() {
            return false;
        }
    };
} // namespace Framework::Networking::RPC
