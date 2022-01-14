/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include <logging/logger.h>

namespace Framework::External::Firebase {
    FirebaseError Wrapper::Init(const std::string &projectId, const std::string &appId, const std::string &apiKey) {
        // Init the firebase options
        firebase::AppOptions opts;
        opts.set_api_key(apiKey.c_str());
        opts.set_project_id(projectId.c_str());
        opts.set_app_id(appId.c_str());

        // Create the application
        _app = firebase::App::Create(opts);

        // Create the initializer and initialize
        firebase::ModuleInitializer initializer;
        initializer.Initialize(_app, nullptr, [](firebase::App *app, void *) -> firebase::InitResult {
            // Initialize the components that we are going to use later on
            firebase::analytics::Initialize(*app);

            firebase::InitResult authRes;
            firebase::auth::Auth::GetAuth(app, &authRes);
            return authRes;
        });

        // Since it's an async initializer, we got to wait for it to complete
        while (initializer.InitializeLastResult().status() != firebase::kFutureStatusComplete) {}

        // Was everything correctly initialized?
        if (initializer.InitializeLastResult().error() != 0) {
            Logging::GetInstance()->Get(FRAMEWORK_INNER_INTEGRATIONS)->debug("[Firebase] FAILED TO INITIALIZE: {}", initializer.InitializeLastResult().error_message());
            return FirebaseError::FIREBASE_INITIALIZE_FAILED;
        }

        // Mark the current class as an auth state listener
        auto auth = GetAuth();
        auth->AddAuthStateListener(this);

        // Configure the analytics
        firebase::analytics::SetAnalyticsCollectionEnabled(true);
        firebase::analytics::SetSessionTimeoutDuration(1000 * 60 * 30);

        Logging::GetInstance()->Get(FRAMEWORK_INNER_INTEGRATIONS)->debug("[Firebase] Initialize complete");
        return FirebaseError::FIREBASE_NONE;
    }

    FirebaseError Wrapper::SignInWithCustomToken(const std::string &token) {
        auto auth                                       = GetAuth();
        firebase::Future<firebase::auth::User *> result = auth->SignInWithCustomToken(token.c_str());
        while (result.status() != firebase::kFutureStatusComplete) {};
        if (result.error() == firebase::auth::kAuthErrorNone) {
            return FirebaseError::FIREBASE_NONE;
        }
        else {
            return FirebaseError::FIREBASE_AUTH_FAILED;
        }
    }

    FirebaseError Wrapper::SignInWithEmailPassword(const std::string &email, const std::string &password) {
        auto auth                                       = GetAuth();
        firebase::Future<firebase::auth::User *> result = auth->SignInWithEmailAndPassword(email.c_str(), password.c_str());
        while (result.status() != firebase::kFutureStatusComplete) {};
        if (result.error() == firebase::auth::kAuthErrorNone) {
            return FirebaseError::FIREBASE_NONE;
        }
        else {
            return FirebaseError::FIREBASE_AUTH_FAILED;
        }
    }

    FirebaseError Wrapper::SignUpWithEmailPassword(const std::string &email, const std::string &password) {
        auto auth                                       = GetAuth();
        firebase::Future<firebase::auth::User *> result = auth->CreateUserWithEmailAndPassword(email.c_str(), password.c_str());
        while (result.status() != firebase::kFutureStatusComplete) {};
        if (result.error() == firebase::auth::kAuthErrorNone) {
            return FirebaseError::FIREBASE_NONE;
        }
        else {
            return FirebaseError::FIREBASE_AUTH_FAILED;
        }
    }

    FirebaseError Wrapper::SignInAnonymously() {
        auto auth                                       = GetAuth();
        firebase::Future<firebase::auth::User *> result = auth->SignInAnonymously();
        while (result.status() != firebase::kFutureStatusComplete) {};
        if (result.error() == firebase::auth::kAuthErrorNone) {
            return FirebaseError::FIREBASE_NONE;
        }
        else {
            return FirebaseError::FIREBASE_AUTH_FAILED;
        }
    }

    FirebaseError Wrapper::SignOut() {
        auto auth = GetAuth();
        auth->SignOut();
        return FirebaseError::FIREBASE_NONE;
    }

    void Wrapper::SetUserProperty(const std::string &name, const std::string &property) {
        firebase::analytics::SetUserProperty(name.c_str(), property.c_str());
    }

    void LogEvent(const std::string &name, const std::string &paramName, const std::string &paramValue) {
        firebase::analytics::LogEvent(name.c_str(), paramName.c_str(), paramValue.c_str());
    }

    void LogEvent(const std::string &name, const std::string &paramName, const double paramValue) {
        firebase::analytics::LogEvent(name.c_str(), paramName.c_str(), paramValue);
    }

    void LogEvent(const std::string &name) {
        firebase::analytics::LogEvent(name.c_str());
    }

    void Wrapper::OnAuthStateChanged(firebase::auth::Auth *auth) {
        _user = auth->current_user();
        if (!_user) {
            firebase::analytics::SetUserId(nullptr);
            Logging::GetInstance()->Get(FRAMEWORK_INNER_INTEGRATIONS)->debug("[Firebase] AuthStateChanged: user null");
            return;
        }

        firebase::analytics::SetUserId(_user->uid().c_str());

        Logging::GetInstance()->Get(FRAMEWORK_INNER_INTEGRATIONS)->debug("[Firebase] AuthStateChanged: user active {}", _user->uid().c_str());
    }
} // namespace Framework::External::Firebase
