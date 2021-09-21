#pragma once

#include "errors.h"

#include <discord.h>
#include <string>

namespace Framework::Integrations::Discord {
    class Wrapper final {
      private:
        discord::User _user;
        discord::Core *_instance;

      public:
        Wrapper() = default;
        DiscordError Init(int64_t id);
        DiscordError Shutdown();

        DiscordError Update();
        DiscordError SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity, const std::string &largeImage, const std::string &largeText,
                                 const std::string &smallImage, const std::string &smallText);
        DiscordError SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity);

        discord::ActivityManager &GetActivityManager();
        discord::UserManager &GetUserManager();
        discord::ImageManager &GetImageManager();
        discord::OverlayManager &GetOverlayManager();
        discord::ApplicationManager &GetApplicationManager();
        discord::VoiceManager &GetVoiceManager();
        discord::RelationshipManager &GetRelationshipManager();
    };
} // namespace Framework::Integrations
