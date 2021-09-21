#pragma once

namespace Framework::Integrations::Firebase {
    enum class FirebaseError {
        FIREBASE_NONE,
        FIREBASE_INITIALIZE_FAILED,
        FIREBASE_AUTH_FAILED
    };
}