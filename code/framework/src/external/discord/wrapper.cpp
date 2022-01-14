/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include <logging/logger.h>

namespace Framework::External::Discord {
    DiscordError Wrapper::Init(int64_t id) {
        auto result = discord::Core::Create(id, DiscordCreateFlags_NoRequireDiscord, &_instance);
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

    DiscordError Wrapper::Update() {
        if (!_instance) {
            return DiscordError::DISCORD_CORE_NULL_INSTANCE;
        }

        _instance->RunCallbacks();
        return DiscordError::DISCORD_NONE;
    }

    DiscordError Wrapper::SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity, const std::string &largeImage,
        const std::string &largeText, const std::string &smallImage, const std::string &smallText) {
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
    DiscordError Wrapper::SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity) {
        return SetPresence(state, details, activity, "logo-large", "MafiaHub", "logo-small", "MafiaHub");
    }

    void Wrapper::SignInWithDiscord(DiscordLoginProc proc) {
        _instance->ApplicationManager().GetOAuth2Token([proc](discord::Result result, const discord::OAuth2Token &tokenData) {
            if (result == discord::Result::Ok) {
                proc(tokenData.GetAccessToken());
            }
            else
                proc("");
        });
    }

    discord::UserManager &Wrapper::GetUserManager() {
        return _instance->UserManager();
    }

    discord::ActivityManager &Wrapper::GetActivityManager() {
        return _instance->ActivityManager();
    }

    discord::ImageManager &Wrapper::GetImageManager() {
        return _instance->ImageManager();
    }

    discord::OverlayManager &Wrapper::GetOverlayManager() {
        return _instance->OverlayManager();
    }

    discord::ApplicationManager &Wrapper::GetApplicationManager() {
        return _instance->ApplicationManager();
    }

    discord::VoiceManager &Wrapper::GetVoiceManager() {
        return _instance->VoiceManager();
    }

    discord::RelationshipManager &Wrapper::GetRelationshipManager() {
        return _instance->RelationshipManager();
    }
} // namespace Framework::External::Discord
