/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "message.h"

#include <BitStream.h>
#include <string>

namespace Framework::Networking::Messages {
    class WeatherUpdate final: public IMessage {
      private:
        SLNet::RakString _weatherPreset;
        float _time        = 0.0f;
        bool _updatePreset = false;

      public:
        uint32_t GetMessageID() const override {
            return GAME_SYNC_WEATHER_UPDATE;
        }

        void FromParameters(float time, bool updatePreset = false, std::string preset = "") {
            _time          = time;
            _updatePreset  = updatePreset;
            _weatherPreset = SLNet::RakString(preset.c_str());
        }

        void FromBitStream(SLNet::BitStream *stream) override {
            stream->Read(_time);
            stream->Read(_updatePreset);

            if (_updatePreset) {
                stream->Read(_weatherPreset);
            }
        }

        SLNet::BitStream *ToBitStream(SLNet::BitStream *stream) override {
            stream->Write(_time);
            stream->Write(_updatePreset);

            if (_weatherPreset) {
                stream->Write(_weatherPreset);
            }
            return stream;
        }

        bool Valid() override {
            return (_updatePreset && !_weatherPreset.IsEmpty()) && _time > 0.0f;
        }

        float GetTime() const {
            return _time;
        }

        std::string GetWeatherPreset() const {
            return _weatherPreset.C_String();
        }

        bool WasWeatherPresetUpdated() const {
            return _updatePreset;
        }
    };
} // namespace Framework::Networking::Messages
