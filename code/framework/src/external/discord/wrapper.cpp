/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */
#include "wrapper.h"

#include <logging/logger.h>

namespace Framework::External::Discord {
    DiscordError Wrapper::Init(int64_t id) {
        const auto result = discord::Core::Create(id, DiscordCreateFlags_NoRequireDiscord, &_instance);
        if (result != discord::Result::Ok) {
            return DiscordError::DISCORD_CORE_CREATE_FAILED;
        }

        _instance->UserManager().OnCurrentUserUpdate.Connect([this]() {
            _instance->UserManager().GetCurrentUser(&_user);
            Logging::GetInstance()->Get(FRAMEWORK_INNER_INTEGRATIONS)->debug("[Discord] Current user updated {} ({})", _user.GetUsername(), _user.GetId());
        });

        _initialized = true;
        return DiscordError::DISCORD_NONE;
    }

    DiscordError Wrapper::Shutdown() {
        if (!_instance) {
            return DiscordError::DISCORD_CORE_NULL_INSTANCE;
        }

        _instance->UserManager().OnCurrentUserUpdate.DisconnectAll();
        _initialized = false;

        return DiscordError::DISCORD_NONE;
    }

    DiscordError Wrapper::Update() const {
        if (!_instance) {
            return DiscordError::DISCORD_CORE_NULL_INSTANCE;
        }

        _instance->RunCallbacks();
        return DiscordError::DISCORD_NONE;
    }

    DiscordError Wrapper::SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity, const std::string &largeImage, const std::string &largeText, const std::string &smallImage, const std::string &smallText) const {
        if (!_instance) {
            return DiscordError::DISCORD_CORE_NULL_INSTANCE;
        }

        discord::Activity act {};
        auto assets = act.GetAssets();
        assets.SetLargeImage(largeImage.c_str());
        assets.SetLargeText(largeText.c_str());
        assets.SetSmallImage(smallImage.c_str());
        assets.SetSmallText(smallText.c_str());

        act.SetDetails(details.c_str());
        act.SetState(state.c_str());
        act.SetType(activity);
        _instance->ActivityManager().UpdateActivity(act, [](discord::Result res) {
            if (res != discord::Result::Ok) {
                Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to update activity");
            }
        });

        return DiscordError::DISCORD_NONE;
    }

    DiscordError Wrapper::SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity) const {
        return SetPresence(state, details, activity, "logo-large", "MafiaHub", "logo-small", "MafiaHub");
    }

    void Wrapper::SignInWithDiscord(const DiscordLoginProc &proc) const {
        _instance->ApplicationManager().GetOAuth2Token([proc](discord::Result result, const discord::OAuth2Token &tokenData) {
            if (result == discord::Result::Ok) {
                proc(tokenData.GetAccessToken());
            }
            else
                proc("");
        });
    }

    discord::UserManager &Wrapper::GetUserManager() const {
        return _instance->UserManager();
    }

    discord::ActivityManager &Wrapper::GetActivityManager() const {
        return _instance->ActivityManager();
    }

    discord::ImageManager &Wrapper::GetImageManager() const {
        return _instance->ImageManager();
    }

    discord::OverlayManager &Wrapper::GetOverlayManager() const {
        return _instance->OverlayManager();
    }

    discord::ApplicationManager &Wrapper::GetApplicationManager() const {
        return _instance->ApplicationManager();
    }

    discord::VoiceManager &Wrapper::GetVoiceManager() const {
        return _instance->VoiceManager();
    }

    discord::RelationshipManager &Wrapper::GetRelationshipManager() const {
        return _instance->RelationshipManager();
    }
} // namespace Framework::External::Discord
