/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once
#include <utils/safe_win32.h>

#include "errors.h"

#include <discord.h>
#include <function2.hpp>
#include <string>

namespace Framework::External::Discord {
    class Wrapper final {
      private:
        bool _initialized = false;
        discord::User _user {};
        discord::Core *_instance {};

      public:
        using DiscordLoginProc = fu2::function<void(const std::string &token) const>;
        Wrapper()              = default;
        DiscordError Init(int64_t id);
        DiscordError Shutdown();

        bool IsInitialized() const {
            return _initialized;
        }

        DiscordError Update() const;
        DiscordError SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity, const std::string &largeImage, const std::string &largeText, const std::string &smallImage, const std::string &smallText) const;
        DiscordError SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity) const;

        void SignInWithDiscord(const DiscordLoginProc &proc) const;

        discord::ActivityManager &GetActivityManager() const;
        discord::UserManager &GetUserManager() const;
        discord::ImageManager &GetImageManager() const;
        discord::OverlayManager &GetOverlayManager() const;
        discord::ApplicationManager &GetApplicationManager() const;
        discord::VoiceManager &GetVoiceManager() const;
        discord::RelationshipManager &GetRelationshipManager() const;
    };
} // namespace Framework::External::Discord
