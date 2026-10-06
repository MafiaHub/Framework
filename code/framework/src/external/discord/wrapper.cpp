/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include <logging/logger.h>

#include <cstring>

namespace Framework::External::Discord {
    Utils::Result<void, Framework::Error> Wrapper::Init(int64_t id) {
        const auto result = discord::Core::Create(id, DiscordCreateFlags_NoRequireDiscord, &_instance);
        if (result != discord::Result::Ok) {
            return Framework::Error("Failed to create the Discord core instance");
        }

        _instance->UserManager().OnCurrentUserUpdate.Connect([this]() {
            _instance->UserManager().GetCurrentUser(&_user);
            Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("[Discord] Current user updated {} ({})", _user.GetUsername(), _user.GetId());
        });

        _initialized = true;
        return {};
    }

    void Wrapper::Shutdown() {
        if (!_instance) {
            return;
        }

        _instance->UserManager().OnCurrentUserUpdate.DisconnectAll();

        delete _instance;
        _instance = nullptr;
        _publishedActivity.reset();

        Lifecycle::Shutdown();
    }

    void Wrapper::Update() {
        if (!_instance) {
            return;
        }

        _instance->RunCallbacks();
    }

    Utils::Result<void, Framework::Error> Wrapper::SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity, const std::string &largeImage, const std::string &largeText, const std::string &smallImage, const std::string &smallText) const {
        if (!_instance) {
            return Framework::Error {"Discord core instance is null"};
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

        return {};
    }

    Utils::Result<void, Framework::Error> Wrapper::SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity) const {
        return SetPresence(state, details, activity, "logo-large", "MafiaHub", "logo-small", "MafiaHub");
    }

    Utils::Result<void, Framework::Error> Wrapper::UpdateActivity(const discord::Activity &activity) const {
        if (!_instance) {
            return Framework::Error {"Discord core instance is null"};
        }

        _instance->ActivityManager().UpdateActivity(activity, [](discord::Result res) {
            if (res != discord::Result::Ok) {
                Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to update activity");
            }
        });

        return {};
    }

    Utils::Result<void, Framework::Error> Wrapper::ClearActivity() const {
        if (!_instance) {
            return Framework::Error {"Discord core instance is null"};
        }

        _instance->ActivityManager().ClearActivity([](discord::Result res) {
            if (res != discord::Result::Ok) {
                Logging::GetLogger(FRAMEWORK_INNER_INTEGRATIONS)->debug("Failed to clear activity");
            }
        });

        return {};
    }

    void Wrapper::SetBaseActivity(const discord::Activity &activity) {
        std::scoped_lock lock(_layersMutex);
        _baseActivity = activity;
        PublishLayers();
    }

    void Wrapper::SetScriptFields(uint32_t fields) {
        std::scoped_lock lock(_layersMutex);
        _scriptFields = fields & PresenceField::All;
        PublishLayers();
    }

    void Wrapper::SetScriptActivity(const discord::Activity &activity, uint32_t written) {
        std::scoped_lock lock(_layersMutex);
        _scriptActivity = activity;
        _scriptWritten  = written & PresenceField::All;
        PublishLayers();
    }

    void Wrapper::ClearScriptActivity() {
        std::scoped_lock lock(_layersMutex);
        _scriptActivity = discord::Activity {};
        _scriptWritten  = 0;
        PublishLayers();
    }

    void Wrapper::PublishLayers() {
        if (!_instance) {
            return;
        }

        const uint32_t overrides = _scriptWritten & _scriptFields;
        if (!_baseActivity && overrides == 0) {
            if (_publishedActivity) {
                _publishedActivity.reset();
                (void)ClearActivity();
            }
            return;
        }

        discord::Activity composed      = _baseActivity.value_or(discord::Activity {});
        const discord::Activity &script = _scriptActivity;
        if (overrides & PresenceField::Type) {
            composed.SetType(script.GetType());
        }
        if (overrides & PresenceField::Name) {
            composed.SetName(script.GetName());
        }
        if (overrides & PresenceField::Details) {
            composed.SetDetails(script.GetDetails());
        }
        if (overrides & PresenceField::State) {
            composed.SetState(script.GetState());
        }
        if (overrides & PresenceField::Timestamps) {
            composed.GetTimestamps() = script.GetTimestamps();
        }
        if (overrides & PresenceField::LargeImage) {
            composed.GetAssets().SetLargeImage(script.GetAssets().GetLargeImage());
            composed.GetAssets().SetLargeText(script.GetAssets().GetLargeText());
        }
        if (overrides & PresenceField::SmallImage) {
            composed.GetAssets().SetSmallImage(script.GetAssets().GetSmallImage());
            composed.GetAssets().SetSmallText(script.GetAssets().GetSmallText());
        }
        if (overrides & PresenceField::Party) {
            composed.GetParty() = script.GetParty();
        }
        if (overrides & PresenceField::Secrets) {
            composed.GetSecrets() = script.GetSecrets();
        }
        if (overrides & PresenceField::Instance) {
            composed.SetInstance(script.GetInstance());
        }
        if (overrides & PresenceField::Platforms) {
            composed.SetSupportedPlatforms(script.GetSupportedPlatforms());
        }

        // Skip unchanged updates; they count against Discord's rate limit. Activity is a POD.
        if (_publishedActivity && std::memcmp(&*_publishedActivity, &composed, sizeof(composed)) == 0) {
            return;
        }
        _publishedActivity = composed;
        (void)UpdateActivity(composed);
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

    std::string Wrapper::GetUserId() const {
        const auto id = _user.GetId();
        return id ? std::to_string(id) : std::string {};
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
