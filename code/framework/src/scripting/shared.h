/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <function2.hpp>
#include <string>

namespace Framework::Scripting {
    class Engine;

    template <typename EngineKind>
    class SDKRegisterWrapper final {
      private:
        EngineKind *_engine = nullptr;

      public:
        SDKRegisterWrapper(EngineKind *engine): _engine(engine) {}

        EngineKind *GetEngine() const {
            return _engine;
        }
    };
    using SDKRegisterCallback = fu2::function<void(SDKRegisterWrapper<Engine>)>;
}
