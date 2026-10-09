/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/result.h"

#include <optional>
#include <string>

// Epic Games account auth for launching an EOS (Epic Store) game outside the Epic launcher.
// Mimics what the Epic launcher itself does: authenticate the user once (browser sign-in ->
// authorizationCode), keep a refresh token, and mint a short-lived single-use "exchange code"
// per launch that goes on the game's command line so its EOS ownership/entitlement check passes.
// Same approach as the open-source "Legendary" launcher. Store-generic — not tied to any game.
namespace Framework::External::Epic {
    struct Tokens {
        std::string accessToken;
        std::string refreshToken;
        std::string accountId;
        std::string displayName;

        bool Valid() const {
            return !accessToken.empty();
        }
    };

    enum class AuthError {
        None,
        NoStoredSignIn,
        Rejected,    // Epic refused the stored sign-in (HTTP 400/401): signed out, expired or revoked
        Unreachable, // no usable answer from Epic
    };
    using AuthResult = Utils::Result<Tokens, AuthError>;

    // Only a missing or rejected sign-in calls for a new one. When Epic can't be reached a new
    // sign-in fails the same way, and the stored one still works once it can.
    inline bool NeedsSignIn(AuthError error) {
        return error == AuthError::NoStoredSignIn || error == AuthError::Rejected;
    }

    // A usable access token: the stored refresh token first, else, when NeedsSignIn, a browser +
    // clipboard sign-in labelled with productName. The refresh token is persisted, DPAPI-encrypted,
    // under %LOCALAPPDATA%\MafiaHub.
    AuthResult EnsureAuthenticated(const std::wstring &productName = {});

    // The silent half of EnsureAuthenticated: refresh the stored token, never show any UI. A rejected
    // stored sign-in is forgotten; one Epic could not be asked about is kept.
    AuthResult TryRefreshStoredAuth();

    // Mint a fresh single-use exchange code from a valid access token (expires in ~5 min).
    std::optional<std::string> GetExchangeCode(const Tokens &tokens);

    // Embedded-webview sign-in: navigate to GetLoginUrl(), then hand the resulting redirect-page
    // text (or a bare authorizationCode) to SignInWithAuthorizationCode to mint + persist tokens.
    // It blocks on the network, so call it off any UI thread. False when nothing was persisted.
    std::wstring GetLoginUrl();
    bool SignInWithAuthorizationCode(const std::string &pageTextOrCode);

    // The "-AUTH_TYPE=exchangecode ..." fragment (leading space) for the game's EOS init. The ids
    // come from the Epic manifest and mint a fresh ownership token (.ovt), without which the game
    // shows the "use the Epic launcher" gate; installDir's .egstore copy is the fallback.
    std::wstring BuildLaunchArgs(const Tokens &tokens, const std::string &exchangeCode, const std::string &appName, const std::string &sandboxId, const std::string &catalogItemId, const std::string &installDir);

    // Forget the stored credentials (e.g. after a hard auth failure so the next launch re-prompts).
    void ClearStoredAuth();
} // namespace Framework::External::Epic
