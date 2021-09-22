#pragma once

#include "errors.h"

namespace Framework::GUI {
    enum class RendererBackend { BACKEND_D3D_9, BACKEND_D3D_11, BACKEND_D3D_12 };

    enum class RendererState { STATE_NOT_INITIALIZED, STATE_READY, STATE_DEVICE_LOST, STATE_DEVICE_NOT_RESET, STATE_DRIVER_ERROR };

    class Renderer {
      private:
        RendererBackend _backend;
        RendererState _state;

      public:
        RendererError Init(RendererBackend);
        RendererError Shutdown();

        void HandleDeviceLost();
        void HandleDeviceReset();

        RendererBackend GetBackend() const {
            return _backend;
        }

        RendererState GetState() const {
            return _state;
        }
    }
} // namespace Framework::GUI