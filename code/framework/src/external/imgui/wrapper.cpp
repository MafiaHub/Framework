/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include "graphics/renderer.h"

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include <optick.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Framework::External::ImGUI {
    Error Wrapper::Init(Config &config) {
        _config = config;

        if (!_config.renderer) {
            return Error::IMGUI_RENDERER_NOT_SET;
        }

        if (!_config.windowHandle && _config.windowBackend == Framework::Graphics::PlatformBackend::PLATFORM_WIN32) {
            return Error::IMGUI_WINDOW_NOT_SET;
        }

        if (!_config.sdlWindow && _config.windowBackend == Framework::Graphics::PlatformBackend::PLATFORM_SDL2) {
            return Error::IMGUI_WINDOW_NOT_SET;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        (void)ImGui::GetIO();
        ImGui::StyleColorsDark();

        switch (_config.renderBackend) {
        case Framework::Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_Init(_config.renderer->GetD3D9Backend()->GetDevice());
        } break;
        case Framework::Graphics::RendererBackend::BACKEND_D3D_11: {
            const auto renderBackend = _config.renderer->GetD3D11Backend();
            ImGui_ImplDX11_Init(renderBackend->GetDevice(), renderBackend->GetContext());
        } break;
        }

        switch (_config.windowBackend) {
        case Framework::Graphics::PlatformBackend::PLATFORM_WIN32: {
            ImGui_ImplWin32_Init(_config.windowHandle);
        } break;
        case Framework::Graphics::PlatformBackend::PLATFORM_SDL2: {
            ImGui_ImplSDL2_InitForD3D(_config.sdlWindow);
        } break;
        }

        _initialized = true;
        return Error::IMGUI_NONE;
    }

    Error Wrapper::Shutdown() {
        switch (_config.renderBackend) {
        case Framework::Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_Shutdown();
        } break;
        case Framework::Graphics::RendererBackend::BACKEND_D3D_11: {
            ImGui_ImplDX11_Shutdown();
        } break;
        }

        switch (_config.windowBackend) {
        case Framework::Graphics::PlatformBackend::PLATFORM_WIN32: {
            ImGui_ImplWin32_Shutdown();
        } break;
        case Framework::Graphics::PlatformBackend::PLATFORM_SDL2: {
            ImGui_ImplSDL2_Shutdown();
        } break;
        }

        ImGui::DestroyContext();

        _initialized = false;
        return Error::IMGUI_NONE;
    }

    Error Wrapper::Update() {
        std::lock_guard _lock(_renderMtx);
        OPTICK_EVENT();

        switch (_config.renderBackend) {
        case Framework::Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_NewFrame();
        } break;
        case Framework::Graphics::RendererBackend::BACKEND_D3D_11: {
            ImGui_ImplDX11_NewFrame();
        } break;
        }

        switch (_config.windowBackend) {
        case Framework::Graphics::PlatformBackend::PLATFORM_WIN32: {
            ImGui_ImplWin32_NewFrame();
        } break;
        case Framework::Graphics::PlatformBackend::PLATFORM_SDL2: {
            ImGui_ImplSDL2_NewFrame();
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

        const auto drawData = ImGui::GetDrawData();
        if (!drawData)
            return Error::IMGUI_NONE;

        switch (_config.renderBackend) {
        case Framework::Graphics::RendererBackend::BACKEND_D3D_9: {
            ImGui_ImplDX9_RenderDrawData(drawData);
        } break;
        case Framework::Graphics::RendererBackend::BACKEND_D3D_11: {
            ImGui_ImplDX11_RenderDrawData(drawData);
        } break;
        }

        return Error::IMGUI_NONE;
    }

    InputState Wrapper::ProcessEvent(const SDL_Event *event) {
        if (_config.windowBackend != Framework::Graphics::PlatformBackend::PLATFORM_SDL2) {
            return InputState::ERROR_MISMATCH;
        }

        if (ImGui_ImplSDL2_ProcessEvent(event)) {
            return InputState::BLOCK;
        } else {
            return InputState::PASS;
        }
    }

    InputState Wrapper::ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (_config.windowBackend != Framework::Graphics::PlatformBackend::PLATFORM_WIN32) {
            return InputState::ERROR_MISMATCH;
        }

        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
            return InputState::BLOCK;
        } else {
            return InputState::PASS;
        }
    }

    void Wrapper::ShowCursor(bool show) {
        ImGuiIO &io = ImGui::GetIO();
        io.MouseDrawCursor = show;
    }

} // namespace Framework::External::ImGUI
