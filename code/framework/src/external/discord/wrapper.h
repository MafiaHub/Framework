/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <discord.h>
#include <functional>
#include <string>

namespace Framework::External::Discord {
    class Wrapper final {
      private:
        bool _initialized = false;
        discord::User _user;
        discord::Core *_instance;

      public:
        using DiscordLoginProc = std::function<void(const std::string &token)>;
        Wrapper()              = default;
        DiscordError Init(int64_t id);
        DiscordError Shutdown();

        bool IsInitialized() const {
            return _initialized;
        }

        DiscordError Update();
        DiscordError SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity, const std::string &largeImage, const std::string &largeText,
            const std::string &smallImage, const std::string &smallText);
        DiscordError SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity);

        void SignInWithDiscord(DiscordLoginProc proc);

        discord::ActivityManager &GetActivityManager();
        discord::UserManager &GetUserManager();
        discord::ImageManager &GetImageManager();
        discord::OverlayManager &GetOverlayManager();
        discord::ApplicationManager &GetApplicationManager();
        discord::VoiceManager &GetVoiceManager();
        discord::RelationshipManager &GetRelationshipManager();
    };
} // namespace Framework::External::Discord
