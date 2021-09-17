#pragma once

#include <RakNetTypes.h>

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
