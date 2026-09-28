/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/GetTime.h>
#include <mafianet/StatisticsHistory.h>

namespace Framework::Networking {
    // Receive() updates plugins once per drained packet. History needs a fixed
    // sampling cadence, while the peer's live counters remain unchanged.
    class SampledStatisticsHistory final: public MafiaNet::StatisticsHistoryPlugin {
      public:
        static constexpr MafiaNet::Time kSampleIntervalMs = 100;

        void Update() override {
            UpdateAt(MafiaNet::GetTime());
        }
        bool UpdateAt(MafiaNet::Time now) {
            if (_sampled && now >= _lastSample && now - _lastSample < kSampleIntervalMs) {
                return false;
            }
            _sampled    = true;
            _lastSample = now;
            MafiaNet::StatisticsHistoryPlugin::Update();
            return true;
        }
        void OnRakPeerShutdown() override {
            MafiaNet::StatisticsHistoryPlugin::OnRakPeerShutdown();
            _sampled = false;
        }

      private:
        MafiaNet::Time _lastSample = 0;
        bool _sampled              = false;
    };
} // namespace Framework::Networking
