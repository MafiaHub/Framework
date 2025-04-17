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
#include <mutex>

#include <logging/logger.h>
#include <utils/time.h>
#include <sol/sol.hpp>

#include "types/errors.h"
#include "shared.h"

namespace Framework::Scripting {
    using EventHandler = std::vector<sol::function>;
    using ScriptProc   = std::function<void()>;
    using InvokeEventProc   = std::function<bool(std::string)>;

    class Engine {
      public:
        sol::state* _luaEngine = nullptr;
        std::mutex _executionMutex;

        std::map<std::string, EventHandler> _eventHandlers = {};
        std::map<std::string, EventHandler> _eventRemoteHandlers = {};

      protected:
        ScriptProc _onLoadProc   = nullptr;
        ScriptProc _onUnloadProc = nullptr;
        InvokeEventProc _onInvokeEventProc = nullptr;

      public:
        virtual EngineError Init(SDKRegisterCallback) = 0;
        virtual EngineError Shutdown()                = 0;
        virtual void Update()                         = 0;

        bool InitCommonSDK();

        sol::state *GetLuaEngine() {
            return _luaEngine;
        }

        void ListenEvent(std::string name, sol::function fnc) {
            _eventHandlers[name].push_back(fnc);
        }

        void ListenRemoteEvent(std::string name, sol::function fnc) {
            _eventRemoteHandlers[name].push_back(fnc);
        }

        template <typename... Args>
        void InvokeEvent(const std::string &name, Args &&...args) {
            auto it = _eventHandlers.find(name);
            if (it != _eventHandlers.end()) {
                std::lock_guard<std::mutex> lock(_executionMutex);

                if (_onInvokeEventProc) {
                    if (!_onInvokeEventProc(name)) {
                        return;
                    }
                }

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

        template <typename... Args>
        void InvokeRemoteEvent(const std::string &name, Args &&...args) {
            auto it = _eventRemoteHandlers.find(name);
            if (it != _eventRemoteHandlers.end()) {
                std::lock_guard<std::mutex> lock(_executionMutex);
                
                if (_onInvokeEventProc) {
                    if (!_onInvokeEventProc(name)) {
                        return;
                    }
                }
                
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

        void ClearEventHandlers() {
            _eventHandlers.clear();
            _eventRemoteHandlers.clear();
        }

        void SetOnLoadProc(ScriptProc proc) {
            _onLoadProc = std::move(proc);
        }

        void SetOnUnloadProc(ScriptProc proc) {
            _onUnloadProc = std::move(proc);
        }

        void SetOnInvokeEventProc(InvokeEventProc proc) {
            _onInvokeEventProc = std::move(proc);
        }
    };
} // namespace Framework::Scripting
