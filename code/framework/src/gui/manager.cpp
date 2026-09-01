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
#include <utils/profiler.h>

#include "gui/backend/view_d3d11.h"
#include "gui/backend/view_d3d12.h"
#include "gui/backend/view_d3d9.h"
#include "gui/resources/scheme.h"

#include "include/cef_scheme.h"

#include <imgui/imgui.h>

#include <filesystem>
#include <optional>
#include <utility>

namespace Framework::GUI {
    namespace {
        constexpr std::size_t kMaxCefCacheProfiles = 16;

        // ImGui has no wait/help/crosshair/zoom shapes; those fall back to the arrow.
        ImGuiMouseCursor ToImGuiCursor(cef_cursor_type_t type) {
            switch (type) {
            case CT_IBEAM:
            case CT_VERTICALTEXT: return ImGuiMouseCursor_TextInput;
            case CT_HAND:
            case CT_GRAB:
            case CT_GRABBING: return ImGuiMouseCursor_Hand;
            case CT_NODROP:
            case CT_NOTALLOWED:
            case CT_DND_NONE: return ImGuiMouseCursor_NotAllowed;
            case CT_CELL:
            case CT_MIDDLEPANNING:
            case CT_MOVE: return ImGuiMouseCursor_ResizeAll;
            case CT_MIDDLE_PANNING_VERTICAL:
            case CT_NORTHRESIZE:
            case CT_NORTHSOUTHRESIZE:
            case CT_ROWRESIZE:
            case CT_SOUTHRESIZE: return ImGuiMouseCursor_ResizeNS;
            case CT_COLUMNRESIZE:
            case CT_EASTRESIZE:
            case CT_EASTWESTRESIZE:
            case CT_MIDDLE_PANNING_HORIZONTAL:
            case CT_WESTRESIZE: return ImGuiMouseCursor_ResizeEW;
            case CT_NORTHEASTRESIZE:
            case CT_NORTHEASTSOUTHWESTRESIZE:
            case CT_SOUTHWESTRESIZE: return ImGuiMouseCursor_ResizeNESW;
            case CT_NORTHWESTRESIZE:
            case CT_NORTHWESTSOUTHEASTRESIZE:
            case CT_SOUTHEASTRESIZE: return ImGuiMouseCursor_ResizeNWSE;
            case CT_NONE: return ImGuiMouseCursor_None;
            default: return ImGuiMouseCursor_Arrow;
            }
        }

        // Guard the pump: some exit paths tear CEF down before our Shutdown runs.
        // TODO(cef-exit): an exit-path-independent Shutdown trigger would remove this.
        bool PumpCefMessageLoopGuarded() {
            __try {
                CefDoMessageLoopWork();
                return true;
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                return false;
            }
        }

        std::optional<std::pair<std::filesystem::path, HANDLE>> ClaimCefCacheProfile(const std::string &rootDir) {
            std::error_code error;
            const std::filesystem::path profilesRoot = std::filesystem::absolute(std::filesystem::path(rootDir) / "cache" / "profiles", error);
            if (error) {
                return std::nullopt;
            }

            for (std::size_t index = 0; index < kMaxCefCacheProfiles; ++index) {
                const std::filesystem::path profileRoot = profilesRoot / std::to_string(index);
                std::filesystem::create_directories(profileRoot, error);
                if (error) {
                    return std::nullopt;
                }

                const std::filesystem::path lockPath = profileRoot / ".framework-profile.lock";
                HANDLE profileLock = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
                if (profileLock != INVALID_HANDLE_VALUE) {
                    return std::make_pair(profileRoot, profileLock);
                }
            }

            return std::nullopt;
        }
    } // namespace
    Manager::Manager() {
        _clipboard        = std::make_unique<SystemClipboard>();
        _resourceRegistry = new Resources::ResourceRegistry();
    }

    Manager::~Manager() {
        if (IsInitialized()) {
            Shutdown();
        }
    }

    void Manager::Shutdown() {
        bool cefStopped = false;
        {
            std::scoped_lock lock(_renderMutex);
            for (auto &view : _views) {
                view.reset();
            }
            _views.clear();
            _dyingViews.clear();
        }

        if (_cefInitialized && _cefPumpFailed) {
            Framework::Logging::GetLogger("Web")->debug("CEF pump previously faulted, skipping CefShutdown");
            _cefInitialized = false;
        }
        else if (_cefInitialized) {
            if (Framework::Utils::IsProcessShutdownInProgress()) {
                // CEF's threads are already gone; CefShutdown would deadlock.
                Framework::Logging::GetLogger("Web")->debug("Process teardown in progress, skipping CefShutdown");
            }
            else {
                CefClearSchemeHandlerFactories();

                // CloseBrowser is async: pump (guarded) until every browser hits
                // OnBeforeClose, 3s cap, then a settle pass.
                bool pumpOk   = true;
                int drainedMs = 0;
                while (pumpOk && CEF::LifeSpanHandler::GetLiveBrowserCount() > 0 && drainedMs < 3000) {
                    pumpOk = PumpCefMessageLoopGuarded();
                    Sleep(10);
                    drainedMs += 10;
                }
                for (int i = 0; pumpOk && i < 50; i++) {
                    pumpOk = PumpCefMessageLoopGuarded();
                    Sleep(10);
                }

                if (pumpOk) {
                    CefShutdown();
                    cefStopped = true;
                }
                else {
                    _cefPumpFailed = true;
                    Framework::Logging::GetLogger("Web")->warn("CEF pump faulted during shutdown drain, skipping CefShutdown");
                }
            }
            _cefInitialized = false;
        }

        // A skipped CefShutdown can leave CEF owning the profile until process
        // exit. Keep our lock too so another client cannot claim that root.
        if (cefStopped && _cacheProfileLock != INVALID_HANDLE_VALUE) {
            CloseHandle(_cacheProfileLock);
            _cacheProfileLock = INVALID_HANDLE_VALUE;
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
        settings.log_severity                 = LOGSEVERITY_ERROR;

        CefString(&settings.log_file) = rootDir + "/logs/cef.log";

        // CEF requires an absolute path for the subprocess executable
        // Resolve next to THIS module (injected DLL), not the process exe.
        static const int s_moduleAnchor = 0;
        HMODULE selfModule              = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&s_moduleAnchor), &selfModule) || !selfModule) {
            Framework::Logging::GetLogger("Web")->error("Failed to resolve owning module for the CEF subprocess path");
            return Error("Failed to resolve owning module for the CEF subprocess path");
        }
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(selfModule, exePath, MAX_PATH);
        std::filesystem::path subprocessPath = std::filesystem::path(exePath).parent_path() / "cef_subprocess.exe";
        CefString(&settings.browser_subprocess_path) = subprocessPath.wstring();

        // CEF holds a process-singleton lock on root_cache_path. Stable numbered
        // profiles keep browser storage across launches while the extra lock
        // gives concurrent clients different roots. The OS releases it after a
        // crash, so the lowest available profile is reusable on the next run.
        const auto cacheProfile = ClaimCefCacheProfile(rootDir);
        if (!cacheProfile) {
            return Error("Failed to claim a persistent CEF cache profile");
        }
        const std::filesystem::path &cacheRoot = cacheProfile->first;
        _cacheProfileLock                     = cacheProfile->second;
        CefString(&settings.root_cache_path)  = cacheRoot.wstring();
        CefString(&settings.cache_path)       = cacheRoot.wstring();

        // Create the CEF app
        _cefApp = new CEF::App();
        _cefApp->SetGPUAccelerated(gpuAccelerated);

        // Initialize CEF
        CefMainArgs mainArgs(GetModuleHandle(nullptr));
        if (!CefInitialize(mainArgs, settings, _cefApp, nullptr)) {
            CloseHandle(_cacheProfileLock);
            _cacheProfileLock = INVALID_HANDLE_VALUE;
            return Error("Failed to initialize CEF");
        }

        // One factory for the whole scheme; an empty domain matches every host
        // and the registry routes them, so CEF never learns about a root.
        CefRegisterSchemeHandlerFactory(Resources::kResourceScheme, "", _resourceRegistry);

        _cefInitialized = true;
        _initialized    = true;
        Framework::Logging::GetLogger("Web")->debug("Using persistent CEF cache profile '{}'", cacheRoot.string());
        Framework::Logging::GetLogger("Web")->info("CEF initialized successfully");
        return {};
    }

    void Manager::Update() {
        if (!_cefInitialized || _cefPumpFailed) {
            return;
        }

        // Pump OUTSIDE _renderMutex: it dispatches window messages that can block
        // on the render thread (exit-flow RHI flush) and deadlock against Render.
        {
            // OnPaint runs inside here, on this thread: its zone nests under this one.
            FW_PROFILE_SCOPE_N("Cef::Pump");
            if (!PumpCefMessageLoopGuarded()) {
                _cefPumpFailed = true;
                Framework::Logging::GetLogger("Web")->warn("CEF message pump faulted (exit teardown race) — disabling further pumps");
                return;
            }
        }

        // Render() takes the same mutex from the Present hook, so this can be a stall
        // rather than work; time it separately from what it guards.
        std::unique_lock<std::recursive_mutex> lock;
        {
            FW_PROFILE_SCOPE_N("Cef::LockWait");
            lock = std::unique_lock<std::recursive_mutex>(_renderMutex);
        }

        // One external begin frame per render frame bounds CEF's cadence to our loop.
        {
            FW_PROFILE_SCOPE_N("Cef::BeginFrames");
            int visible = 0;
            for (auto &view : _views) {
                view->RequestBeginFrame();
                view->Update();
                visible += view->ShouldDisplay() ? 1 : 0;
            }
            FW_PROFILE_PLOT("cef.views", static_cast<int64_t>(_views.size()));
            FW_PROFILE_PLOT("cef.views.visible", static_cast<int64_t>(visible));
        }

        // Free retired views once in-flight frames referencing their textures drained
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
        if (!_cefInitialized || _cefPumpFailed) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        const View *cursorOwner = nullptr;
        for (auto *view : GetViewsByZIndex()) {
            view->SubmitImGuiDraw();
            if (view->HasFocus() && view->ShouldDisplay()) {
                cursorOwner = view;
            }
        }

        // Blink's cursor is set on the OS cursor, which a host drawing ImGui's software cursor
        // never shows, so mirror the focused view's shape onto that instead. Topmost focused view
        // wins; ImGui resets to the arrow each frame, so this only has to hold while one is up.
        if (cursorOwner && ImGui::GetCurrentContext()) {
            ImGui::SetMouseCursor(ToImGuiCursor(cursorOwner->GetCursorType()));
        }
    }

    void Manager::Render() {
        if (!_cefInitialized || _cefPumpFailed) {
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

        // 0x0 means "fill the viewport" and follow it across resizes
        const bool autoResize = (width == 0 && height == 0);

        const auto viewport = GetViewportConfiguration();
        if (width == 0) {
            width = viewport.width;
        }

        if (height == 0) {
            height = viewport.height;
        }

        // Create the view based on the graphics backend
        std::unique_ptr<View> view;
        switch (_graphicsRenderer->GetBackendType()) {
        case Graphics::RendererBackend::BACKEND_D3D_9:
            view = std::make_unique<ViewD3D9>(++_id, _graphicsRenderer, this);
            break;
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

        if (auto result = view->Init(url, width, height, offsetX, offsetY, _gpuAccelerated); !result) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: {}", result.GetError().message);
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

        // Callers hand us a size per window message, so the same size arrives many
        // times over a drag-resize. Re-applying it costs a CEF relayout per view and,
        // on the next Render(), a GPU drain plus texture reallocation.
        if (width == _viewportConfiguration.width && height == _viewportConfiguration.height) {
            return;
        }

        _viewportConfiguration.width  = width;
        _viewportConfiguration.height = height;

        for (auto &view : _views) {
            if (view && view->IsAutoResize()) {
                view->Resize(width, height);
            }
        }
    }

    void Manager::RetireView(std::unique_ptr<View> view) {
        // Caller holds _renderMutex. Hide it, then let it age out in Update.
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

    void Manager::RegisterResourceRoot(const std::string &host, std::shared_ptr<Resources::ResourceProvider> provider) {
        _resourceRegistry->RegisterRoot(host, std::move(provider));
    }

    void Manager::RegisterResourceDirectory(const std::string &host, const std::filesystem::path &root) {
        RegisterResourceRoot(host, std::make_shared<Resources::DirectoryProvider>(root));
    }

    bool Manager::UnregisterResourceRoot(const std::string &host) {
        return _resourceRegistry->UnregisterRoot(host);
    }

    std::string Manager::ResourceURL(const std::string &host, const std::string &path) {
        return Resources::MakeResourceURL(host, path);
    }
} // namespace Framework::GUI
