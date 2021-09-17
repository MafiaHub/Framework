#include "wrapper.h"

#include <logging/logger.h>

namespace Framework::Integrations {
    bool Discord::Init(int64_t id) {
        auto result = discord::Core::Create(id, DiscordCreateFlags_NoRequireDiscord, &_instance);
        if (result != discord::Result::Ok) {
            return false;
        }

        _instance->UserManager().OnCurrentUserUpdate.Connect([this]() {
            _instance->UserManager().GetCurrentUser(&_user);
            Logging::GetInstance()
                ->Get(FRAMEWORK_INNER_INTEGRATIONS)
                ->debug("[Discord] Current user updated {} ({})", _user.GetUsername(), _user.GetId());
        });

        return true;
    }

    bool Discord::Shutdown() {
        if (!_instance) {
            return false;
        }

        _instance->UserManager().OnCurrentUserUpdate.DisconnectAll();

        return true;
    }

    void Discord::Update() {
        if (_instance) {
            _instance->RunCallbacks();
        }
    }

    void Discord::SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity,
                              const std::string &largeImage, const std::string &largeText,
                              const std::string &smallImage, const std::string &smallText) {
        if (!_instance) {
            return;
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
    }
    void Discord::SetPresence(const std::string &state, const std::string &details, discord::ActivityType activity) {
        SetPresence(state, details, activity, "logo-large", "MafiaHub", "logo-small", "MafiaHub");
    }

    discord::UserManager &Discord::GetUserManager() {
        return _instance->UserManager();
    }

    discord::ActivityManager &Discord::GetActivityManager() {
        return _instance->ActivityManager();
    }

    discord::ImageManager &Discord::GetImageManager() {
        return _instance->ImageManager();
    }

    discord::OverlayManager &Discord::GetOverlayManager() {
        return _instance->OverlayManager();
    }

    discord::ApplicationManager &Discord::GetApplicationManager() {
        return _instance->ApplicationManager();
    }

    discord::VoiceManager &Discord::GetVoiceManager() {
        return _instance->VoiceManager();
    }

    discord::RelationshipManager &Discord::GetRelationshipManager() {
        return _instance->RelationshipManager();
    }
} // namespace Framework::Integrations
