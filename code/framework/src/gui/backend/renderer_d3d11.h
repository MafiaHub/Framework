#pragma once

#include "d3d11/context.h"
#include "d3d11/driver.h"

#include "graphics/renderer.h"

namespace Framework::GUI {
    class RendererD3D11 {
      private:
        std::unique_ptr<ultralight::GPUDriverD3D11> _gpuDriver;
        std::unique_ptr<ultralight::GPUContextD3D11> _gpuContext;

        public:
        bool Init(Graphics::Renderer *);
    };
}
