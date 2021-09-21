#pragma once

namespace Framework::Integrations::Sentry {
    enum class SentryError {
        SENTRY_NONE,
        SENTRY_BREAKPAD_NOT_FOUND,
        SENTRY_CACHE_DIRECTORY_CREATION_FAILED,
        SENTRY_INIT_FAILED,
        SENTRY_INVALID_INSTANCE
    };
}