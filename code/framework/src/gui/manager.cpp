#include "manager.h"

#include <logging/logger.h>

#include "gui/backend/view_d3d11.h"

#include <core_modules.h>


namespace Framework::GUI {
    Manager::Manager() {
        _clipboard = std::make_unique<SystemClipboard>();
        _updateCooldown = Utils::Time::GetTimePoint() + std::chrono::milliseconds(UPDATE_COOLDOWN_MS);

        CoreModules::SetWebManager(this);
    }

    Manager::~Manager() {
        // Destroy the views
        for (auto &view : _views) {
            view.reset();
        }

        // Destroy the Ultralight renderer
        if (_ultralightRenderer) {
            _ultralightRenderer->Release();
        }
    }

    bool Manager::Init(const std::string &rootDir, ViewportConfiguration initialViewport, Graphics::Renderer *renderer, bool gpu_accelerated) {
        _graphicsRenderer = renderer;
        _gpuAccelerated   = gpu_accelerated;

        SetViewportConfiguration(initialViewport);

        // Initialize the configuration
        ultralight::Config rendererConfig;
        rendererConfig.cache_path    = (rootDir + "/cache").c_str();

        // Initialize the platform
        ultralight::Platform::instance().set_config(rendererConfig);
        ultralight::Platform::instance().set_clipboard(_clipboard.get());
        ultralight::Platform::instance().set_font_loader(ultralight::GetPlatformFontLoader());
        ultralight::Platform::instance().set_file_system(ultralight::GetPlatformFileSystem(rootDir.c_str()));
        ultralight::Platform::instance().set_logger(ultralight::GetDefaultLogger((rootDir + "/logs/web_manager.log").c_str()));

        // Initialise backend renderer for Ultralight
        switch (_graphicsRenderer->GetBackendType()) {
            case Graphics::RendererBackend::BACKEND_D3D_11: ViewD3D11::InitRenderer(_graphicsRenderer); break;
            default: break;
        }
        
        // Initialize the ultralight renderer
        _ultralightRenderer = ultralight::Renderer::Create();
        if (!_ultralightRenderer) {
            Framework::Logging::GetLogger("Web")->error("Failed to initialize renderer");
            return false;
        }

        return true;
    }

    void Manager::Update() {
        if (!_ultralightRenderer) {
            return;
        }

        if (Utils::Time::Compare(_updateCooldown, Utils::Time::GetTimePoint()) >= 0) {
            return;
        }

        // Update the renderer
        std::lock_guard lock(_renderMutex);
        _ultralightRenderer->Update();
        _ultralightRenderer->RefreshDisplay(0);
        _ultralightRenderer->Render();

        // Update the views
        for (auto &view : _views) {
            view->Update();
        }
    }

    void Manager::Render() {
        if (!_ultralightRenderer) {
            return;
        }

        // Update the views
        std::vector<GUI::View *> views;
        for (auto &view : _views) {
            views.push_back(view.get());
        }
        std::sort(views.begin(), views.end(), [](GUI::View *a, GUI::View *b) {
            return a->GetZIndex() < b->GetZIndex();
        });
        
        std::lock_guard lock(_renderMutex);

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

    int Manager::CreateView(std::string url, int width, int height, int offset_x, int offset_y) {
        if (!_ultralightRenderer) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: Renderer is not initialized");
            return -1;
        }

        if (width == 0) {
            width = _viewportConfiguration.width;
        }

        if (height == 0) {
            height = _viewportConfiguration.height;
        }

        // Create the view
        std::unique_ptr<View> view;
        switch (_graphicsRenderer->GetBackendType()) {
        case Graphics::RendererBackend::BACKEND_D3D_11: 
            view = std::make_unique<ViewD3D11>(_ultralightRenderer.get(), _graphicsRenderer, this); break;
        default: 
            Framework::Logging::GetLogger("Web")->error("Failed to create view: Unsupported renderer backend");
            return -1;
        }
        if (!view || !view.get()) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: failed");
            return -1;
        }

        if (!view->Init(url, width, height, offset_x, offset_y, _gpuAccelerated)) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: initialization failed");
            return -1;
        }

        // Add the view to the list
        _views.push_back(std::move(view));

        // Return the view id
        const auto viewId = _views.size() - 1;

        // Log the view creation
        Framework::Logging::GetLogger("Web")->debug("Created view with id {}", viewId);
        return viewId;
    }

    bool Manager::DestroyView(int id) {
        if (!_ultralightRenderer) {
            Framework::Logging::GetLogger("Web")->error("Failed to destroy view: Renderer is not initialized");
            return false;
        }

        // Check if the view exists
        if (id < 0 || id >= _views.size()) {
            Framework::Logging::GetLogger("Web")->error("Failed to destroy view: View does not exist");
            return false;
        }

        // Destroy the view
        _views[id].reset();

        // Remove the view from the list
        _views.erase(_views.begin() + id);

        Framework::Logging::GetLogger("Web")->debug("Destroyed view with id {}", id);

        _updateCooldown = Utils::Time::GetTimePoint() + std::chrono::milliseconds(UPDATE_COOLDOWN_MS);

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

    std::vector<GUI::View*> Manager::GetAllViews() const {
        std::vector<GUI::View*> views;
        for (const auto &view : _views) {
            views.push_back(view.get());
        }
        return views;
    }

    std::vector<GUI::View*> Manager::GetGCViews() const {
        std::vector<GUI::View*> views;
        for (const auto &view : _views) {
            if (view->IsGarbageCollected()) {
                views.push_back(view.get());
            }
        }
        return views;
    }
} // namespace Framework::GUI
