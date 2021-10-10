#pragma once

namespace Framework::GUI {
    enum class RendererBackend { BACKEND_D3D_9, BACKEND_D3D_11, BACKEND_D3D_12 };

    enum class RendererAPI { CEF, IMGUI, ULTRALIGHT };

    enum class RendererState { STATE_NOT_INITIALIZED, STATE_READY, STATE_DEVICE_LOST, STATE_DEVICE_NOT_RESET, STATE_DRIVER_ERROR };
}
