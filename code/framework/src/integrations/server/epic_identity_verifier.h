/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <external/epic/account_proof.h>
#include <networking/rpc/client_identity.h>

#include <functional>
#include <future>
#include <unordered_map>
#include <vector>

namespace Framework::Integrations::Server {
    class EpicIdentityVerifier final {
      public:
        using Verify = std::function<std::optional<External::Epic::VerifiedAccount>(const std::string &)>;
        struct Decision {
            MafiaNet::RakNetGUID guid;
            Networking::RPC::ClientIdentity identity;
            bool verified {};
        };

        // A bounded number of off-thread requests; cancelled requests still occupy their worker
        // slot until done, so disconnect/reconnect cannot create unlimited verification threads.
        explicit EpicIdentityVerifier(Verify verify = External::Epic::VerifyAccountProofOnline): _verify(std::move(verify)) {}
        bool Begin(MafiaNet::RakNetGUID guid, Networking::RPC::ClientIdentity identity);
        void Drop(uint64_t guid);
        void Collect(std::vector<Decision> &out);
        void Shutdown();

      private:
        struct Pending {
            MafiaNet::RakNetGUID guid;
            Networking::RPC::ClientIdentity identity;
            std::future<std::optional<External::Epic::VerifiedAccount>> result;
            bool abandoned {};
        };
        Verify _verify;
        std::vector<Pending> _pending;
        std::unordered_map<std::string, int64_t> _usedTokens;
    };
} // namespace Framework::Integrations::Server
