/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <BitStream.h>
#include <RakNetTypes.h>

namespace Framework::Networking::Messages {
    class IMessage {
      public:
        virtual uint8_t GetMessageID() const = 0;

        virtual void Serialize(SLNet::BitStream *bs, bool write) = 0;

        virtual bool Valid() = 0;
    };
} // namespace Framework::Networking::Messages
