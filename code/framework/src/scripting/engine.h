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
    using EventRegistrationCallback = std::function<void(const std::string &)>;

    class Engine {
      public:
        sol::state* _luaEngine = nullptr;
        std::mutex _executionMutex;

        std::map<std::string, EventHandler> _eventHandlers = {};
        std::map<std::string, EventHandler> _eventRemoteHandlers = {};

      protected:
        ScriptProc _onLoadProc   = nullptr;
        ScriptProc _onUnloadProc = nullptr;
        InvokeEventProc _onInvokeEventProc                 = nullptr;
        EventRegistrationCallback _onFirstEventRegisteredProc = nullptr;
        EventRegistrationCallback _onLastEventUnregisteredProc = nullptr;

      public:
        virtual EngineError Init(SDKRegisterCallback) = 0;
        virtual EngineError Shutdown()                = 0;
        virtual void Update()                         = 0;

        bool InitCommonSDK();

        sol::state *GetLuaEngine() {
            return _luaEngine;
        }

        void ListenEvent(const std::string &name, sol::function fnc) {
            auto &handlers         = _eventHandlers[name];
            bool firstRegistration = handlers.empty();

            handlers.push_back(std::move(fnc));

            if (firstRegistration && _onFirstEventRegisteredProc) {
                _onFirstEventRegisteredProc(name);
            }
        }

        void RemoveEventListener(const std::string &name, const sol::function &fnc) {
            auto it = _eventHandlers.find(name);
            if (it == _eventHandlers.end())
                return;

            auto &handlers = it->second;

            handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                               [&](const sol::function &f) {
                                   return f == fnc;
                               }),
                handlers.end());

            if (handlers.empty()) {
                _eventHandlers.erase(it);
                if (_onLastEventUnregisteredProc) {
                    _onLastEventUnregisteredProc(name);
                }
            }
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

        void SetOnFirstEventRegisteredProc(EventRegistrationCallback cb) {
            _onFirstEventRegisteredProc = std::move(cb);
        }

        void SetOnLastEventUnregisteredProc(EventRegistrationCallback cb) {
            _onLastEventUnregisteredProc = std::move(cb);
        }
    };
} // namespace Framework::Scripting
