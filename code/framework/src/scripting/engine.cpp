/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

#include "builtins/color_rgb.h"
#include "builtins/color_rgba.h"
#include "builtins/console.h"
#include "builtins/environment.h"
#include "builtins/event.h"
#include "builtins/exports.h"
#include "builtins/hash.h"
#include "builtins/json.h"
#include "builtins/matrix.h"
#include "builtins/message.h"
#include "builtins/quaternion.h"
#include "builtins/resource.h"
#include "builtins/vector_2.h"
#include "builtins/vector_3.h"
#include "builtins/vector_4.h"

namespace Framework::Scripting {
    bool Engine::InitCommonSDK() {
        Builtins::Console::Register(_luaEngine);
        Builtins::ColorRGB::Register(_luaEngine);
        Builtins::ColorRGBA::Register(_luaEngine);
        Builtins::Event::Register(_luaEngine);
        Builtins::Exports::Register(_luaEngine);
        Builtins::Hash::Register(_luaEngine);
        Builtins::JSON::Register(_luaEngine);
        Builtins::Matrix::Register(_luaEngine);
        Builtins::Message::Register(_luaEngine);
        Builtins::Quaternion::Register(_luaEngine);
        Builtins::ResourceBuiltin::Register(_luaEngine);
        Builtins::Vector4::Register(_luaEngine);
        Builtins::Vector3::Register(_luaEngine);
        Builtins::Vector2::Register(_luaEngine);
        Builtins::Environment::Register(_luaEngine);
        return true;
    }
} // namespace Framework::Scripting
