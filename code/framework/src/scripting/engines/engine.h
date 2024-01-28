/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

#include "../errors.h"
#include "callback.h"

namespace Framework::Scripting::Engines {
    class IEngine {
      public:
        virtual ~IEngine() {}
        virtual EngineError Init(SDKRegisterCallback) = 0;
        virtual EngineError Shutdown()                = 0;
        virtual void Update()                         = 0;

        virtual bool PreloadGamemode(std::string) = 0;
        virtual bool LoadGamemode(std::string)   = 0;
        virtual bool UnloadGamemode(std::string) = 0;

        virtual void SetProcessArguments(int, char **) = 0;

        virtual void SetModName(std::string) = 0;
        virtual std::string GetModName() const = 0;

        virtual std::string GetGameModeName() const = 0;
    };
} // namespace Framework::Scripting::Engines
