/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

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

    // Ensure we hold a usable access token: try the stored refresh token first, and only fall
    // back to an interactive browser sign-in when that's missing/expired. productName labels the
    // sign-in dialog (e.g. the project name). Returns false if the user cancels or auth fails.
    // On success the refresh token is (re)persisted, DPAPI-encrypted, next to the launcher exe.
    bool EnsureAuthenticated(Tokens &out, const std::wstring &productName = {});

    // Mint a fresh single-use exchange code from a valid access token (expires in ~5 min).
    bool GetExchangeCode(const Tokens &tokens, std::string &outCode);

    // Embedded-webview sign-in: navigate to GetLoginUrl(), then hand the resulting redirect-page
    // text (or a bare authorizationCode) to SignInWithAuthorizationCode to mint + persist tokens.
    std::wstring GetLoginUrl();
    bool SignInWithAuthorizationCode(const std::string &pageTextOrCode);

    // Build the "-AUTH_TYPE=exchangecode ..." command-line fragment (leading space) the game's
    // EOS init consumes. appName/sandboxId come from the game's Epic manifest (AppName /
    // CatalogNamespace); installDir is the game root, used to locate the ownership-verification
    // token (.ovt) the game checks — without it the Epic build shows the "use the Epic launcher"
    // gate. See Framework::External::Epic::FindInstalledApp for the manifest fields.
    std::wstring BuildLaunchArgs(const Tokens &tokens, const std::string &exchangeCode,
                                 const std::string &appName, const std::string &sandboxId,
                                 const std::string &installDir);

    // Forget the stored credentials (e.g. after a hard auth failure so the next launch re-prompts).
    void ClearStoredAuth();
} // namespace Framework::External::Epic
