#pragma once

#include <discord.h>
#include <string>

namespace Framework::Integrations {
    class Discord final {
      private:
        discord::User _user;
        discord::Core *_instance;

      public:
        Discord() = default;
        bool Init(int64_t id);
        bool Shutdown();

        void Update();

        void SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity,
                         const std::string &largeImage, const std::string &largeText, const std::string &smallImage,
                         const std::string &smallText);
        void SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity);

        discord::ActivityManager &GetActivityManager();
        discord::UserManager &GetUserManager();
        discord::ImageManager &GetImageManager();
        discord::OverlayManager &GetOverlayManager();
        discord::ApplicationManager &GetApplicationManager();
        discord::VoiceManager &GetVoiceManager();
        discord::RelationshipManager &GetRelationshipManager();
    };
} // namespace Framework::Integrations
