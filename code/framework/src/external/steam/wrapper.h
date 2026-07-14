/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/lifecycle.h>

#define VERSION_SAFE_STEAM_API_INTERFACES

#include <utils/error.h>
#include <utils/result.h>

#include <steam_api.h>
#include <string>
#include <memory>

namespace Framework::External::Steam {
    struct AuthTicket {
        EResult status     = k_EResultPending;
        HAuthTicket handle = 0;
        char buffer[1024]  = {0};
        size_t size        = 0;

        bool IsPending() const {
            return status == k_EResultPending;
        }

        bool IsValid() const {
            return status == k_EResultOK;
        }

        bool IsError() const {
            return !IsPending() && !IsValid();
        }
    };
    class Wrapper final : public Framework::Lifecycle {
      private:
        std::unique_ptr<CSteamAPIContext> _ctx;
        AuthTicket _authTicket;
        bool _overlayActive = false;

      public:
        Wrapper() = default;

        ~Wrapper() = default;

        [[nodiscard]] Utils::Result<void, Framework::Error> Init();
        void Shutdown() override;

        CSteamID GetSteamID() const;
        EPersonaState GetSteamUserState() const;
        std::string GetSteamUsername() const;

        const AuthTicket &GetAuthTicket() const {
            return _authTicket;
        }

        // Whether the given Steam app is installed locally (typed wrapper over SteamApps so the common
        // path doesn't need GetContext()).
        [[nodiscard]] bool IsAppInstalled(uint32_t appId) const;

        // Local install directory of the given Steam app, or empty if unavailable.
        [[nodiscard]] std::string GetAppInstallDir(uint32_t appId) const;

        // Escape hatch: the raw Steam API context, for SDK features the wrapper doesn't surface.
        const CSteamAPIContext *GetContext() const {
            return _ctx.get();
        };

        STEAM_CALLBACK(Wrapper, OnGameOverlayActivated, GameOverlayActivated_t);
        STEAM_CALLBACK(Wrapper, OnGetAuthSessionTicketResponse, GetAuthSessionTicketResponse_t);
    };
} // namespace Framework::External::Steam
