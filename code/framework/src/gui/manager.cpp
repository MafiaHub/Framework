#include "manager.h"

#include <logging/logger.h>

#include "gui/backend/view_d3d11.h"

namespace Framework::GUI {
    Manager::Manager() {
        _clipboard = std::make_unique<SystemClipboard>();
    }

    Manager::~Manager() {
        // Destroy the views
        for (auto &view : _views) {
            view.reset();
        }

        // Destroy the renderer
        if (_renderer) {
            _renderer->Release();
        }
    }

    bool Manager::Init(const std::string& rootDir, ViewportConfiguration initialViewport, Graphics::Renderer* renderer) {
        _graphicsRenderer = renderer;

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

        // Initialize the renderer
        _renderer = ultralight::Renderer::Create();
        if (!_renderer) {
            Framework::Logging::GetLogger("Web")->error("Failed to initialize renderer");
            return false;
        }

        return true;
    }

    void Manager::Update() {
        if (!_renderer) {
            return;
        }

        // Update the renderer
        std::lock_guard lock(_renderMutex);
        _renderer->Update();
        _renderer->Render();
        _renderer->RefreshDisplay(0);

        // Update the views
        for (auto &view : _views) {
            view->Update();
        }
    }

    void Manager::Render() {
        if (!_renderer) {
            return;
        }

        // Render the views
        for (auto &view : _views) {
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

    int Manager::CreateView(std::string url, int width, int height) {
        if (!_renderer) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: Renderer is not initialized");
            return -1;
        }

        // Create the view
        std::unique_ptr<View> view;
        switch (_graphicsRenderer->GetBackendType()) {
        case Graphics::RendererBackend::BACKEND_D3D_11: 
            view = std::make_unique<ViewD3D11>(_renderer.get(), _graphicsRenderer); break;
        default: 
            Framework::Logging::GetLogger("Web")->error("Failed to create view: Unsupported renderer backend");
            return -1;
        }
        if (!view || !view.get()) {
            Framework::Logging::GetLogger("Web")->error("Failed to create view: failed");
            return -1;
        }

        if (!view->Init(url, width, height)) {
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
        if (!_renderer) {
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

        return true;
    }
} // namespace Framework::GUI
