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

#include <typeinfo>

#include "world/modules/base.hpp"

#include <BitStream.h>

namespace Framework::Networking::RPC {
    template <class T>
    class IGameRPC {
      private:
        SLNet::Packet *packet {};
        uint32_t _hashName = 0;
      protected:
        flecs::entity_t _serverID = 0;

      public:
        IGameRPC(): _hashName(Utils::Hashing::CalculateCRC32(typeid(T).name())) {};
        void SetServerID(flecs::entity_t serverID) {
            _serverID = serverID;
        }

        flecs::entity_t GetServerID() const {
            return _serverID;
        }

        virtual void Serialize(SLNet::BitStream *bs, bool write) = 0;
        virtual bool Valid() const = 0;

        void Serialize2(SLNet::BitStream *bs, bool write) {
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

        void SetPacket(SLNet::Packet *p) {
            packet = p;
        }

        SLNet::Packet *GetPacket() const {
            return packet;
        }

        bool IsGameRPC() {
            return true;
        }
    };
} // namespace Framework::Networking::RPC
