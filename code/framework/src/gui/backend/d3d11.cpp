#include "d3d11.h"

namespace Framework::GUI {
    bool D3D11Backend::Init(ID3D11Device* device, ID3D11DeviceContext* context) {
        _device = device;
        _context = context;

        return true;
    }

    bool D3D11Backend::Shutdown() {
        return true;
    }
}
