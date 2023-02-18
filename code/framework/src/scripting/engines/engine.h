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
#include "resource.h"

namespace Framework::Scripting::Engines {
    class IEngine {
      public:
        virtual ~IEngine() {}
        virtual EngineError Init(SDKRegisterCallback) = 0;
        virtual EngineError Shutdown()                = 0;
        virtual void Update()                         = 0;

        virtual IResource *LoadResource(std::string)   = 0;
        virtual IResource *UnloadResource(std::string) = 0;

        virtual void SetProcessArguments(int, char **) = 0;

        virtual void SetModName(std::string) = 0;
        virtual std::string GetModName() const = 0;
    };
} // namespace Framework::Scripting::Engines
