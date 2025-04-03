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

namespace Framework::Integrations::Scripting {
    class ViewWrapper {
      private:
        int _viewId;
        Framework::GUI::Manager *_webManager;
        Framework::GUI::View *_view;
        sol::function _onDOMReadyCallback;
        sol::function _onWindowObjectReadyCallback;
        sol::function _onConsoleMessageCallback;

      public:
        ViewWrapper(int viewId, Framework::GUI::Manager *manager): _viewId(viewId), _webManager(manager), _view(nullptr) {
            if (_webManager) {
                _view = _webManager->GetView(_viewId);
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
    };

    class Views {
      private:
        static ViewWrapper CreateView(const std::string &url, int width = 0, int height = 0, int x = 0, int y = 0) {
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

            // ensures the view is destroyed on gamemode reload or server disconnect
            view->SetGarbageCollected(true);

            return ViewWrapper(viewId, guiManager);
        }

      public:
        static void Register(sol::state *luaEngine) {
            // Register the ViewWrapper class
            sol::usertype<ViewWrapper> viewWrapperType = luaEngine->new_usertype<ViewWrapper>("View", sol::no_constructor);

            viewWrapperType["create"]      = &Views::CreateView;
            viewWrapperType["getId"]       = &ViewWrapper::GetId;
            viewWrapperType["setPosition"] = &ViewWrapper::SetPosition;
            viewWrapperType["setFocus"]    = &ViewWrapper::Focus;
            viewWrapperType["getFocus"]    = &ViewWrapper::HasFocus;
            viewWrapperType["setDisplay"]  = &ViewWrapper::Display;
            viewWrapperType["getDisplay"]  = &ViewWrapper::ShouldDisplay;
            viewWrapperType["destroy"]     = &ViewWrapper::Destroy;

            // TODO: consider whether we want to expose raw JS calls to the user
            // viewWrapperType["evaluateScript"]                 = &ViewWrapper::EvaluateScript;
        }
    };
} // namespace Framework::Integrations::Scripting
