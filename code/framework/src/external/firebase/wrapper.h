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
        firebase::App *_app;
        firebase::auth::User *_user;

        bool _valid = false;

      private:
        void OnAuthStateChanged(firebase::auth::Auth *) override;

      public:
        FirebaseError Init(const std::string &, const std::string &, const std::string &);

        FirebaseError SignInWithCustomToken(const std::string &);
        FirebaseError SignInWithEmailPassword(const std::string &, const std::string &);
        FirebaseError SignUpWithEmailPassword(const std::string &, const std::string &);
        FirebaseError SignInAnonymously();
        FirebaseError SignOut();

        void LogEvent(const std::string &, const std::string &, const std::string &);
        void LogEvent(const std::string &, const std::string &, const double);
        void LogEvent(const std::string &);

        bool IsValid() const {
            return _valid;
        }

        firebase::auth::Auth *GetAuth() const {
            return firebase::auth::Auth::GetAuth(_app);
        }

        firebase::auth::User *GetCurrentUser() const {
            return _user;
        }

        firebase::App* GetApp() {
            return _app;
        }
    };
} // namespace Framework::Integrations
