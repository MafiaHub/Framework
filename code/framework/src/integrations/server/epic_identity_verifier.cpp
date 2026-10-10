/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "epic_identity_verifier.h"

#include <algorithm>
#include <chrono>

namespace Framework::Integrations::Server {
    bool EpicIdentityVerifier::Begin(MafiaNet::RakNetGUID guid, Networking::RPC::ClientIdentity identity) {
        if (_pending.size() >= 16 || !External::Epic::AccountProofKeyId(identity.epicProof) || std::ranges::any_of(_pending, [guid](const Pending &pending) {
                return pending.guid == guid;
            }))
            return false;
        auto proof = std::move(identity.epicProof);
        identity.epicProof.clear();
        try {
            auto result = std::async(std::launch::async, [verify = _verify, proof = std::move(proof)] {
                try {
                    return verify(proof);
                }
                catch (const std::exception &) {
                    return std::optional<External::Epic::VerifiedAccount> {};
                }
            });
            _pending.push_back({guid, std::move(identity), std::move(result), false});
        }
        catch (const std::exception &) {
            return false;
        }
        return true;
    }

    void EpicIdentityVerifier::Drop(uint64_t guid) {
        for (auto &pending : _pending) {
            if (pending.guid.g == guid)
                pending.abandoned = true;
        }
    }

    void EpicIdentityVerifier::Collect(std::vector<Decision> &out) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::erase_if(_usedTokens, [now](const auto &entry) {
            return entry.second <= now;
        });
        for (auto it = _pending.begin(); it != _pending.end();) {
            if (it->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            const auto result = it->result.get();
            if (!it->abandoned) {
                Decision decision {it->guid, std::move(it->identity), false};
                if (result && result->expiresAt > now && (decision.identity.epicId.empty() || decision.identity.epicId == result->accountId) && _usedTokens.size() < 4096 && !_usedTokens.contains(result->tokenId)) {
                    decision.identity.epicId = result->accountId;
                    decision.verified        = true;
                    _usedTokens.emplace(result->tokenId, result->expiresAt);
                }
                else {
                    decision.identity.epicId.clear();
                }
                out.push_back(std::move(decision));
            }
            it = _pending.erase(it);
        }
    }

    void EpicIdentityVerifier::Shutdown() {
        // Workers capture their inputs and verifier by value, never the instance or networking.
        // Joining them before teardown keeps both code and process services alive until they finish.
        _pending.clear();
        _usedTokens.clear();
    }
} // namespace Framework::Integrations::Server
