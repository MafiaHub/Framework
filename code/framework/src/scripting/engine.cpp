/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

#include "builtins/console.h"
#include "builtins/color_rgb.h"
#include "builtins/color_rgba.h"
#include "builtins/quaternion.h"
#include "builtins/vector_2.h"
#include "builtins/vector_3.h"

namespace Framework::Scripting {
    void Engine::ListenEvent(const std::string name, const sol::function fnc) {
        _eventHandlers[name].push_back(fnc);
    }

    bool Engine::InitCommonSDK() {
        // Bind the event handler
        _luaEngine.set_function("listenEvent", [this](const std::string name, const sol::function fnc) {
            ListenEvent(name, fnc);
        });

        // Bind the builtins
        Builtins::Console::Register(_luaEngine);
        Builtins::ColorRGB::Register(_luaEngine);
        Builtins::ColorRGBA::Register(_luaEngine);
        Builtins::Quaternion::Register(_luaEngine);
        Builtins::Vector3::Register(_luaEngine);
        Builtins::Vector2::Register(_luaEngine);

        return true;
    }
}
