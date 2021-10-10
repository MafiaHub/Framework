#pragma once

#include "backend.h"

#include <d3d11.h>

namespace Framework::GUI {
    class D3D11Backend: public Backend<ID3D11Device *, ID3D11DeviceContext*> {
      public:
        virtual bool Init(ID3D11Device *, ID3D11DeviceContext *) override;
        virtual bool Shutdown() override;

        virtual void Update() override;
    };
} // namespace Framework::GUI
