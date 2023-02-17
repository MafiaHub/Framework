/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../engine_kind.h"
#include <function2.hpp>

namespace Framework::Scripting::Engines::Node {
    class SDK;
}

namespace Framework::Scripting::Engines {
    class SDKRegisterWrapper final {
      private:
        Framework::Scripting::EngineTypes _kind = ENGINE_NODE;
        void *_sdk                              = nullptr;

      public:
        SDKRegisterWrapper(void *sdk, Framework::Scripting::EngineTypes kind): _sdk(sdk), _kind(kind) {}

        Framework::Scripting::EngineTypes GetKind() const {
            return _kind;
        }

        Framework::Scripting::Engines::Node::SDK *GetNodeSDK() {
            return reinterpret_cast<Framework::Scripting::Engines::Node::SDK *>(_sdk);
        }
    };
    using SDKRegisterCallback = fu2::function<void(SDKRegisterWrapper)>;
} // namespace Framework::Scripting::Engines
