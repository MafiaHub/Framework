#include "d3d11.h"

namespace Framework::GUI {
    bool D3D11Backend::Init(ID3D11Device* device) {
        _device = device;

        return true;
    }

    bool D3D11Backend::Shutdown() {
        return true;
    }
}
