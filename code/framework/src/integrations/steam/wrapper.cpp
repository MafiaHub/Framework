#include "wrapper.h"

#include <logging/logger.h>

namespace Framework::Integrations::Steam {
    WrapperError Wrapper::Init() {
        if (!SteamAPI_IsSteamRunning()) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Steam API is not running");
            return WRAPPER_ERROR_STEAM_NOT_RUNNING;
        }

        if (!SteamAPI_InitSafe()) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to init steam API");
            return WRAPPER_ERROR_INIT_STEAM;
        }

        CSteamAPIContext *ctx = new CSteamAPIContext();
        _ctx                  = ctx;

        if (!_ctx) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to create steam api context");
            return WRAPPER_ERROR_CONTEXT_CREATE;
        }

        if (!_ctx->Init()) {
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to init steam context");
            return WRAPPER_ERROR_CONTEXT_INIT;
        }

        return WRAPPER_ERROR_NONE;
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
} // namespace Framework::Integrations::Steam
