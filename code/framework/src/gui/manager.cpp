/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "manager.h"

#include <logging/logger.h>
#include <utils/path.h>
#include <utils/process_shutdown.h>
#include <utils/profiler.h>

#include "gui/backend/view_d3d11.h"
#include "gui/backend/view_d3d12.h"
#include "gui/backend/view_d3d8.h"
#include "gui/backend/view_d3d9.h"
#include "gui/cef/runtime.h"
#include "gui/remote/launch.h"
#include "gui/remote/remote_session.h"
#include "gui/remote/remote_view.h"
#include "gui/resources/scheme.h"

#include "include/cef_scheme.h"

#include <imgui.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace Framework::GUI {
    namespace {
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

        if (_remote) {
            _remote->Stop();
            _remote.reset();
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
                bool pumpFailed = false;
                cefStopped      = CEF::StopRuntime(pumpFailed);
                if (pumpFailed) {
                    _cefPumpFailed = true;
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
        if (_cefInitialized || _remote) {
            return {};
        }
        _graphicsRenderer = renderer;
        _gpuAccelerated   = gpuAccelerated;

        SetViewportConfiguration(initialViewport);

        // Resolve next to THIS module (injected DLL), not the process exe.
        const std::wstring moduleDirName = Framework::Utils::GetModuleDirW();
        if (moduleDirName.empty()) {
            Framework::Logging::GetLogger("Web")->error("Failed to resolve owning module for the CEF subprocess path");
            return Error("Failed to resolve owning module for the CEF subprocess path");
        }
        const std::filesystem::path moduleDir = moduleDirName;

        if (WantsRemoteHost(renderer)) {
            auto result = InitRemote(rootDir, moduleDir);
            if (result) {
                _initialized = true;
                return {};
            }
            // A machine that cannot run the 64-bit host (a 32-bit Wine prefix) still gets a UI when
            // the in-process runtime ships alongside.
            std::error_code error;
            if (!std::filesystem::is_regular_file(moduleDir / "libcef.dll", error)) {
                return result;
            }
            Framework::Logging::GetLogger("Web")->warn("{}; falling back to CEF in this process", result.GetError().message);
        }

        // Create the CEF app
        _cefApp = new CEF::App();
        _cefApp->SetGPUAccelerated(gpuAccelerated);

        if (!CEF::StartRuntime(rootDir, moduleDir, _cefApp, _resourceRegistry, _cacheProfileLock)) {
            return Error("Failed to initialize CEF");
        }

        _cefInitialized = true;
        _initialized    = true;
        Framework::Logging::GetLogger("Web")->info("CEF initialized successfully");
        return {};
    }

    bool Manager::WantsRemoteHost(const Graphics::Renderer *renderer) const {
#ifdef FW_CEF_REMOTE_DEFAULT
        bool remote = true;
#else
        bool remote = false;
#endif
        if (const char *mode = std::getenv("FW_CEF_MODE")) {
            if (std::string_view(mode) == "remote") {
                remote = true;
            }
            else if (std::string_view(mode) == "inprocess") {
                remote = false;
            }
        }
        // The host paints on the CPU, and only the D3D8 backend composites CPU frames from it.
        if (remote && renderer && renderer->GetBackendType() != Graphics::RendererBackend::BACKEND_D3D_8) {
            Framework::Logging::GetLogger("Web")->info("Remote CEF composites on Direct3D 8 only; running CEF in this process");
            return false;
        }
        return remote;
    }

    Utils::Result<void, Error> Manager::InitRemote(const std::string &rootDir, const std::filesystem::path &moduleDir) {
        const std::filesystem::path hostPath = moduleDir / Remote::kHostDirectory / Remote::kHostExecutable;
        std::error_code error;
        if (!std::filesystem::is_regular_file(hostPath, error)) {
            return Error("The CEF host is missing at '" + hostPath.string() + "'");
        }

        auto session = std::make_unique<Remote::RemoteSession>(_resourceRegistry.get());
        if (auto result = session->Start(rootDir, hostPath); !result) {
            return result;
        }
        _remote = std::move(session);
        Framework::Logging::GetLogger("Web")->info("CEF runs in the host process '{}'", hostPath.string());
        return {};
    }

    Remote::RemoteView *Manager::GetRemoteView(int id) const {
        // Every view is a RemoteView in remote mode.
        return _remote ? static_cast<Remote::RemoteView *>(GetView(id)) : nullptr;
    }

    void Manager::UpdateRemote() {
        // Dispatched outside _renderMutex, as the in-process pump runs its handlers.
        Remote::RemoteSession::Listener listener;
        listener.onMessage = [this](const Remote::HostMessage &message) {
            if (const auto *event = std::get_if<Remote::Protocol::ViewEventMessage>(&message)) {
                if (auto *view = GetRemoteView(event->viewId)) {
                    view->OnHostEvent(event->data);
                }
            }
            else if (const auto *page = std::get_if<Remote::Protocol::PageEvent>(&message)) {
                if (auto *view = GetRemoteView(page->viewId)) {
                    view->OnHostPageEvent(page->name, page->payload);
                }
            }
            else if (const auto *surface = std::get_if<Remote::Protocol::FrameSurface>(&message)) {
                if (auto *view = GetRemoteView(surface->viewId)) {
                    view->OnHostSurface(surface->generation, surface->width, surface->height);
                }
            }
        };
        listener.onHostLost = [this] {
            std::scoped_lock lock(_renderMutex);
            for (auto &view : _views) {
                static_cast<Remote::RemoteView *>(view.get())->OnHostLost();
            }
        };
        listener.onHostReady = [this] {
            std::scoped_lock lock(_renderMutex);
            for (auto &view : _views) {
                static_cast<Remote::RemoteView *>(view.get())->OnHostReady();
            }
        };
        _remote->Poll(listener);
    }

    void Manager::Update() {
        if (_remote) {
            FW_PROFILE_SCOPE_N("Cef::Remote");
            UpdateRemote();
        }
        else if (!_cefInitialized || _cefPumpFailed) {
            return;
        }
        else {
            // Pump OUTSIDE _renderMutex: it dispatches window messages that can block
            // on the render thread (exit-flow RHI flush) and deadlock against Render.
            // OnPaint runs inside here, on this thread: its zone nests under this one.
            FW_PROFILE_SCOPE_N("Cef::Pump");
            if (!CEF::PumpMessageLoopGuarded()) {
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
                ReconcileSuppression(view.get());
                view->CheckNavigationStarted();
                view->RequestBeginFrame();
                view->Update();
                visible += view->IsOnScreen() ? 1 : 0;
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

    void Manager::SetCompositingSuppressed(bool suppressed) {
        _compositingSuppressed.store(suppressed, std::memory_order_release);
    }

    bool Manager::IsCompositingSuppressed() const {
        return _compositingSuppressed.load(std::memory_order_acquire);
    }

    bool Manager::IsBackendReady() const {
        return _remote || (_cefInitialized && !_cefPumpFailed);
    }

    bool Manager::IsViewComposited(const View *view) const {
        return !IsCompositingSuppressed() || view->AlwaysComposites();
    }

    void Manager::ReconcileSuppression(View *view) {
        // Per view per tick, not on the suppression edge: a view created or focused while
        // suppression is up has to be caught too.
        const bool composited = IsViewComposited(view);
        view->SetAudioMuted(!composited);
        if (!composited) {
            if (view->HasFocus()) {
                _focusSuspended.insert(view->GetId());
                view->Focus(false); // a real blur, so the page gets its keyup instead of nothing
            }
            return;
        }
        if (_focusSuspended.erase(view->GetId()) > 0) {
            view->Focus(true);
        }
    }

    void Manager::SubmitImGuiDraws() {
        if (!IsBackendReady()) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        const View *cursorOwner = nullptr;
        for (auto *view : GetViewsByZIndex()) {
            view->SubmitImGuiDraw();
            if (view->HasFocus() && view->IsOnScreen()) {
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
        if (!IsBackendReady()) {
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
        if (!_cefInitialized && !_remote) {
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
        if (_remote) {
            view = std::make_unique<Remote::RemoteView>(++_id, _graphicsRenderer, this, _remote.get());
        }
        else {
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
            case Graphics::RendererBackend::BACKEND_D3D_8:
                view = std::make_unique<ViewD3D8>(++_id, _graphicsRenderer, this);
                break;
            default:
                Framework::Logging::GetLogger("Web")->error("Failed to create view: Unsupported renderer backend");
                return -1;
            }
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
        _focusSuspended.erase(view->GetId());
        view->Display(false);
        view->Focus(false);
        _dyingViews.emplace_back(std::move(view), 0);
    }

    bool Manager::DestroyView(int id) {
        if (!_cefInitialized && !_remote) {
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
        if (_remote) {
            // The callback makes CEF objects in this process; serve the content through RegisterResourceRoot instead.
            Framework::Logging::GetLogger("Web")->error("Scheme handler {}://{} ignored: CEF runs in the host process", schema, domain);
            return;
        }
        _cefApp->RegisterSchemeHandlerFactory(schema, domain, callback);
        CefRegisterSchemeHandlerFactory(schema, domain, _cefApp);
    }

    void Manager::RegisterResourceRoot(const std::string &host, std::shared_ptr<Resources::ResourceProvider> provider) {
        _resourceRegistry->RegisterRoot(host, std::move(provider));
        if (_remote) {
            _remote->RegisterRoot(host);
        }
    }

    void Manager::RegisterResourceDirectory(const std::string &host, const std::filesystem::path &root) {
        RegisterResourceRoot(host, std::make_shared<Resources::DirectoryProvider>(root));
    }

    bool Manager::UnregisterResourceRoot(const std::string &host) {
        if (_remote) {
            _remote->UnregisterRoot(host);
        }
        return _resourceRegistry->UnregisterRoot(host);
    }

    std::string Manager::ResourceURL(const std::string &host, const std::string &path) {
        return Resources::MakeResourceURL(host, path);
    }
} // namespace Framework::GUI
