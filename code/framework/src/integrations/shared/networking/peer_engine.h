/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/lifecycle.h>

#include <memory>

namespace Framework::Integrations::Shared::Networking {
    // Common Lifecycle plumbing for the client and server networking engines: owns the peer and
    // forwards Shutdown/Update to it. Derived engines add their peer-specific Init/Connect surface
    // and a typed accessor.
    template <typename TPeer>
    class PeerEngine : public Framework::Lifecycle {
      protected:
        std::unique_ptr<TPeer> _peer = std::make_unique<TPeer>();

      public:
        void Shutdown() override {
            if (_peer) {
                _peer->Shutdown();
            }
            Lifecycle::Shutdown();
        }

        void Update() override {
            if (_peer) {
                _peer->Update();
            }
        }
    };
} // namespace Framework::Integrations::Shared::Networking
