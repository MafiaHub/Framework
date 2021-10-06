/*
 * MafiaHub OSS license
 * Copyright (c) 2021 MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <RakNetTypes.h>
#include <BitStream.h>

namespace Framework::Networking::Messages {
    class IMessage {
      public:
        virtual uint32_t GetMessageID() const = 0;

        virtual void FromPacket(SLNet::Packet *packet) {
            SLNet::BitStream bsIn(packet->data, packet->length, false);
            bsIn.IgnoreBytes(sizeof(SLNet::MessageID));
            FromBitStream(&bsIn);
        };

        virtual void FromBitStream(SLNet::BitStream *) = 0;

        virtual SLNet::BitStream *ToBitStream(SLNet::BitStream *) = 0;

        virtual bool Valid() = 0;
    };
} // namespace Framework::Networking::Messages
