/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "manager.h"

#include <logging/logger.h>

#include "gui/backend/view_d3d11.h"

#include <core_modules.h>

#include <filesystem>

namespace Framework::GUI {
    Manager::Manager() {
        _clipboard = std::make_unique<SystemClipboard>();
        CoreModules::SetWebManager(this);
    }

    Manager::~Manager() {
        if (IsInitialized()) {
            Shutdown();
        }
    }

    void Manager::Shutdown() {
        for (auto &view : _views) {
            view.reset();
        }
        _views.clear();

        if (_cefInitialized) {
            CefShutdown();
            _cefInitialized = false;
        }

        Lifecycle::Shutdown();
    }

    bool Manager::Init(const std::string &rootDir, ViewportConfiguration initialViewport, Graphics::Renderer *renderer, bool gpuAccelerated) {
        _graphicsRenderer = renderer;
        _gpuAccelerated   = gpuAccelerated;

        SetViewportConfiguration(initialViewport);

        // Configure CEF settings
        CefSettings settings;
        settings.windowless_rendering_enabled = true;
        settings.multi_threaded_message_loop  = false;
        settings.no_sandbox                   = true;
        settings.log_severity                 = LOGSEVERITY_FATAL;

        CefString(&settings.cache_path) = rootDir + "/cache";
        CefString(&settings.log_file)  = rootDir + "/logs/cef.log";

        // CEF requires an absolute path for the subprocess executable
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path subprocessPath = std::filesystem::path(exePath).parent_path() / "cef_subprocess.exe";
        CefString(&settings.browser_subprocess_path) = subprocessPath.wstring();

        // Create the CEF app
        _cefApp = new CEF::App();

        // Initialize CEF
        CefMainArgs mainArgs(GetModuleHandle(nullptr));
        if (!CefInitialize(mainArgs, settings, _cefApp, nullptr)) {
            Framework::Logging::GetLogger("Web")->error("Failed to initialize CEF");
            return false;
        }

        _cefInitialized = true;
        _initialized    = true;
        Framework::Logging::GetLogger("Web")->info("CEF initialized successfully");
        return true;
    }

    void Manager::Update() {
        if (!_cefInitialized) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        // Pump the CEF message loop
        CefDoMessageLoopWork();

        // Update the views
        for (auto &view : _views) {
            view->Update();
        }
    }

    void Manager::Render() {
        if (!_cefInitialized) {
            return;
        }

        // Sort views by z-index
        std::vector<GUI::View *> views;
        for (auto &view : _views) {
            views.push_back(view.get());
        }
        std::sort(views.begin(), views.end(), [](GUI::View *a, GUI::View *b) {
            return a->GetZIndex() < b->GetZIndex();
        });

        std::scoped_lock lock(_renderMutex);

        // Render the views
        for (auto &view : views) {
            view->Render();
        }
    }

    void Manager::ProcessMouseEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const {
        for (auto &view : _views) {
            view->ProcessMouseEvent(hWnd, msg, wParam, lParam);
        }
    }

    void Manager::ProcessKeyboardEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const {
        for (auto &view : _views) {
            view->ProcessKeyboardEvent(hWnd, msg, wParam, lParam);
        }
    }

    int Manager::CreateView(std::string url, int width, int height, int offsetX, int offsetY) {
        if (!_cefInitialized) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: CEF is not initialized");
            return -1;
        }

        if (width == 0) {
            width = _viewportConfiguration.width;
        }

        if (height == 0) {
            height = _viewportConfiguration.height;
        }

        // Create the view based on the graphics backend
        std::unique_ptr<View> view;
        switch (_graphicsRenderer->GetBackendType()) {
        case Graphics::RendererBackend::BACKEND_D3D_11:
            view = std::make_unique<ViewD3D11>(_graphicsRenderer, this);
            break;
        default:
            Framework::Logging::GetLogger("Web")->error("Failed to create view: Unsupported renderer backend");
            return -1;
        }
        if (!view) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: failed");
            return -1;
        }

        if (!view->Init(url, width, height, offsetX, offsetY, _gpuAccelerated)) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: initialization failed");
            return -1;
        }

        _views.push_back(std::move(view));

        const auto viewId = _views.size() - 1;
        Framework::Logging::GetLogger("Web")->debug("Created view with id {}", viewId);
        return static_cast<int>(viewId);
    }

    bool Manager::DestroyView(int id) {
        if (!_cefInitialized) {
            Framework::Logging::GetLogger("Web")->error("Failed to destroy view: CEF is not initialized");
            return false;
        }

        if (id < 0 || id >= static_cast<int>(_views.size())) {
            Framework::Logging::GetLogger("Web")->error("Failed to destroy view: View does not exist");
            return false;
        }

        _views[id].reset();
        _views.erase(_views.begin() + id);

        Framework::Logging::GetLogger("Web")->debug("Destroyed view with id {}", id);
        return true;
    }

    void Manager::CleanupViews() {
        for (auto it = _views.begin(); it != _views.end();) {
            if ((*it)->IsGarbageCollected()) {
                it = _views.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    bool Manager::IsAnyViewFocused() const {
        for (const auto &view : _views) {
            if (view->HasFocus()) {
                return true;
            }
        }
        return false;
    }

    bool Manager::IsAnyGCViewFocused() const {
        for (const auto &view : _views) {
            if (view->HasFocus() && view->IsGarbageCollected()) {
                return true;
            }
        }
        return false;
    }

    std::vector<GUI::View *> Manager::GetAllViews() const {
        std::vector<GUI::View *> views;
        for (const auto &view : _views) {
            views.push_back(view.get());
        }
        return views;
    }

    std::vector<GUI::View *> Manager::GetGCViews() const {
        std::vector<GUI::View *> views;
        for (const auto &view : _views) {
            if (view->IsGarbageCollected()) {
                views.push_back(view.get());
            }
        }
        return views;
    }
} // namespace Framework::GUI
