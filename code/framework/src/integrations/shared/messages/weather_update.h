/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "messages.h"

#include <BitStream.h>
#include <string>

namespace Framework::Integrations::Shared::Messages {
    class WeatherUpdate final: public Framework::Networking::Messages::IMessage {
      private:
        SLNet::RakString _weatherPreset;
        float _time        = 0.0f;
        bool _updatePreset = false;

      public:
        uint8_t GetMessageID() const override {
            return GAME_SYNC_WEATHER_UPDATE;
        }

        void FromParameters(float time, bool updatePreset = false, std::string preset = "") {
            _time          = time;
            _updatePreset  = updatePreset;
            _weatherPreset = SLNet::RakString(preset.c_str());
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, _time);
            bs->Serialize(write, _updatePreset);

            if (_updatePreset) {
                bs->Serialize(write, _weatherPreset);
            }
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
} // namespace Framework::Integrations::Shared::Messages
