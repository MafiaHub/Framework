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

#include <filesystem>

namespace Framework::GUI {
    Manager::Manager() {
        _clipboard = std::make_unique<SystemClipboard>();
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

    Utils::Result<void, Error> Manager::Init(const std::string &rootDir, ViewportConfiguration initialViewport, Graphics::Renderer *renderer, bool gpuAccelerated) {
        if (_cefInitialized) {
            return {};
        }
        _graphicsRenderer = renderer;
        _gpuAccelerated   = gpuAccelerated;

        SetViewportConfiguration(initialViewport);

        // Configure CEF settings
        CefSettings settings;
        settings.windowless_rendering_enabled = true;
        settings.multi_threaded_message_loop  = false;
        settings.no_sandbox                   = true;
        settings.log_severity                 = LOGSEVERITY_FATAL;

        // CEF >=120 holds a process-singleton lock on root_cache_path. Two clients on the
        // same machine sharing it would trigger the singleton relay (a stray blank browser
        // window) and a startup crash in the second instance. Scope the cache per-process so
        // dual-client debugging works. The path must be absolute; cache_path must equal or be
        // a child of root_cache_path.
        std::filesystem::path cacheRoot = std::filesystem::absolute(std::filesystem::path(rootDir) / "cache" / std::to_string(GetCurrentProcessId()));
        CefString(&settings.root_cache_path) = cacheRoot.wstring();
        CefString(&settings.cache_path)      = cacheRoot.wstring();
        CefString(&settings.log_file)        = rootDir + "/logs/cef.log";

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
            return Error("Failed to initialize CEF");
        }

        _cefInitialized = true;
        _initialized    = true;
        Framework::Logging::GetLogger("Web")->info("CEF initialized successfully");
        return {};
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

    int Manager::CreateView(const std::string &url, int width, int height, int offsetX, int offsetY) {
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
            view = std::make_unique<ViewD3D11>(++_id, _graphicsRenderer, this);
            break;
        default:
            Framework::Logging::GetLogger("Web")->error("Failed to create view: Unsupported renderer backend");
            return -1;
        }
        if (!view) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: failed");
            return -1;
        }

        if (auto result = view->Init(url, width, height, offsetX, offsetY, _gpuAccelerated); !result) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: {}", result.GetError().message);
            return -1;
        }

        _views.push_back(std::move(view));

        Framework::Logging::GetLogger("Web")->debug("Created view with id {}", _id);
        return _id;
    }

    bool Manager::DestroyView(int id) {
        if (!_cefInitialized) {
            Framework::Logging::GetLogger("Web")->error("Failed to destroy view: CEF is not initialized");
            return false;
        }

        int index = -1;
        int i     = 0;

        for (auto it = _views.begin(); it != _views.end(); ++it, ++i) {
            if ((*it)->GetId() == id) {
                index = i;
                break;
            }
        }

        if (index == -1) {
            Framework::Logging::GetLogger("Web")->error("Failed to destroy view: View does not exist");
            return false;
        }

        _views[index].reset();
        _views.erase(_views.begin() + index);

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

    View *Manager::GetView(int id) const {
        for (auto it = _views.begin(); it != _views.end(); ++it) {
            if ((*it)->GetId() == id) {
                return it->get();
            }
        }
        return nullptr;
    }

    void Manager::RegisterSchemeHandlerFactory(const std::string &schema, const std::string &domain, Framework::GUI::CEF::SchemaHandlerFactoryCallback callback) {
        _cefApp->RegisterSchemeHandlerFactory(schema, domain, callback);
        CefRegisterSchemeHandlerFactory(schema, domain, _cefApp);
    }
} // namespace Framework::GUI
