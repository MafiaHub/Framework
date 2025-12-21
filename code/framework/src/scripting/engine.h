/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mutex>
#include <functional>

#include <sol/sol.hpp>

#include "types/errors.h"
#include "shared.h"

namespace Framework::Scripting {
    using ScriptProc = std::function<void()>;

    class Engine {
      public:
        sol::state* _luaEngine = nullptr;
        std::mutex _executionMutex;

      protected:
        ScriptProc _onLoadProc   = nullptr;
        ScriptProc _onUnloadProc = nullptr;

      public:
        virtual EngineError Init(SDKRegisterCallback) = 0;
        virtual EngineError Shutdown()                = 0;
        virtual void Update()                         = 0;

        bool InitCommonSDK();

        sol::state *GetLuaEngine() {
            return _luaEngine;
        }

        void SetOnLoadProc(ScriptProc proc) {
            _onLoadProc = std::move(proc);
        }

        void SetOnUnloadProc(ScriptProc proc) {
            _onUnloadProc = std::move(proc);
        }
    };
} // namespace Framework::Scripting
