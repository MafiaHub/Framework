#pragma once

#include "backend.h"

#ifdef WIN32
#include <d3d9.h>
#else
#define IDirect3D9 void
#endif

namespace Framework::GUI {
    class D3D9Backend: public Backend<IDirect3D9 *, IDirect3D9 *> {
      public:
        virtual bool Init(IDirect3D9 *, IDirect3D9*) override;
        virtual bool Shutdown() override;

        virtual void Update() override;
    };
} // namespace Framework::GUI
