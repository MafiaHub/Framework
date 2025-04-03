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
#include <vector>

#include "engine.h"

namespace Framework::Scripting {
    class ClientEngine : public Engine {
      private:
        // Script management
        std::vector<std::string> _loadedScripts;
        std::string _scriptCachePath;
        std::atomic<bool> _shutdownInProgress = false;

      public:
        ClientEngine() = default;
        ~ClientEngine() = default;

        EngineError Init(SDKRegisterCallback) override;
        EngineError Shutdown() override;
        void Update() override;

        // Script management
        bool LoadScripts();
        bool AddScript(const std::string &path);

        void SandboxEnvironment();


        void SetScriptCachePath(const std::string &path) {
            _scriptCachePath = path;
        }

        std::string GetScriptCachePath() const {
            return _scriptCachePath;
        }
    };
}
