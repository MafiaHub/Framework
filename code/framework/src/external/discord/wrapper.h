/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once
#include <utils/safe_win32.h>
#include <utils/lifecycle.h>

#include "errors.h"

#include <discord.h>
#include <function2.hpp>
#include <string>

namespace Framework::External::Discord {
    class Wrapper final : public Framework::Lifecycle {
      private:
        discord::User _user {};
        discord::Core *_instance {};

      public:
        using DiscordLoginProc = fu2::function<void(const std::string &token) const>;
        Wrapper()              = default;
        [[nodiscard]] bool Init(int64_t id);
        void Shutdown() override;

        void Update() override;
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
