/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/lifecycle.h>

#include "utils/safe_win32.h"

#include <utils/error.h>
#include <utils/result.h>

#include "font_family.h"
#include "graphics/backend/d3d12_ui_compositor.h"
#include "graphics/types.h"
#include "widgets/world_text_layout.h"

#include <atomic>
#include <deque>
#include <function2/function2.hpp>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

struct ImFont;

namespace Framework::Graphics {
    class Renderer;
} // namespace Framework::Graphics

namespace Framework::External::ImGUI {
    enum class InputState {
        BLOCK,
        PASS,
        ERROR_MISMATCH
    };

    // A TTF a mod draws with by name, loaded into the atlas beside the UI font. ImGui 1.92
    // rasterizes at whatever size a draw call asks for, so a font carries no size.
    struct FontSource {
        std::string name;
        std::string path;
    };

    struct Config {
        Graphics::PlatformBackend windowBackend = Graphics::PlatformBackend::PLATFORM_WIN32;
        Graphics::RendererBackend renderBackend = Graphics::RendererBackend::BACKEND_D3D_11;

        // Optional UI font. When fontPath is set, the TTF is loaded as the default
        // font; otherwise ImGui's embedded ASCII-only font is used. With ImGui 1.92's
        // dynamic atlas, glyphs are rasterized on demand, so a Unicode-covering font
        // (e.g. Latin + Cyrillic + CJK) "just works" without explicit glyph ranges.
        std::string fontPath;
        float fontSize = 16.0f;

        // Named fonts for mod widgets (nametags, HUD text); never the default font.
        std::vector<FontSource> fonts;

        // NOTE: Set up during init
        Graphics::Renderer *renderer = nullptr;
        HWND windowHandle            = nullptr;
    };

    class Wrapper final: public Framework::Lifecycle {
      public:
        using RenderProc = fu2::function<void() const>;

      private:
        Config _config;
        std::queue<RenderProc> _renderQueue;
        std::recursive_mutex _renderMtx;

        std::unordered_map<std::string, ImFont *> _fonts;

        // A session's font families: queued from any thread, loaded on Update's thread, removed from the atlas when the session ends.
        struct PendingFamily {
            std::string family;
            FontFamilySource faces;
        };
        struct RuntimeFace {
            std::string bytes;      // the atlas reads the face from here
            ImFont *font = nullptr; // null = failed to load
        };
        static constexpr size_t kMaxRuntimeFaces     = 64;
        static constexpr size_t kMaxRuntimeFontBytes = 64u << 20;
        std::mutex _pendingMtx;
        std::vector<PendingFamily> _pendingFamilies;
        bool _clearFamilies = false;
        std::unordered_map<std::string, Widgets::WorldTextFonts> _families;
        std::deque<RuntimeFace> _runtimeFaces; // a failed face counts against the budget too
        size_t _runtimeFontBytes = 0;
        uint32_t _fontGeneration = 0;

        void LoadPendingFamilies();
        void DropRuntimeFaces();
        ImFont *LoadRuntimeFace(const std::string &family, const char *face, std::string bytes);

        static inline std::atomic_bool isContextInitialized = false;

        bool _processEventEnabled = true;

        // Back buffer the overlay was last scaled to; only the log line reads it.
        int _scaledBackBufferWidth  = 0;
        int _scaledBackBufferHeight = 0;

        // Matches the ImGui coordinate space to the back buffer we actually draw into.
        void ScaleToBackBuffer();

        int _dx12RtvFormat = 0;
        Graphics::D3D12UICompositor _dx12Compositor;
        std::atomic<float> _hdrUIBrightnessScale {1.0f};
        void InitDX12Backend();
        void SyncDX12RtvFormat();

      public:
        [[nodiscard]] Utils::Result<void, Framework::Error> Init(Config &config);
        // If the DX12 GPU drain fails, stays initialized; retain the renderer and retry.
        void Shutdown() override;

        InputState ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;
        static void ShowCursor(bool show);
        // An ImGui text field has the keyboard, as of the last Update; read it on Update's thread.
        static bool WantsTextInput();

        void Update() override;
        Utils::Result<void, Framework::Error> Render();

        // Scale linear RGB for the DX12 scRGB overlay (ImGui and CEF), leaving
        // alpha and SDR rendering unchanged. 1.0 maps UI white to 80 nits;
        // use desiredWhiteNits / 80.0f. May be updated from another thread.
        // Negative and non-finite values are ignored; zero produces black UI.
        void SetHDRUIBrightnessScale(float scale);

        // Release/recreate backend device objects around a graphics device reset
        // (e.g. D3D9 lost device on alt-tab). Must bracket the host's device Reset.
        void OnDeviceLost();
        void OnDeviceReset();

        // Null when the font was never declared or its file failed to load; the caller then
        // draws with the current font.
        ImFont *GetFont(const std::string &name) const {
            const auto it = _fonts.find(name);
            return it != _fonts.end() ? it->second : nullptr;
        }

        // Loads a family's faces before the next frame, replacing what it had. Any thread. A face that fails to load, or would pass the session's budget, stays null and is logged.
        void QueueFontFamily(const std::string &family, FontFamilySource faces);
        // Ends the session's families before the next frame, removing their faces from the atlas. Any thread.
        void ClearFontFamilies();
        // A loaded family, else a declared font as the regular face. Update's thread.
        Widgets::WorldTextFonts GetFontFamily(const std::string &family) const;
        // Changes whenever a family loads or the families clear. A layout built under another generation may point at a removed face: rebuild it.
        uint32_t GetFontGeneration() const {
            return _fontGeneration;
        }

        void PushWidget(const RenderProc &proc) {
            _renderQueue.push(proc);
        }
    };
} // namespace Framework::External::ImGUI
