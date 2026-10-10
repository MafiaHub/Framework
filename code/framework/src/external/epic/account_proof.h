/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Framework::External::Epic {
    inline constexpr std::size_t kMaxAccountProofLength = 8192;

    struct VerifiedAccount {
        std::string accountId;
        std::string tokenId;
        int64_t expiresAt {};
    };

    // Only an Epic ownership JWT (egoc1~), never an access or refresh token. The key id is
    // untrusted until VerifyAccountProof verifies the signature with an Epic-published key.
    std::optional<std::string> AccountProofKeyId(std::string_view proof);
    std::optional<VerifiedAccount> VerifyAccountProof(std::string_view proof, std::string_view publicJwk, int64_t now);

    // Fetches the key from Epic's fixed HTTPS endpoint, with certificate verification, bounded
    // response size and a timeout. Call off the tick thread. Never accepts a URL from the token.
    std::optional<VerifiedAccount> VerifyAccountProofOnline(const std::string &proof);
} // namespace Framework::External::Epic
