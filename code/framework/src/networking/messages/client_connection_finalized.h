#pragma once

#include "message.h"

#include <BitStream.h>

namespace Framework::Networking::Messages {
    class ClientConnectionFinalized final: public IMessage {
      private:
        float _serverTickRate = 0.0f;

      public:
        uint32_t GetMessageID() const override {
            return GAME_CONNECTION_FINALIZED;
        }

        void FromParameters(float tickRate) {
            _serverTickRate = tickRate;
        }

        void FromBitStream(SLNet::BitStream *stream) override {
            stream->Read(_serverTickRate);
        }

        SLNet::BitStream *ToBitStream(SLNet::BitStream *stream) override {
            stream->Write(_serverTickRate);
            return stream;
        }

        bool Valid() override {
            return _serverTickRate > 0.0f;
        }

        float GetServerTickRate() const {
            return _serverTickRate;
        }
    };
} // namespace Framework::Networking::Messages
