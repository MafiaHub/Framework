/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <map>
#include <vector>

#include <logging/logger.h>
#include <utils/time.h>
#include <sol/sol.hpp>

#include "errors.h"
#include "shared.h"

namespace Framework::Scripting {
    using EventHandler = std::vector<sol::function>;
    class Engine {
      public:
        sol::state _luaEngine;
        std::map<std::string, EventHandler> _eventHandlers = {};

      public:
        virtual EngineError Init(SDKRegisterCallback) = 0;
        virtual EngineError Shutdown()                = 0;
        virtual void Update()                         = 0;

        bool InitCommonSDK();

        sol::state& GetLuaEngine() {
            return _luaEngine;
        }

        void ListenEvent(std::string, sol::function);
        
        template <typename... Args>
        void InvokeEvent(const std::string &name, Args &&...args) {
            auto &callbacks = _eventHandlers[name];

            for (auto &callback : callbacks) {
                sol::protected_function pf {callback};
                auto result = pf(std::forward<Args>(args)...);
                if (!result.valid()) {
                    sol::error err = result;
                    spdlog::error(err.what());
                }
            }
        }
    };
} // namespace Framework::Scripting
