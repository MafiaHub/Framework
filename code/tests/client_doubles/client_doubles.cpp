/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// The functions the client builtins call on the chat box and on the Discord wrapper, whose own sources need
// ImGui and the Discord library and are not part of FrameworkClientScriptingTests. See
// integrations/client/instance.h next to this file.

#include "integrations/client/instance.h"

namespace ClientDoubles {
    PresenceLog &Presence() {
        static PresenceLog log;
        return log;
    }
} // namespace ClientDoubles

namespace Framework::Integrations::Client::UI {
    // As chat_box.cpp has them, without what only drawing reads.
    void ChatBox::OpenInput() {
        if (!_sessionActive || _inputActive) {
            return;
        }
        _inputActive = true;
    }

    void ChatBox::CloseInput(bool) {
        _inputActive = false;
    }

    void ChatBox::SetSessionActive(bool active) {
        _sessionActive = active;
        if (!active) {
            _inputActive = false;
        }
    }
} // namespace Framework::Integrations::Client::UI

namespace Framework::External::Discord {
    // Connected at once, to nothing.
    Utils::Result<void, Framework::Error> Wrapper::Init(int64_t) {
        _initialized = true;
        return {};
    }

    void Wrapper::Shutdown() {
        _initialized = false;
    }

    void Wrapper::Update() {}

    void Wrapper::SetScriptActivity(const discord::Activity &activity, uint32_t written) {
        ClientDoubles::PresenceLog &log = ClientDoubles::Presence();
        log.activity                    = activity;
        log.written                     = written;
        ++log.published;
    }

    void Wrapper::ClearScriptActivity() {
        ++ClientDoubles::Presence().cleared;
    }

    std::string Wrapper::GetUserId() const {
        return ClientDoubles::Presence().userId;
    }

    std::string Wrapper::GetUsername() const {
        return ClientDoubles::Presence().username;
    }
} // namespace Framework::External::Discord
