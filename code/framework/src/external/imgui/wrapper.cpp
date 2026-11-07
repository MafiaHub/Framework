/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include "font_fallback.h"
#include "graphics/renderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <logging/logger.h>
#include <utility>

#include <imgui.h>

#include <imgui_impl_dx11.h>
#ifdef FW_IMGUI_DX12
#include <imgui_impl_dx12.h>
#endif
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include "graphics/backend/d3d11.h"
#include "graphics/backend/d3d12.h"
#include "graphics/backend/d3d9.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Framework::External::ImGUI {
    namespace {
#ifdef FW_IMGUI_DX12
        // Give ImGui 1.92's dynamic font atlas (RendererHasTextures) the backend's SRV slot
        // pool; the legacy single-descriptor init clears the flag and asserts on atlas rebuild.
        void ImGuiAllocSRV(ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE *outCpu, D3D12_GPU_DESCRIPTOR_HANDLE *outGpu) {
            auto *backend  = static_cast<Graphics::D3D12Backend *>(info->UserData);
            const int slot = backend->AllocateSRVSlot();
            IM_ASSERT(slot >= 0 && "D3D12 SRV descriptor heap exhausted");
            if (slot < 0) {
                *outCpu = {};
                *outGpu = {};
                return;
            }
            *outCpu = backend->GetSRVSlotCPUHandle(slot);
            *outGpu = backend->GetSRVSlotGPUHandle(slot);
        }

        void ImGuiFreeSRV(ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
            auto *backend       = static_cast<Graphics::D3D12Backend *>(info->UserData);
            const auto base     = backend->GetSRVHeap()->GetCPUDescriptorHandleForHeapStart();
            const UINT descSize = backend->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            backend->FreeSRVSlot(static_cast<int>((cpu.ptr - base.ptr) / descSize)); // slot = handle offset from heap start
        }
#endif
    } // namespace

    Utils::Result<void, Framework::Error> Wrapper::Init(Config &config) {
        if (isContextInitialized) {
            return {};
        }

        _config = config;

        if (!_config.renderer) {
            return Framework::Error("ImGui renderer is not set");
        }

        if (!_config.windowHandle && _config.windowBackend == Graphics::PlatformBackend::PLATFORM_WIN32) {
            return Framework::Error("ImGui window handle is not set");
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();

        io.ConfigWindowsResizeFromEdges = true;

        // The launcher opts into per-monitor DPI awareness, so Windows no longer bitmap-scales
        // the UI for us. Scale both fonts and style metrics from the window's monitor instead.
        if (_config.windowBackend == Graphics::PlatformBackend::PLATFORM_WIN32) {
            const float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(_config.windowHandle);
            if (dpiScale > 0.0f) {
                ImGui::GetStyle().FontScaleDpi = dpiScale;
                ImGui::GetStyle().ScaleAllSizes(dpiScale);
            }
        }

        // Load the optional UI font before the first frame. ImGui 1.92 rasterizes
        // glyphs on demand, so any script the font covers renders without baking
        // explicit ranges. Falls back to the embedded font when unset or on failure.
        if (!_config.fontPath.empty()) {
            ImFontConfig fontCfg;
            fontCfg.Flags |= ImFontFlags_NoLoadError; // return null on failure instead of asserting (debug builds)
            if (!io.Fonts->AddFontFromFileTTF(_config.fontPath.c_str(), _config.fontSize, &fontCfg)) {
                Logging::GetLogger("ImGui")->warn("Failed to load UI font '{}', using default", _config.fontPath);
                io.Fonts->AddFontDefault();
            }
        }

        // The first font in the atlas is the default one, so a mod font must never take that
        // slot when no UI font was configured.
        if (!_config.fonts.empty() && io.Fonts->Fonts.empty()) {
            io.Fonts->AddFontDefault();
        }
        for (const FontSource &source : _config.fonts) {
            ImFontConfig fontCfg;
            fontCfg.Flags |= ImFontFlags_NoLoadError;
            if (ImFont *font = io.Fonts->AddFontFromFileTTF(source.path.c_str(), 0.0f, &fontCfg)) {
                _fonts[source.name] = font;
            }
            else {
                Logging::GetLogger("ImGui")->warn("Failed to load font '{}' from '{}', its users draw with the default font", source.name, source.path);
            }
        }

        ImGui::StyleColorsDark();

        switch (_config.renderBackend) {
        case Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_Init(_config.renderer->GetD3D9Backend()->GetDevice());
        } break;
        case Graphics::RendererBackend::BACKEND_D3D_11: {
            const auto renderBackend = _config.renderer->GetD3D11Backend();
            ImGui_ImplDX11_Init(renderBackend->GetDevice(), renderBackend->GetContext());
        } break;
#ifdef FW_IMGUI_DX12
        case Graphics::RendererBackend::BACKEND_D3D_12: {
            InitDX12Backend();
        } break;
#endif
        }

        switch (_config.windowBackend) {
        case Graphics::PlatformBackend::PLATFORM_WIN32: {
            ImGui_ImplWin32_Init(_config.windowHandle);
        } break;
        }

        _initialized = isContextInitialized = true;
        return {};
    }

    void Wrapper::Shutdown() {
        if (!isContextInitialized) {
            return;
        }

        switch (_config.renderBackend) {
        case Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_Shutdown();
        } break;
        case Graphics::RendererBackend::BACKEND_D3D_11: {
            ImGui_ImplDX11_Shutdown();
        } break;
#ifdef FW_IMGUI_DX12
        case Graphics::RendererBackend::BACKEND_D3D_12: {
            if (!_config.renderer->GetD3D12Backend()->WaitForGpu()) {
                Logging::GetLogger("ImGui")->error("GPU drain failed; retaining DX12 UI resources");
                return;
            }
            _dx12Compositor.Reset();
            ImGui_ImplDX12_Shutdown();
        } break;
#endif
        }

        switch (_config.windowBackend) {
        case Graphics::PlatformBackend::PLATFORM_WIN32: {
            ImGui_ImplWin32_Shutdown();
        } break;
        }

        ImGui::DestroyContext();
        _fonts.clear(); // the atlas that owned them is gone
        {
            std::scoped_lock lock(_familyMtx);
            _pendingFamilies.clear();
            _clearFamilies = false;
            _families.clear();
        }
        _runtimeFaces.clear();
        _runtimeFontBytes = 0;

        isContextInitialized = false;
        Lifecycle::Shutdown();
    }

    void Wrapper::Update() {
        std::scoped_lock _lock(_renderMtx);

        // Between frames, so no draw list of this frame holds a face being added.
        LoadPendingFamilies();

        switch (_config.renderBackend) {
        case Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_NewFrame();
        } break;
        case Graphics::RendererBackend::BACKEND_D3D_11: {
            ImGui_ImplDX11_NewFrame();
        } break;
#ifdef FW_IMGUI_DX12
        case Graphics::RendererBackend::BACKEND_D3D_12: {
            SyncDX12RtvFormat();
            ImGui_ImplDX12_NewFrame();
        } break;
#endif
        }

        switch (_config.windowBackend) {
        case Graphics::PlatformBackend::PLATFORM_WIN32: {
            ImGui_ImplWin32_NewFrame();
        } break;
        }

        ScaleToBackBuffer();

        ImGui::NewFrame();

        // process all widgets
        while (!_renderQueue.empty()) {
            const auto &proc = _renderQueue.front();
            proc();
            _renderQueue.pop();
        }

        ImGui::Render();
    }

#ifdef FW_IMGUI_DX12
    void Wrapper::InitDX12Backend() {
        const auto renderBackend = _config.renderer->GetD3D12Backend();
        const auto rtvFormat     = Graphics::D3D12UICompositor::RenderFormat(renderBackend->GetBackBufferFormat());
        _dx12RtvFormat           = static_cast<int>(rtvFormat);
        Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->info("ImGui DX12 pipeline format {}, back buffer format {}", _dx12RtvFormat, static_cast<int>(renderBackend->GetBackBufferFormat()));

        ImGui_ImplDX12_InitInfo initInfo {};
        initInfo.Device               = renderBackend->GetDevice();
        initInfo.CommandQueue         = renderBackend->GetCommandQueue();
        initInfo.NumFramesInFlight    = renderBackend->NumFramesInFlight();
        initInfo.RTVFormat            = rtvFormat;
        initInfo.SrvDescriptorHeap    = renderBackend->GetSRVHeap();
        initInfo.UserData             = renderBackend;
        initInfo.SrvDescriptorAllocFn = ImGuiAllocSRV;
        initInfo.SrvDescriptorFreeFn  = ImGuiFreeSRV;
        ImGui_ImplDX12_Init(&initInfo);
    }

    void Wrapper::SyncDX12RtvFormat() {
        const auto renderBackend = _config.renderer->GetD3D12Backend();
        if (static_cast<int>(Graphics::D3D12UICompositor::RenderFormat(renderBackend->GetBackBufferFormat())) == _dx12RtvFormat) {
            return;
        }

        if (!renderBackend->WaitForGpu()) {
            return;
        }
        ImGui_ImplDX12_Shutdown();
        InitDX12Backend();
    }
#endif

    void Wrapper::ScaleToBackBuffer() {
        // The platform backend measures the window and reports it as DisplaySize, but everything we
        // draw lands in the game's back buffer. The two disagree whenever the game renders at a
        // resolution the window does not carry - driver downsampling (DSR/DLDSR), a DPI-virtualized
        // window - and the renderer backend then draws the whole overlay 1:1 into a corner of a
        // larger target. FramebufferScale is what ImGui reserves for exactly that: it stretches the
        // viewport, the scissor rects and the font rasterizer over the real target while layout
        // stays in window coordinates, which is also the space mouse messages arrive in.
        ImGuiIO &io                = ImGui::GetIO();
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

        int backBufferWidth  = 0;
        int backBufferHeight = 0;
        if (_config.renderer == nullptr || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f || !_config.renderer->GetBackBufferSize(backBufferWidth, backBufferHeight)) {
            return;
        }

        io.DisplayFramebufferScale = ImVec2(static_cast<float>(backBufferWidth) / io.DisplaySize.x, static_cast<float>(backBufferHeight) / io.DisplaySize.y);

        if (backBufferWidth != _scaledBackBufferWidth || backBufferHeight != _scaledBackBufferHeight) {
            _scaledBackBufferWidth  = backBufferWidth;
            _scaledBackBufferHeight = backBufferHeight;
            Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->debug("Overlay scaled for a {}x{} back buffer behind a {}x{} window", backBufferWidth, backBufferHeight, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
        }
    }

    void Wrapper::SetHDRUIBrightnessScale(float scale) {
        if (std::isfinite(scale) && scale >= 0.0f) {
            _hdrUIBrightnessScale.store(scale, std::memory_order_relaxed);
        }
    }

    Utils::Result<void, Framework::Error> Wrapper::Render() {
        std::scoped_lock _lock(_renderMtx);

        if (!isContextInitialized) {
            return Framework::Error {"ImGui context is not initialized"};
        }

        const auto drawData = ImGui::GetDrawData();
        if (!drawData)
            return {};

        switch (_config.renderBackend) {
        case Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_RenderDrawData(drawData);
        } break;
        case Graphics::RendererBackend::BACKEND_D3D_11: {
            // The one call every D3D11 project makes inside Present, where the swapchain is alive.
            _config.renderer->GetD3D11Backend()->RefreshBackBufferSize();
            ImGui_ImplDX11_RenderDrawData(drawData);
        } break;
#ifdef FW_IMGUI_DX12
        case Graphics::RendererBackend::BACKEND_D3D_12: {
            const auto renderBackend = _config.renderer->GetD3D12Backend();
            auto *backBuffer         = renderBackend->GetCurrentBackBuffer();
            if (!backBuffer) {
                return {}; // Begin() did not acquire a frame.
            }
            const auto description = backBuffer->GetDesc();
            auto *commands         = renderBackend->GetGraphicsCommandList();
            if (static_cast<int>(Graphics::D3D12UICompositor::RenderFormat(description.Format)) != _dx12RtvFormat) {
                // SyncDX12RtvFormat may defer a rebuild if its GPU drain fails.
                return Framework::Error("DX12 UI render-target format change is pending");
            }
            const bool linearOutput = description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (linearOutput) {
                const auto width = static_cast<UINT>(description.Width);
                if (!_dx12Compositor.Matches(width, description.Height)) {
                    if (!renderBackend->WaitForGpu()) {
                        return Framework::Error("GPU drain failed before resizing the UI compositor");
                    }
                    const HRESULT result = _dx12Compositor.Resize(renderBackend->GetDevice(), width, description.Height);
                    if (FAILED(result)) {
                        Logging::GetLogger("ImGui")->error("DX12 UI compositor allocation failed: {:#x}", static_cast<unsigned>(result));
                        return Framework::Error("Could not create the DX12 UI compositor");
                    }
                }
                _dx12Compositor.Begin(commands);
            }
            ImGui_ImplDX12_RenderDrawData(drawData, commands);
            if (linearOutput) {
                _dx12Compositor.Composite(commands, renderBackend->GetCurrentRenderTarget(), _hdrUIBrightnessScale.load(std::memory_order_relaxed));
                auto *heap = renderBackend->GetSRVHeap();
                commands->SetDescriptorHeaps(1, &heap);
            }
        } break;
#endif
        }

        return {};
    }

    void Wrapper::OnDeviceLost() {
        std::scoped_lock _lock(_renderMtx);
        if (!isContextInitialized) {
            return;
        }
        switch (_config.renderBackend) {
        case Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_InvalidateDeviceObjects();
        } break;
        default: break;
        }
    }

    void Wrapper::OnDeviceReset() {
        std::scoped_lock _lock(_renderMtx);
        if (!isContextInitialized) {
            return;
        }
        switch (_config.renderBackend) {
        case Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_CreateDeviceObjects();
        } break;
        default: break;
        }
    }

    InputState Wrapper::ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const {
        if (_config.windowBackend != Graphics::PlatformBackend::PLATFORM_WIN32) {
            return InputState::ERROR_MISMATCH;
        }

        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
            return InputState::BLOCK;
        }
        return InputState::PASS;
    }

    void Wrapper::ShowCursor(bool show) {
        ImGuiIO &io        = ImGui::GetIO();
        io.MouseDrawCursor = show;
    }

    bool Wrapper::WantsTextInput() {
        return isContextInitialized && ImGui::GetIO().WantTextInput;
    }

    static size_t FaceBytes(const FontFamilySource &faces) {
        return faces.regular.size() + faces.bold.size() + faces.italic.size() + faces.boldItalic.size();
    }

    void Wrapper::QueueFontFamily(const std::string &family, FontFamilySource faces) {
        std::scoped_lock lock(_familyMtx);
        // Registering again before the next frame replaces the queued faces, and the queue holds no
        // more than the runtime budget, so a script that registers in a loop cannot grow it.
        std::erase_if(_pendingFamilies, [&](const PendingFamily &entry) {
            return entry.family == family;
        });
        size_t queued = 0;
        for (const PendingFamily &entry : _pendingFamilies) {
            queued += FaceBytes(entry.faces);
        }
        if (FaceBytes(faces) > kMaxRuntimeFontBytes - queued) {
            Logging::GetLogger("ImGui")->warn("Font '{}' not loaded: more than {} MiB of fonts are waiting for the next frame", family, kMaxRuntimeFontBytes >> 20);
            return;
        }
        _pendingFamilies.push_back({family, std::move(faces)});
    }

    void Wrapper::ClearFontFamilies() {
        std::scoped_lock lock(_familyMtx);
        _pendingFamilies.clear();
        _clearFamilies = true;
    }

    FontFamily Wrapper::GetFontFamily(const std::string &family) const {
        if (family.empty()) {
            return {};
        }
        {
            std::scoped_lock lock(_familyMtx);
            if (const auto it = _families.find(family); it != _families.end()) {
                return it->second;
            }
        }
        return {GetFont(family)};
    }

    void Wrapper::LoadPendingFamilies() {
        std::vector<PendingFamily> pending;
        bool clear = false;
        {
            std::scoped_lock lock(_familyMtx);
            pending.swap(_pendingFamilies);
            clear = std::exchange(_clearFamilies, false);
        }
        if (clear) {
            std::scoped_lock lock(_familyMtx);
            _families.clear();
        }
        if (!isContextInitialized) {
            return;
        }
        // Parsing runs outside the lock: a render-thread GetFontFamily must not wait on it.
        for (const PendingFamily &entry : pending) {
            const FontFamily loaded {LoadRuntimeFace(entry.family, "regular", entry.faces.regular), LoadRuntimeFace(entry.family, "bold", entry.faces.bold), LoadRuntimeFace(entry.family, "italic", entry.faces.italic),
                LoadRuntimeFace(entry.family, "boldItalic", entry.faces.boldItalic)};
            std::scoped_lock lock(_familyMtx);
            _families[entry.family] = loaded;
        }
    }

    ImFont *Wrapper::LoadRuntimeFace(const std::string &family, const char *face, const std::string &bytes) {
        // ImGui asserts on anything this small; no real font is.
        if (bytes.size() <= 100) {
            return nullptr;
        }
        const std::string key = std::to_string(bytes.size()) + ":" + std::to_string(std::hash<std::string> {}(bytes));
        if (const auto it = _runtimeFaces.find(key); it != _runtimeFaces.end()) {
            return it->second;
        }
        if (_runtimeFaces.size() >= kMaxRuntimeFaces || bytes.size() > kMaxRuntimeFontBytes - _runtimeFontBytes) {
            Logging::GetLogger("ImGui")->warn("Font '{}' {} face not loaded: runtime fonts are capped at {} faces and {} MiB until the game restarts", family, face, kMaxRuntimeFaces, kMaxRuntimeFontBytes >> 20);
            return nullptr;
        }

        // The default font keeps the first slot (see Init), and is what a face falls back to.
        ImGuiIO &io = ImGui::GetIO();
        if (io.Fonts->Fonts.empty()) {
            io.Fonts->AddFontDefault();
        }
        const ImFont *fallback = io.FontDefault ? io.FontDefault : io.Fonts->Fonts[0];

        // The atlas owns the copy, and frees it itself when the face fails to load.
        void *data = IM_ALLOC(bytes.size());
        std::memcpy(data, bytes.data(), bytes.size());
        ImFontConfig cfg;
        cfg.Flags |= ImFontFlags_NoLoadError;
        std::snprintf(cfg.Name, sizeof(cfg.Name), "%s %s", family.c_str(), face);
        // ImGui asserts on a file that does not parse; a server's file is only logged.
        const bool assertOnError           = std::exchange(io.ConfigErrorRecoveryEnableAssert, false);
        ImFont *font                       = io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(bytes.size()), 0.0f, &cfg);
        io.ConfigErrorRecoveryEnableAssert = assertOnError;
        if (!font) {
            Logging::GetLogger("ImGui")->warn("Font '{}' {} face failed to load; it draws with the default font", family, face);
        }
        else {
            MergeFontFallback(*io.Fonts, font, fallback);
            _runtimeFontBytes += bytes.size();
        }
        // A failure too, so registering the same broken file again does not parse it again.
        _runtimeFaces[key] = font;
        return font;
    }

} // namespace Framework::External::ImGUI
