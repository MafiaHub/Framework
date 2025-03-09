#include "renderer_d3d11.h"

#include <Ultralight/platform/Platform.h>

namespace Framework::GUI {
    bool RendererD3D11::Init(Graphics::Renderer *renderer) {
        if (renderer->GetBackendType() != Graphics::RendererBackend::BACKEND_D3D_11) {
            return false;
        }

        _gpuContext = std::make_unique<ultralight::GPUContextD3D11>(renderer->GetD3D11Backend()->GetDevice(), renderer->GetD3D11Backend()->GetContext(), renderer->GetD3D11Backend()->GetSwapChain());
        _gpuDriver  = std::make_unique<ultralight::GPUDriverD3D11>(_gpuContext.get());

        ultralight::Platform::instance().set_gpu_driver(_gpuDriver.get());
        return true;
    }
} // namespace Framework::GUI
