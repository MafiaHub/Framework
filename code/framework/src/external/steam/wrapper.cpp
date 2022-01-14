/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include <logging/logger.h>

namespace Framework::External::Steam {
    SteamError Wrapper::Init() {
        if (!SteamAPI_IsSteamRunning()) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Steam API is not running");
            return SteamError::STEAM_CLIENT_NOT_RUNNING;
        }

        if (!SteamAPI_Init()) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to init steam API");
            return SteamError::STEAM_INIT_FAILED;
        }

        if (!SteamUser()->BLoggedOn()) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("User is not logged on");
            return SteamError::STEAM_USER_NOT_LOGGED_ON;
        }

        CSteamAPIContext *ctx = new CSteamAPIContext();
        _ctx                  = ctx;

        if (!_ctx) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to create steam api context");
            return SteamError::STEAM_CONTEXT_CREATION_FAILED;
        }

        if (!_ctx->Init()) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to init steam context");
            return SteamError::STEAM_CONTEXT_INIT_FAILED;
        }

        return SteamError::STEAM_NONE;
    }

    SteamError Wrapper::Shutdown() {
        SteamAPI_Shutdown();
        return SteamError::STEAM_NONE;
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
            return EPersonaState::k_EPersonaStateOffline;
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
