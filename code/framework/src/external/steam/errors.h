#pragma once

namespace Framework::External::Steam {
    enum class SteamError { STEAM_NONE, STEAM_CLIENT_NOT_RUNNING, STEAM_INIT_FAILED, STEAM_CONTEXT_CREATION_FAILED, STEAM_CONTEXT_INIT_FAILED };
}
