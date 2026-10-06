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
#include <function2/function2.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace Framework::External::Discord {
    // Activity fields as bits; an image bit includes its tooltip.
    namespace PresenceField {
        constexpr uint32_t Type       = 1u << 0;
        constexpr uint32_t Name       = 1u << 1;
        constexpr uint32_t Details    = 1u << 2;
        constexpr uint32_t State      = 1u << 3;
        constexpr uint32_t Timestamps = 1u << 4;
        constexpr uint32_t LargeImage = 1u << 5;
        constexpr uint32_t SmallImage = 1u << 6;
        constexpr uint32_t Party      = 1u << 7;
        constexpr uint32_t Secrets    = 1u << 8;
        constexpr uint32_t Instance   = 1u << 9;
        constexpr uint32_t Platforms  = 1u << 10;
        constexpr uint32_t All        = (1u << 11) - 1;
    } // namespace PresenceField

    class Wrapper final : public Framework::Lifecycle {
      private:
        discord::User _user {};
        discord::Core *_instance {};

        // Published activity = base, overridden by the script fields that _scriptFields allows.
        mutable std::mutex _layersMutex;
        std::optional<discord::Activity> _baseActivity;
        discord::Activity _scriptActivity {};
        uint32_t _scriptWritten = 0;
        uint32_t _scriptFields  = PresenceField::All;
        std::optional<discord::Activity> _publishedActivity;

        void PublishLayers();

      public:
        using DiscordLoginProc = fu2::function<void(const std::string &token) const>;
        Wrapper()              = default;
        [[nodiscard]] Utils::Result<void, Framework::Error> Init(int64_t id);
        void Shutdown() override;

        void Update() override;
        Utils::Result<void, Framework::Error> SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity, const std::string &largeImage, const std::string &largeText, const std::string &smallImage, const std::string &smallText) const;
        Utils::Result<void, Framework::Error> SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity) const;

        // Publish a fully-composed activity (the entire rich-presence surface); use when the
        // SetPresence shortcuts above are too coarse. Null-safe.
        Utils::Result<void, Framework::Error> UpdateActivity(const discord::Activity &activity) const;
        // Clear the local player's activity entirely.
        Utils::Result<void, Framework::Error> ClearActivity() const;

        // Layered presence: the mod sets a base activity and the PresenceField mask scripts may
        // override. By default there is no base and every field is open.
        void SetBaseActivity(const discord::Activity &activity);
        void SetScriptFields(uint32_t fields);
        // `written` is the PresenceField mask the script staged.
        void SetScriptActivity(const discord::Activity &activity, uint32_t written);
        void ClearScriptActivity();

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
