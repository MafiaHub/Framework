/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include "graphics/renderer.h"

#include <logging/logger.h>

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_win32.h>

#include "graphics/backend/d3d11.h"
#include "graphics/backend/d3d12.h"
#include "graphics/backend/d3d9.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Framework::External::ImGUI {
    namespace {
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

        ImGui::StyleColorsDark();

        switch (_config.renderBackend) {
        case Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_Init(_config.renderer->GetD3D9Backend()->GetDevice());
        } break;
        case Graphics::RendererBackend::BACKEND_D3D_11: {
            const auto renderBackend = _config.renderer->GetD3D11Backend();
            ImGui_ImplDX11_Init(renderBackend->GetDevice(), renderBackend->GetContext());
        } break;
        case Graphics::RendererBackend::BACKEND_D3D_12: {
            const auto renderBackend = _config.renderer->GetD3D12Backend();

            ImGui_ImplDX12_InitInfo initInfo {};
            initInfo.Device               = renderBackend->GetDevice();
            initInfo.CommandQueue         = renderBackend->GetCommandQueue();
            initInfo.NumFramesInFlight    = renderBackend->NumFramesInFlight();
            initInfo.RTVFormat            = DXGI_FORMAT_R8G8B8A8_UNORM;
            initInfo.SrvDescriptorHeap    = renderBackend->GetSRVHeap();
            initInfo.UserData             = renderBackend;
            initInfo.SrvDescriptorAllocFn = ImGuiAllocSRV;
            initInfo.SrvDescriptorFreeFn  = ImGuiFreeSRV;
            ImGui_ImplDX12_Init(&initInfo);
        } break;
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
        case Graphics::RendererBackend::BACKEND_D3D_12: {
            ImGui_ImplDX12_Shutdown();
        } break;
        }

        switch (_config.windowBackend) {
        case Graphics::PlatformBackend::PLATFORM_WIN32: {
            ImGui_ImplWin32_Shutdown();
        } break;
        }

        ImGui::DestroyContext();

        isContextInitialized = false;
        Lifecycle::Shutdown();
    }

    void Wrapper::Update() {
        std::scoped_lock _lock(_renderMtx);

        switch (_config.renderBackend) {
        case Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_NewFrame();
        } break;
        case Graphics::RendererBackend::BACKEND_D3D_11: {
            ImGui_ImplDX11_NewFrame();
        } break;
        case Graphics::RendererBackend::BACKEND_D3D_12: {
            ImGui_ImplDX12_NewFrame();
        } break;
        }

        switch (_config.windowBackend) {
        case Graphics::PlatformBackend::PLATFORM_WIN32: {
            ImGui_ImplWin32_NewFrame();
        } break;
        }

        ImGui::NewFrame();

        // process all widgets
        while (!_renderQueue.empty()) {
            const auto &proc = _renderQueue.front();
            proc();
            _renderQueue.pop();
        }

        ImGui::Render();
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
            ImGui_ImplDX11_RenderDrawData(drawData);
        } break;
        case Graphics::RendererBackend::BACKEND_D3D_12: {
            // TODO(DavoSK): pass second argument here
            const auto renderBackend = _config.renderer->GetD3D12Backend();
            ImGui_ImplDX12_RenderDrawData(drawData, renderBackend->GetGraphicsCommandList());
        } break;
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

} // namespace Framework::External::ImGUI
