/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "manager.h"

#include <logging/logger.h>
#include <utils/process_shutdown.h>

#include "gui/backend/view_d3d11.h"
#include "gui/backend/view_d3d12.h"

#include "include/cef_scheme.h"

#include <cstdio>
#include <filesystem>

namespace Framework::GUI {
    namespace {
        // Unbuffered shutdown trace: the spdlog file sink is unreliable
        // (per-logger sink duplication overwrites the file) and shutdown is
        // exactly where the in-game console can no longer be read. Open/append/
        // close per line so every entry survives even a hard process kill.
        void ShutdownTrace(const std::string &rootDir, const std::string &msg) {
            if (rootDir.empty()) {
                return;
            }
            FILE *f = nullptr;
            if (fopen_s(&f, (rootDir + "/logs/web_shutdown_trace.log").c_str(), "a") != 0 || !f) {
                return;
            }
            fprintf(f, "[%llu ms] %s\n", static_cast<unsigned long long>(GetTickCount64()), msg.c_str());
            fclose(f);
        }
    } // namespace
    Manager::Manager() {
        _clipboard = std::make_unique<SystemClipboard>();
    }

    Manager::~Manager() {
        if (IsInitialized()) {
            Shutdown();
        }
    }

    void Manager::Shutdown() {
        {
            std::scoped_lock lock(_renderMutex);
            ShutdownTrace(_rootDir, fmt::format("Shutdown begin (views={}, dying={}, processTeardown={})", _views.size(), _dyingViews.size(), Framework::Utils::IsProcessShutdownInProgress()));
            for (auto &view : _views) {
                view.reset();
            }
            _views.clear();
            _dyingViews.clear();
        }
        ShutdownTrace(_rootDir, "views released");

        if (_cefInitialized) {
            if (Framework::Utils::IsProcessShutdownInProgress()) {
                // Too late for a clean CefShutdown: its threads are gone and it
                // would deadlock. Let the OS reap; subprocesses watch the parent.
                Framework::Logging::GetLogger("Web")->debug("Process teardown in progress, skipping CefShutdown");
                ShutdownTrace(_rootDir, "process teardown in progress, skipping CefShutdown");
            }
            else {
                // The global request context holds references to registered
                // scheme handler factories (our CefApp); outstanding references
                // at CefShutdown time can stall it
                CefClearSchemeHandlerFactories();
                ShutdownTrace(_rootDir, "scheme handler factories cleared");

                // CloseBrowser is async — pump until every browser has been
                // through OnBeforeClose (hard cap so a wedged renderer can't
                // hold game exit hostage), then a settle pass for the teardown
                // tasks queued behind the close. The count can't miss a
                // browser: views create via CreateBrowserSync, so
                // OnAfterCreated has fired before a view can exist
                int drainedMs = 0;
                while (CEF::LifeSpanHandler::GetLiveBrowserCount() > 0 && drainedMs < 3000) {
                    CefDoMessageLoopWork();
                    Sleep(10);
                    drainedMs += 10;
                }
                for (int i = 0; i < 50; i++) {
                    CefDoMessageLoopWork();
                    Sleep(10);
                }
                ShutdownTrace(_rootDir, fmt::format("message loop drained (close wait {} ms, {} browsers left)", drainedMs, CEF::LifeSpanHandler::GetLiveBrowserCount()));

                CefShutdown();
                ShutdownTrace(_rootDir, "CefShutdown complete");
            }
            _cefInitialized = false;
        }

        Lifecycle::Shutdown();
        ShutdownTrace(_rootDir, "Shutdown end");
    }

    GUIError Manager::Init(const std::string &rootDir, ViewportConfiguration initialViewport, Graphics::Renderer *renderer, bool gpuAccelerated) {
        _graphicsRenderer = renderer;
        _gpuAccelerated   = gpuAccelerated;
        _rootDir          = rootDir;

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

        // CEF requires an absolute path for the subprocess executable. Resolve it
        // relative to the module containing this code rather than the process exe:
        // when the framework lives in a DLL injected into a game, the game's
        // install dir doesn't (and shouldn't) carry our CEF binaries.
        static const int s_moduleAnchor = 0;
        HMODULE selfModule              = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&s_moduleAnchor), &selfModule) || !selfModule) {
            // Without it GetModuleFileNameW(nullptr) would silently point the
            // subprocess path at the game EXE's dir, which doesn't carry CEF
            Framework::Logging::GetLogger("Web")->error("Failed to resolve owning module for the CEF subprocess path");
            return GUIError::GUI_CEF_INIT_FAILED;
        }
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(selfModule, exePath, MAX_PATH);
        std::filesystem::path subprocessPath = std::filesystem::path(exePath).parent_path() / "cef_subprocess.exe";
        CefString(&settings.browser_subprocess_path) = subprocessPath.wstring();

        // Create the CEF app
        _cefApp = new CEF::App();

        // Initialize CEF
        CefMainArgs mainArgs(GetModuleHandle(nullptr));
        if (!CefInitialize(mainArgs, settings, _cefApp, nullptr)) {
            Framework::Logging::GetLogger("Web")->error("Failed to initialize CEF");
            return GUIError::GUI_CEF_INIT_FAILED;
        }

        _cefInitialized = true;
        _initialized    = true;
        Framework::Logging::GetLogger("Web")->info("CEF initialized successfully");
        return GUIError::GUI_NONE;
    }

    void Manager::Update() {
        if (!_cefInitialized) {
            return;
        }

        // Pump OUTSIDE _renderMutex. The pump dispatches the game window's
        // messages mid-tick; a handler that blocks on the render thread (the
        // exit flow does an RHI flush) deadlocks against Manager::Render,
        // which takes this mutex on the render thread every frame. Seen as a
        // minutes-long exit stall crawling forward on the game's wait
        // timeouts. The pump itself never touches _views.
        CefDoMessageLoopWork();

        std::scoped_lock lock(_renderMutex);

        // Update the views
        for (auto &view : _views) {
            view->Update();
        }

        // Destroy retired views once any in-flight frames referencing their
        // textures have long drained
        constexpr int kDyingViewTicks = 8;
        for (auto it = _dyingViews.begin(); it != _dyingViews.end();) {
            if (++it->second > kDyingViewTicks) {
                it = _dyingViews.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    std::vector<GUI::View *> Manager::GetViewsByZIndex() const {
        std::vector<GUI::View *> views;
        for (const auto &view : _views) {
            views.push_back(view.get());
        }
        std::sort(views.begin(), views.end(), [](GUI::View *a, GUI::View *b) {
            return a->GetZIndex() < b->GetZIndex();
        });
        return views;
    }

    void Manager::SubmitImGuiDraws() {
        if (!_cefInitialized) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        for (auto *view : GetViewsByZIndex()) {
            view->SubmitImGuiDraw();
        }
    }

    void Manager::Render() {
        if (!_cefInitialized) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        for (auto *view : GetViewsByZIndex()) {
            view->Render();
        }
    }

    void Manager::ProcessMouseEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const {
        std::scoped_lock lock(_renderMutex);
        for (auto &view : _views) {
            view->ProcessMouseEvent(hWnd, msg, wParam, lParam);
        }
    }

    void Manager::ProcessKeyboardEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const {
        std::scoped_lock lock(_renderMutex);
        for (auto &view : _views) {
            view->ProcessKeyboardEvent(hWnd, msg, wParam, lParam);
        }
    }

    int Manager::CreateView(const std::string &url, int width, int height, int offsetX, int offsetY) {
        if (!_cefInitialized) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: CEF is not initialized");
            return -1;
        }

        // A 0x0 request means "fill the viewport" — such views follow the
        // viewport across resizes (see Manager::Resize)
        const bool autoResize = (width == 0 && height == 0);

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
        case Graphics::RendererBackend::BACKEND_D3D_12:
            view = std::make_unique<ViewD3D12>(++_id, _graphicsRenderer, this);
            break;
        default:
            Framework::Logging::GetLogger("Web")->error("Failed to create view: Unsupported renderer backend");
            return -1;
        }
        if (!view) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: failed");
            return -1;
        }

        if (view->Init(url, width, height, offsetX, offsetY, _gpuAccelerated) != GUIError::GUI_NONE) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: initialization failed");
            return -1;
        }

        view->SetAutoResize(autoResize);

        {
            std::scoped_lock lock(_renderMutex);
            _views.push_back(std::move(view));
        }

        Framework::Logging::GetLogger("Web")->debug("Created view with id {}", _id);
        return _id;
    }

    void Manager::Resize(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }

        std::scoped_lock lock(_renderMutex);
        _viewportConfiguration.width  = width;
        _viewportConfiguration.height = height;

        for (auto &view : _views) {
            if (view && view->IsAutoResize()) {
                view->Resize(width, height);
            }
        }
    }

    void Manager::RetireView(std::unique_ptr<View> view) {
        // Caller must hold _renderMutex. Hide the view so no further draws are
        // submitted, then let it age out (destroyed in Update once in-flight
        // frames that may reference its texture have drained).
        view->Display(false);
        view->Focus(false);
        _dyingViews.emplace_back(std::move(view), 0);
    }

    bool Manager::DestroyView(int id) {
        if (!_cefInitialized) {
            Framework::Logging::GetLogger("Web")->error("Failed to destroy view: CEF is not initialized");
            return false;
        }

        std::scoped_lock lock(_renderMutex);

        for (auto it = _views.begin(); it != _views.end(); ++it) {
            if ((*it)->GetId() == id) {
                RetireView(std::move(*it));
                _views.erase(it);

                Framework::Logging::GetLogger("Web")->debug("Destroyed view with id {}", id);
                return true;
            }
        }

        Framework::Logging::GetLogger("Web")->error("Failed to destroy view: View does not exist");
        return false;
    }

    void Manager::CleanupViews() {
        std::scoped_lock lock(_renderMutex);

        for (auto it = _views.begin(); it != _views.end();) {
            if ((*it)->IsGarbageCollected()) {
                RetireView(std::move(*it));
                it = _views.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    bool Manager::IsAnyViewFocused() const {
        std::scoped_lock lock(_renderMutex);
        for (const auto &view : _views) {
            if (view->HasFocus()) {
                return true;
            }
        }
        return false;
    }

    bool Manager::IsAnyGCViewFocused() const {
        std::scoped_lock lock(_renderMutex);
        for (const auto &view : _views) {
            if (view->HasFocus() && view->IsGarbageCollected()) {
                return true;
            }
        }
        return false;
    }

    std::vector<GUI::View *> Manager::GetAllViews() const {
        std::scoped_lock lock(_renderMutex);
        std::vector<GUI::View *> views;
        for (const auto &view : _views) {
            views.push_back(view.get());
        }
        return views;
    }

    std::vector<GUI::View *> Manager::GetGCViews() const {
        std::scoped_lock lock(_renderMutex);
        std::vector<GUI::View *> views;
        for (const auto &view : _views) {
            if (view->IsGarbageCollected()) {
                views.push_back(view.get());
            }
        }
        return views;
    }

    View *Manager::GetView(int id) const {
        std::scoped_lock lock(_renderMutex);
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
