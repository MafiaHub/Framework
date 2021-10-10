#pragma once

#include "backend.h"

#include <d3d11.h>

namespace Framework::GUI {
    class D3D11Backend: public Backend<ID3D11Device *> {
      public:
        virtual bool Init(ID3D11Device *) override;
        virtual bool Shutdown() override;
    };
} // namespace Framework::GUI
