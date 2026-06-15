/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include <logging/logger.h>

namespace Framework::External::Steam {
    Utils::Result<void, Framework::Error> Wrapper::Init() {
        if (!SteamAPI_IsSteamRunning()) {
            return Framework::Error("Steam client is not running");
        }

        if (!SteamAPI_Init()) {
            return Framework::Error("Failed to initialize the Steam API");
        }

        if (!SteamUser()->BLoggedOn()) {
            SteamAPI_Shutdown();
            return Framework::Error("Steam user is not logged on");
        }

        auto ctx = std::make_unique<CSteamAPIContext>();
        if (!ctx->Init()) {
            SteamAPI_Shutdown();
            return Framework::Error("Failed to initialize the Steam API context");
        }

        _ctx         = std::move(ctx);
        _initialized = true;
        return {};
    }

    void Wrapper::Shutdown() {
        SteamAPI_Shutdown();
        Lifecycle::Shutdown();
    }

    bool Wrapper::IsAppInstalled(uint32_t appId) const {
        if (!_ctx) {
            return false;
        }
        ISteamApps *apps = _ctx->SteamApps();
        return apps && apps->BIsAppInstalled(appId);
    }

    std::string Wrapper::GetAppInstallDir(uint32_t appId) const {
        if (!_ctx) {
            return {};
        }
        ISteamApps *apps = _ctx->SteamApps();
        if (!apps) {
            return {};
        }
        char path[1024] = {0};
        const auto length = apps->GetAppInstallDir(appId, path, sizeof(path));
        if (length == 0 || length >= sizeof(path)) {
            return {};
        }
        return path;
    }

    CSteamID Wrapper::GetSteamID() const {
        if (!_ctx) {
            return CSteamID();
        }

        return _ctx->SteamUser()->GetSteamID();
    }

    std::string Wrapper::GetSteamUsername() const {
        if (!_ctx) {
            return std::string();
        }

        return _ctx->SteamFriends()->GetPersonaName();
    }

    EPersonaState Wrapper::GetSteamUserState() const {
        if (!_ctx) {
            return k_EPersonaStateOffline;
        }

        return _ctx->SteamFriends()->GetPersonaState();
    }

    void Wrapper::OnGameOverlayActivated(GameOverlayActivated_t *pParam) {
        _overlayActive = pParam->m_bActive;
    }

    void Wrapper::OnGetAuthSessionTicketResponse(GetAuthSessionTicketResponse_t *pParam) {
        _authTicket._status = pParam->m_eResult;
    }
} // namespace Framework::External::Steam
