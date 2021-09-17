#pragma once

#include <firebase/analytics.h>
#include <firebase/analytics/event_names.h>
#include <firebase/analytics/parameter_names.h>
#include <firebase/analytics/user_property_names.h>
#include <firebase/app.h>
#include <firebase/auth.h>
#include <firebase/util.h>
#include <string>


namespace Framework::Integrations {
    class Firebase final: public firebase::auth::AuthStateListener {
      private:
        firebase::App *_app;
        firebase::auth::User *_user;

        bool _valid = false;

      private:
        void OnAuthStateChanged(firebase::auth::Auth *) override;

      public:
        bool Init(const std::string &, const std::string &, const std::string &);

        bool SignInWithEmailPassword(const std::string &, const std::string &);
        bool SignUpWithEmailPassword(const std::string &, const std::string &);
        bool SignInAnonymously();
        bool SignOut();

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
    };
} // namespace Framework::Integrations
