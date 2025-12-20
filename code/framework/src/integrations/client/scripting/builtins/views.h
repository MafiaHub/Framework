/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <sol/sol.hpp>

#include "gui/manager.h"
#include "gui/view.h"

#include <logging/logger.h>

#include "core_modules.h"
#include <mutex>
#include <unordered_set>
#include <vector>

#include <JavaScriptCorePP/JSHelper.h>

namespace Framework::Integrations::Scripting {
    class ViewWrapper {
      private:
        int _viewId;
        Framework::GUI::Manager *_webManager;
        Framework::GUI::View *_view;

        sol::function _onViewInit {};

        using EventMeta = std::pair<int, std::string>;

        static inline std::map<EventMeta, Framework::Scripting::EventHandler> _eventHandlers = {};

      public:
        ViewWrapper(int viewId, Framework::GUI::Manager *manager): _viewId(viewId), _webManager(manager), _view(nullptr) {
            if (_webManager) {
                _view = _webManager->GetView(_viewId);

                // Hook up a window object ready callback
                _view->SetOnWindowObjectReadyCallback([viewId, this](uint64_t frame_id, bool is_main_frame, std::string url) {
                    const auto view = Framework::CoreModules::GetGUIManager()->GetView(viewId);
                    const auto sdk  = view->GetSDK();

                    // Grab the context
                    auto context = JavaScriptCorePP::JSContext(sdk->GetContext());
                    auto obj     = context.GetGlobalObject();

                    // Set up a bootstrapped Event system
                    const std::string eventBootstrappingModule = R"(
                        (function() {
                            var __mh_events = {};
                            window.on = function(eventName, callback) {
                                if (!__mh_events[eventName]) {
                                    __mh_events[eventName] = [];
                                }
                                __mh_events[eventName].push(callback);
                            };
                            window.__mh_emit = function(eventName, eventPayload) {
                                if (__mh_events[eventName]) {
                                    for (var i = 0; i < __mh_events[eventName].length; i++) {
                                        __mh_events[eventName][i](eventPayload);
                                    }
                                }
                            };
                        })()
                    )";

                    view->EvaluateScript(eventBootstrappingModule);

                    // Bind emit function
                    obj["emit"] = [viewId, this](const JavaScriptCorePP::JSContext &context, const std::vector<JavaScriptCorePP::JSValue> &args, JavaScriptCorePP::JSValue &returnValue, JavaScriptCorePP::JSValue &returnException) {
                        // Make sure there is only two arguments
                        if (args.size() == 0) {
                            returnException = context.CreateString("Invalid argument count: emit(string, object | string | null)");
                            return;
                        }
                        std::string eventName;
                        std::string eventPayload;
                        // Grab the event name - must be a string
                        if (args[0].IsString()) {
                            eventName = args[0].GetString();
                        }
                        else {
                            returnException = context.CreateString("First argument must be a string");
                            return;
                        }

                        const auto scriptingModule = Framework::CoreModules::GetScriptingEngine();

                        sol::object payload {};
                        if (args[1].IsObject() || args[1].IsString()) {
                            try {
                                std::string payloadStr = "{}";

                                if (args[1].IsString()) {
                                    payloadStr = args[1].GetString();
                                }
                                else if (args[1].IsObject()) {
                                    payloadStr = args[1].ToJSON();
                                }

                                payloadStr                 = args[1].ToJSON();
                                nlohmann::json payloadJson = nlohmann::json::parse(payloadStr);
                                payload                    = Framework::Scripting::Utils::JsonToSol(sol::this_state(scriptingModule->GetLuaEngine()->lua_state()), payloadJson);
                            }
                            catch (const std::exception &ex) {
                                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Failed to parse event payload: {}", ex.what());
                                return;
                            }
                        }

                        InvokeEvent(EventMeta(viewId, eventName), payload);
                    };
                });

                // Hook up a DOMContentLoaded callback
                _view->SetOnDOMReadyCallback([viewId, this](uint64_t frame_id, bool is_main_frame, std::string url) {
                    if (_onViewInit.valid()) {
                        sol::protected_function pf {_onViewInit};
                        auto result = pf(viewId, frame_id, is_main_frame, url);
                        if (!result.valid()) {
                            sol::error err = result;
                            spdlog::error(err.what());
                        }
                    }
                });
            }
        }

        inline void ListenEvent(std::string name, sol::function fnc) {
            _eventHandlers[EventMeta(_viewId, name)].push_back(fnc);
        }

        template <typename... Args>
        static inline void InvokeEvent(const EventMeta &name, Args &&...args) {
            auto it = _eventHandlers.find(name);
            if (it != _eventHandlers.end()) {
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

        void InvokeJSEvent(const std::string &eventName, const sol::object object) {
            if (_view) {
                try {
                    // Convert the object to JSON
                    nlohmann::json jsonPayload = Framework::Scripting::Utils::SolToJson(object);
                    std::string eventPayload   = jsonPayload.dump();

                    // Emit the event to the view
                    _view->EvaluateScript(fmt::format("window.__mh_emit(`{}`, `{}`);", eventName, eventPayload));
                }
                catch (const std::exception &ex) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Failed to emit event: {}", ex.what());
                }
            }
        }

        // Explicit method to destroy the view
        void Destroy() {
            if (_webManager && _view) {
                // Then destroy the view
                _webManager->DestroyView(_viewId);
                _view = nullptr;
            }
        }

        int GetId() const {
            return _viewId;
        }

        void SetPosition(int x, int y) {
            if (_view) {
                _view->SetPosition(x, y);
            }
        }

        void SetZIndex(int z) {
            if (_view) {
                _view->SetZIndex(z);
            }
        }

        int GetZIndex() const {
            if (_view) {
                return _view->GetZIndex();
            }
            return 0;
        }

        void Focus(bool enable) {
            if (_view) {
                _view->Focus(enable);
            }
        }

        bool HasFocus() const {
            if (_view) {
                return _view->HasFocus();
            }
            return false;
        }

        void Display(bool enable) {
            if (_view) {
                _view->Display(enable);
            }
        }

        bool ShouldDisplay() const {
            if (_view) {
                return _view->ShouldDisplay();
            }
            return false;
        }

        std::string EvaluateScript(const std::string &script) {
            if (_view) {
                return _view->EvaluateScript(script);
            }
            return "";
        }

        void SetOnViewInit(sol::function callback) {
            _onViewInit = callback;
        }
    };

    class Views {
      private:
        static ViewWrapper *CreateView(const std::string &url, int width = 0, int height = 0, int x = 0, int y = 0) {
            auto guiManager = Framework::CoreModules::GetGUIManager();
            if (!guiManager) {
                throw std::runtime_error("GUI Manager is not initialized");
            }

            int viewId = guiManager->CreateView(url, width, height, x, y);
            if (viewId < 0) {
                throw std::runtime_error("Failed to create view");
            }

            const auto view = guiManager->GetView(viewId);
            view->Display(true);
            view->Focus(false);

            // ensures the view is destroyed on resource reload or server disconnect
            view->SetGarbageCollected(true);

            return new ViewWrapper(viewId, guiManager);
        }

      public:
        static void Register(sol::state *luaEngine) {
            // Register the ViewWrapper class
            sol::usertype<ViewWrapper> viewWrapperType = luaEngine->new_usertype<ViewWrapper>("View", sol::no_constructor);

            viewWrapperType["create"]        = &Views::CreateView;
            viewWrapperType["getId"]         = &ViewWrapper::GetId;
            viewWrapperType["setPosition"]   = &ViewWrapper::SetPosition;
            viewWrapperType["setZIndex"]     = &ViewWrapper::SetZIndex;
            viewWrapperType["getZIndex"]     = &ViewWrapper::GetZIndex;
            viewWrapperType["setFocus"]      = &ViewWrapper::Focus;
            viewWrapperType["getFocus"]      = &ViewWrapper::HasFocus;
            viewWrapperType["setDisplay"]    = &ViewWrapper::Display;
            viewWrapperType["getDisplay"]    = &ViewWrapper::ShouldDisplay;
            viewWrapperType["on"]            = &ViewWrapper::ListenEvent;
            viewWrapperType["emit"]          = &ViewWrapper::InvokeJSEvent;
            viewWrapperType["onViewInit"] = &ViewWrapper::SetOnViewInit;
            viewWrapperType["destroy"]       = &ViewWrapper::Destroy;

            // TODO: consider whether we want to expose raw JS calls to the user
            // viewWrapperType["evaluateScript"]                 = &ViewWrapper::EvaluateScript;
        }
    };
} // namespace Framework::Integrations::Scripting
