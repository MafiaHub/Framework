/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// A stand-in for the instance of a client, for FrameworkClientScriptingTests only: this directory is ahead of
// the framework's on the include path of that binary, so a client builtin that includes
// "integrations/client/instance.h" gets this one. The real header needs a renderer, a browser and Direct3D,
// and builds on Windows alone; a builtin reaches three things through it, and they are here. The chat box
// and the Discord wrapper are the real classes, with the few functions the tests need of them defined in
// client_doubles.cpp.

#include <external/discord/wrapper.h>

#include "integrations/client/ui/chat_box.h"

#include <cstdint>
#include <string>
#include <vector>

// This is a second definition of Integrations::Client::Instance. It is sound only because nothing in the binary
// sees the real one: code/tests/CMakeLists.txt fails the configuration if FrameworkClientScriptingTests ever
// links FrameworkClient. Do not include this from, or add this directory to, any other target.
namespace Framework::Integrations::Client {
    class Instance final {
      public:
        // The lines a script sent, in order.
        std::vector<std::string> sent;
        // Null until a test gives one, as on a client whose mod has no Discord application.
        External::Discord::Wrapper *presence = nullptr;

        void SendChatMessage(const std::string &text) {
            sent.push_back(text);
        }

        UI::ChatBox &GetChatBox() {
            return _chatBox;
        }

        External::Discord::Wrapper *GetPresence() const {
            return presence;
        }

      private:
        UI::ChatBox _chatBox;
    };
} // namespace Framework::Integrations::Client

namespace ClientDoubles {
    // What the Discord wrapper of the tests was told, and what it answers.
    struct PresenceLog final {
        discord::Activity activity {};
        uint32_t written = 0;
        int published    = 0;
        int cleared      = 0;
        std::string userId;
        std::string username;
    };

    PresenceLog &Presence();
} // namespace ClientDoubles
