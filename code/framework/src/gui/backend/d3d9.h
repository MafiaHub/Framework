#pragma once

#include "backend.h"

#include <d3d9.h>

namespace Framework::GUI {
    class D3D9Backend: public Backend<IDirect3D9 *, IDirect3D9 *> {
      public:
        virtual bool Init(IDirect3D9 *, IDirect3D9*) override;
        virtual bool Shutdown() override;

        virtual void Update() override;
    };
} // namespace Framework::GUI
