#pragma once

namespace Framework::Integrations::Steam {
    enum SteamError {
        STEAM_NONE,
        STEAM_CLIENT_NOT_RUNNING,
        STEAM_INIT_FAILED,
        STEAM_CONTEXT_CREATION_FAILED,
        STEAM_CONTEXT_INIT_FAILED
    };
}
