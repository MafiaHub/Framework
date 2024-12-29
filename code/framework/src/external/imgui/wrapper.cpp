/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include "graphics/renderer.h"

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Framework::External::ImGUI {
    Error Wrapper::Init(Config &config) {
        if (isContextInitialized) {
            return Error::IMGUI_NONE;
        }

        _config = config;

        if (!_config.renderer) {
            return Error::IMGUI_RENDERER_NOT_SET;
        }

        if (!_config.windowHandle && _config.windowBackend == Graphics::PlatformBackend::PLATFORM_WIN32) {
            return Error::IMGUI_WINDOW_NOT_SET;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();

        io.ConfigWindowsResizeFromEdges = true;

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
            ImGui_ImplDX12_Init(renderBackend->GetDevice(), renderBackend->NumFramesInFlight(), DXGI_FORMAT_R8G8B8A8_UNORM, renderBackend->GetSRVHeap(), renderBackend->GetSRVHeap()->GetCPUDescriptorHandleForHeapStart(),
                renderBackend->GetSRVHeap()->GetGPUDescriptorHandleForHeapStart());
        } break;
        }

        switch (_config.windowBackend) {
        case Graphics::PlatformBackend::PLATFORM_WIN32: {
            ImGui_ImplWin32_Init(_config.windowHandle);
        } break;
        }

        _initialized = isContextInitialized = true;
        return Error::IMGUI_NONE;
    }

    Error Wrapper::Shutdown() {
        if (!isContextInitialized) {
            return Error::IMGUI_NONE;
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

        _initialized = isContextInitialized = false;
        return Error::IMGUI_NONE;
    }

    Error Wrapper::Update() {
        std::lock_guard _lock(_renderMtx);

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

        return Error::IMGUI_NONE;
    }

    Error Wrapper::Render() {
        std::lock_guard _lock(_renderMtx);

        if (!isContextInitialized) {
            return Error::IMGUI_NOT_INITIALIZED;
        }

        const auto drawData = ImGui::GetDrawData();
        if (!drawData)
            return Error::IMGUI_NONE;

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

        return Error::IMGUI_NONE;
    }

    InputState Wrapper::ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const {
        if (_config.windowBackend != Graphics::PlatformBackend::PLATFORM_WIN32) {
            return InputState::ERROR_MISMATCH;
        }

        if (!_processEventEnabled) {
            return InputState::PASS;
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
