#pragma once

namespace Framework::Integrations {
    enum class DiscordError {
        DISCORD_NONE,
        DISCORD_CORE_CREATE_FAILED,
        DISCORD_CORE_NULL_INSTANCE
    };
}