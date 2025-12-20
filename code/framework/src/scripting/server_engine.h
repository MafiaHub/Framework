/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <map>
#include <string>

#include <logging/logger.h>
#include <utils/time.h>

#include "engine.h"

namespace Framework::Scripting {
    class ServerEngine : public Engine {
      public:
        EngineError Init(SDKRegisterCallback) override;
        EngineError Shutdown() override;
        void Update() override;
    };
} // namespace Framework::Scripting
