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

#include "types/errors.h"
#include "shared.h"

namespace Framework::Scripting {
    using EventHandler = std::vector<sol::function>;
    class Engine {
      public:
        sol::state _luaEngine;

        std::map<std::string, EventHandler> _internalEventHandlers = {};
        std::map<std::string, EventHandler> _userDefinedEventHandlers = {};

      private:
        template <typename... Args>
        void InvokeHandlers(const std::map<std::string, std::vector<sol::function>> &handlersMap, const std::string &name, Args &&...args) {
            auto it = handlersMap.find(name);
            if (it != handlersMap.end()) {
                for (auto &callback : it->second) {
                    sol::protected_function pf {callback};
                    auto result = pf(std::forward<Args>(args)...);
                    if (!result.valid()) {
                        sol::error err = result;
                        spdlog::error(err.what());
                    }
                }
            }
        }

      public:
        virtual EngineError Init(SDKRegisterCallback) = 0;
        virtual EngineError Shutdown()                = 0;
        virtual void Update()                         = 0;

        bool InitCommonSDK();

        sol::state& GetLuaEngine() {
            return _luaEngine;
        }

        void ListenEvent(std::string name, sol::function fnc, bool userDefined = false) {
            if (userDefined) {
                _userDefinedEventHandlers[name].push_back(fnc);
            }
            else {
                _internalEventHandlers[name].push_back(fnc);
            }
        }

        template <typename... Args>
        void InvokeEvent(const std::string &name, Args &&...args) {
            InvokeHandlers(_internalEventHandlers, name, std::forward<Args>(args)...);
            InvokeHandlers(_userDefinedEventHandlers, name, std::forward<Args>(args)...);
        }

        // Invoke only user-defined event handlers
        template <typename... Args>
        void InvokeUserDefinedEvent(const std::string &name, Args &&...args) {
            InvokeHandlers(_userDefinedEventHandlers, name, std::forward<Args>(args)...);
        }
    };
} // namespace Framework::Scripting
