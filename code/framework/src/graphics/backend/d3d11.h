#pragma once

#include "backend.h"

#ifdef WIN32
#include <d3d11.h>
#else 
#define ID3D11Device void
#define ID3D11DeviceContext void
#endif

namespace Framework::Graphics {
    class D3D11Backend: public Backend<ID3D11Device *, ID3D11DeviceContext*> {
      public:
        virtual bool Init(ID3D11Device *, ID3D11DeviceContext *) override;
        virtual bool Shutdown() override;

        virtual void Update() override;
    };
} // namespace Framework::GUI
