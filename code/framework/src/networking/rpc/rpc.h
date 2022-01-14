/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
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

namespace Framework::Networking::RPC {
    class IRPC {
      private:
        SLNet::Packet *packet;
        uint32_t _hashName;

      public:
        IRPC(const std::string &name): _hashName(Utils::Hashing::CalculateCRC32(name.c_str(), name.length())) {};

        virtual void Serialize(SLNet::BitStream *bs, bool write) = 0;
        virtual bool Valid()                                     = 0;

        uint32_t GetHashName() const {
            return _hashName;
        }

        void SetPacket(SLNet::Packet *p) {
            packet = p;
        }

        SLNet::Packet *GetPacket() const {
            return packet;
        }
    };
} // namespace Framework::Networking::RPC
