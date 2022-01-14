/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#define VERSION_SAFE_STEAM_API_INTERFACES

#include "errors.h"

#include <steam_api.h>
#include <string>

namespace Framework::External::Steam {
    struct AuthTicket {
        EResult _status     = k_EResultPending;
        HAuthTicket _handle = 0;
        char _buffer[1024]  = {0};
        size_t _size        = 0;

        bool IsPending() const {
            return _status == k_EResultPending;
        }

        bool IsValid() const {
            return _status == k_EResultOK;
        }

        bool IsError() const {
            return !IsPending() && !IsValid();
        }
    };
    class Wrapper final {
      private:
        CSteamAPIContext *_ctx = nullptr;
        AuthTicket _authTicket;
        bool _overlayActive = false;

      public:
        Wrapper() = default;

        ~Wrapper() = default;

        SteamError Init();
        SteamError Shutdown();

        CSteamID GetSteamID() const;
        EPersonaState GetSteamUserState() const;
        std::string GetSteamUsername() const;

        const AuthTicket &GetAuthTicket() const {
            return _authTicket;
        }

        const CSteamAPIContext *GetContext() const {
            return _ctx;
        };

        STEAM_CALLBACK(Wrapper, OnGameOverlayActivated, GameOverlayActivated_t);
        STEAM_CALLBACK(Wrapper, OnGetAuthSessionTicketResponse, GetAuthSessionTicketResponse_t);
    };
} // namespace Framework::External::Steam
