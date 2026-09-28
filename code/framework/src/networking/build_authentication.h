/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/GetTime.h>
#include <mafianet/MessageIdentifiers.h>
#include <mafianet/TwoWayAuthentication.h>

namespace Framework::Networking {
    // Keep MafiaNet's challenge/response verification and disconnect cleanup,
    // but allow busy peers 30 seconds to complete it. Both the outgoing
    // challenge and server nonce must survive that window: extending only the
    // former still loses the server's nonce after MafiaNet's five-second TTL.
    class BuildAuthentication: public MafiaNet::TwoWayAuthentication {
      public:
        static constexpr MafiaNet::Time kTimeoutMs = 30000;

        void Update() override {
            UpdateAt(MafiaNet::GetTime());
        }

        void UpdateAt(MafiaNet::Time now) {
            while (!outgoingChallenges.IsEmpty()) {
                const auto &challenge = outgoingChallenges.Peek();
                if (now < challenge.time || now - challenge.time < kTimeoutMs) {
                    break;
                }
                const auto expired = outgoingChallenges.Pop();
                PushToUser(ID_TWO_WAY_AUTHENTICATION_OUTGOING_CHALLENGE_TIMEOUT, expired.identifier, expired.remoteSystem);
            }
            auto &nonces = nonceGenerator.generatedNonces;
            while (nonces.Size() != 0) {
                const auto *nonce = nonces[0];
                if (now < nonce->whenGenerated || now - nonce->whenGenerated < kTimeoutMs) {
                    break;
                }
                MafiaNet::OP_DELETE(nonces[0], _FILE_AND_LINE_);
                nonces.RemoveAtIndex(0);
            }
        }
    };
} // namespace Framework::Networking
