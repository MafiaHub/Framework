/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <firebase/analytics.h>
#include <firebase/analytics/event_names.h>
#include <firebase/analytics/parameter_names.h>
#include <firebase/analytics/user_property_names.h>
#include <firebase/app.h>
#include <firebase/auth.h>
#include <firebase/util.h>
#include <string>

namespace Framework::External::Firebase {
    class Wrapper final: public firebase::auth::AuthStateListener {
      private:
        firebase::App *_app         = nullptr;
        firebase::auth::User *_user = nullptr;

        bool _valid = false;

      private:
        void OnAuthStateChanged(firebase::auth::Auth *) override;

      public:
        FirebaseError Init(const std::string &projectId, const std::string &appId, const std::string &apiKey);

        FirebaseError SignInWithCustomToken(const std::string &);
        FirebaseError SignInWithEmailPassword(const std::string &, const std::string &);
        FirebaseError SignUpWithEmailPassword(const std::string &, const std::string &);
        FirebaseError SignInAnonymously();
        FirebaseError SignOut();

        void LogEvent(const std::string &, const std::string &, const std::string &);
        void LogEvent(const std::string &, const std::string &, const double);
        void LogEvent(const std::string &);
        void SetUserProperty(const std::string &, const std::string &);

        bool IsValid() const {
            return _valid;
        }

        firebase::auth::Auth *GetAuth() const {
            return firebase::auth::Auth::GetAuth(_app);
        }

        firebase::auth::User *GetCurrentUser() const {
            return _user;
        }

        firebase::App *GetApp() {
            return _app;
        }
    };
} // namespace Framework::External::Firebase
