/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

namespace Framework::External::ImGUI {
    Error Wrapper::Init() {
        if (!_config.renderer) {
            return Error::IMGUI_RENDERER_NOT_SET;
        }

        if (!_config.windowHandle && _config.windowBackend == WindowBackend::WIN_32) {
            return Error::IMGUI_WINDOW_NOT_SET;
        }

        if (!_config.sdlWindow && _config.windowBackend == WindowBackend::SDL2) {
            return Error::IMGUI_WINDOW_NOT_SET;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        (void)ImGui::GetIO();
        ImGui::StyleColorsDark();

        switch (_config.renderBackend) {
        case RenderBackend::D3D9: {
            ImGui_ImplDX9_Init(_config.renderer->GetD3D9Backend()->GetDevice());
        } break;
        case RenderBackend::D3D11: {
            const auto renderBackend = _config.renderer->GetD3D11Backend();
            ImGui_ImplDX11_Init(renderBackend->GetDevice(), renderBackend->GetContext());
        } break;
        }

        switch (_config.windowBackend) {
        case WindowBackend::WIN_32: {
            ImGui_ImplWin32_Init(_config.windowHandle);
        } break;
        case WindowBackend::SDL2: {
            ImGui_ImplSDL2_InitForD3D(_config.sdlWindow);
        } break;
        }

        return Error::IMGUI_NONE;
    }

    Error Wrapper::Shutdown() {
        switch (_config.renderBackend) {
        case RenderBackend::D3D9: {
            ImGui_ImplDX9_Shutdown();
        } break;
        case RenderBackend::D3D11: {
            ImGui_ImplDX11_Shutdown();
        } break;
        }

        switch (_config.windowBackend) {
        case WindowBackend::WIN_32: {
            ImGui_ImplWin32_Shutdown();
        } break;
        case WindowBackend::SDL2: {
            ImGui_ImplSDL2_Shutdown();
        } break;
        }

        ImGui::DestroyContext();

        return Error::IMGUI_NONE;
    }

    Error Wrapper::NewFrame() {
        switch (_config.renderBackend) {
        case RenderBackend::D3D9: {
            ImGui_ImplDX9_NewFrame();
        } break;
        case RenderBackend::D3D11: {
            ImGui_ImplDX11_NewFrame();
        } break;
        }

        switch (_config.windowBackend) {
        case WindowBackend::WIN_32: {
            ImGui_ImplWin32_NewFrame();
        } break;
        case WindowBackend::SDL2: {
            ImGui_ImplSDL2_NewFrame();
        } break;
        }

        ImGui::NewFrame();

        return Error::IMGUI_NONE;
    }

    Error Wrapper::EndFrame() {
        ImGui::Render();

        return Error::IMGUI_NONE;
    }

    Error Wrapper::Render() {
        switch (_config.renderBackend) {
        case RenderBackend::D3D9: {
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        } break;
        case RenderBackend::D3D11: {
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        } break;
        }

        return Error::IMGUI_NONE;
    }

    Error Wrapper::ProcessEvent(const SDL_Event *event) {
        if (_config.windowBackend != WindowBackend::SDL2) {
            return Error::IMGUI_BACKEND_MISMATCH;
        }
    }

    Error Wrapper::ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (_config.windowBackend != WindowBackend::WIN_32) {
            return Error::IMGUI_BACKEND_MISMATCH;
        }
    }

} // namespace Framework::External::ImGUI
