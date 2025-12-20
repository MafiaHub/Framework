/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "shared.h"
#include "types/errors.h"

#include <atomic>
#include <map>
#include <string>

#include "engine.h"

namespace Framework::Scripting {
    /**
     * Client-side scripting engine.
     *
     * Note: Per-resource sandboxing is handled by ResourceManager using
     * EnvironmentSandbox::SetupClientSandbox(). This engine just provides
     * the base Lua state and common SDK registration.
     */
    class ClientEngine : public Engine {
      private:
        std::atomic<bool> _shutdownInProgress = false;

      public:
        ClientEngine() = default;
        ~ClientEngine() = default;

        EngineError Init(SDKRegisterCallback) override;
        EngineError Shutdown() override;
        void Update() override;
    };
}
