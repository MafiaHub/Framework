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

#include <utils/error.h>
#include <utils/result.h>

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
        [[nodiscard]] Utils::Result<void, Framework::Error> Init(int64_t id);
        void Shutdown() override;

        void Update() override;
        Utils::Result<void, Framework::Error> SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity, const std::string &largeImage, const std::string &largeText, const std::string &smallImage, const std::string &smallText) const;
        Utils::Result<void, Framework::Error> SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity) const;

        void SignInWithDiscord(const DiscordLoginProc &proc) const;

        // Snowflake of the signed-in user once OnCurrentUserUpdate has fired, empty otherwise.
        std::string GetUserId() const;

        // Escape hatches: the raw Discord SDK managers, for SDK features the wrapper doesn't surface.
        // Prefer SetPresence / SignInWithDiscord / GetUserId above.
        discord::ActivityManager &GetActivityManager() const;
        discord::UserManager &GetUserManager() const;
        discord::ImageManager &GetImageManager() const;
        discord::OverlayManager &GetOverlayManager() const;
        discord::ApplicationManager &GetApplicationManager() const;
        discord::VoiceManager &GetVoiceManager() const;
        discord::RelationshipManager &GetRelationshipManager() const;
    };
} // namespace Framework::External::Discord
