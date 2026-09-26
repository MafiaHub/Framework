/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <cstddef>
#include <cstdint>

// The Windows SDK stopped shipping d3d8.h, so the framework binds the handful of Direct3D 8 calls
// it needs by vtable slot. The slots and values below follow the DirectX 8.1 SDK declaration order
// (IUnknown first); a Direct3D 8 game owns the device, the framework only borrows it.
namespace Framework::Graphics::D3D8 {
    namespace Detail {
        template <typename Result, typename... Args>
        Result Call(const void *self, size_t slot, Args... args) {
            using Function = Result(__stdcall *)(const void *, Args...);
            return reinterpret_cast<Function>((*reinterpret_cast<void *const *const *>(self))[slot])(self, args...);
        }
    } // namespace Detail

    inline constexpr uint32_t kFormatA8R8G8B8     = 21;
    inline constexpr uint32_t kPoolManaged        = 1;
    inline constexpr uint32_t kBackBufferTypeMono = 0;
    inline constexpr uint32_t kStateBlockAll      = 1;
    inline constexpr uint32_t kTriangleStrip      = 5;
    inline constexpr uint32_t kFvfXyzRhw          = 0x004;
    inline constexpr uint32_t kFvfDiffuse         = 0x040;
    inline constexpr uint32_t kFvfTex1            = 0x100;

    // D3DRENDERSTATETYPE
    inline constexpr uint32_t kRsZEnable                = 7;
    inline constexpr uint32_t kRsFillMode               = 8;
    inline constexpr uint32_t kRsZWriteEnable           = 14;
    inline constexpr uint32_t kRsAlphaTestEnable        = 15;
    inline constexpr uint32_t kRsSrcBlend               = 19;
    inline constexpr uint32_t kRsDestBlend              = 20;
    inline constexpr uint32_t kRsCullMode               = 22;
    inline constexpr uint32_t kRsAlphaBlendEnable       = 27;
    inline constexpr uint32_t kRsFogEnable              = 28;
    inline constexpr uint32_t kRsSpecularEnable         = 29;
    inline constexpr uint32_t kRsStencilEnable          = 52;
    inline constexpr uint32_t kRsWrap0                  = 128;
    inline constexpr uint32_t kRsClipping               = 136;
    inline constexpr uint32_t kRsLighting               = 137;
    inline constexpr uint32_t kRsVertexBlend            = 151;
    inline constexpr uint32_t kRsClipPlaneEnable        = 152;
    inline constexpr uint32_t kRsMultiSampleAntialias   = 161;
    inline constexpr uint32_t kRsIndexedVertexBlend     = 167;
    inline constexpr uint32_t kRsColorWriteEnable       = 168;
    inline constexpr uint32_t kRsBlendOp                = 171;
    inline constexpr uint32_t kFillSolid                = 3;
    inline constexpr uint32_t kBlendOne                 = 2;
    inline constexpr uint32_t kBlendInvSrcAlpha         = 6;
    inline constexpr uint32_t kBlendOpAdd               = 1;
    inline constexpr uint32_t kCullNone                 = 1;
    inline constexpr uint32_t kColorWriteAll            = 0xf;

    // D3DTEXTURESTAGESTATETYPE; Direct3D 8 keeps sampler state here too.
    inline constexpr uint32_t kTssColorOp               = 1;
    inline constexpr uint32_t kTssColorArg1             = 2;
    inline constexpr uint32_t kTssAlphaOp               = 4;
    inline constexpr uint32_t kTssAlphaArg1             = 5;
    inline constexpr uint32_t kTssTexCoordIndex         = 11;
    inline constexpr uint32_t kTssAddressU              = 13;
    inline constexpr uint32_t kTssAddressV              = 14;
    inline constexpr uint32_t kTssMagFilter             = 16;
    inline constexpr uint32_t kTssMinFilter             = 17;
    inline constexpr uint32_t kTssMipFilter             = 18;
    inline constexpr uint32_t kTssTextureTransformFlags = 24;
    inline constexpr uint32_t kTopDisable               = 1;
    inline constexpr uint32_t kTopSelectArg1            = 2;
    inline constexpr uint32_t kTaTexture                = 2;
    inline constexpr uint32_t kTextureFilterNone        = 0;
    inline constexpr uint32_t kTextureFilterPoint       = 1;
    inline constexpr uint32_t kTextureAddressClamp      = 3;

    struct LockedRect {
        INT pitch;
        void *bits;
    };

    struct SurfaceDesc {
        uint32_t format;
        uint32_t type;
        DWORD usage;
        uint32_t pool;
        UINT size;
        uint32_t multiSampleType;
        UINT width;
        UINT height;
    };
    static_assert(sizeof(SurfaceDesc) == 32);

    struct Viewport {
        DWORD x;
        DWORD y;
        DWORD width;
        DWORD height;
        float minZ;
        float maxZ;
    };
    static_assert(sizeof(Viewport) == 24);

    class Surface final {
      public:
        ULONG Release() {
            return Detail::Call<ULONG>(this, 2);
        }
        HRESULT GetDesc(SurfaceDesc *desc) {
            return Detail::Call<HRESULT>(this, 8, desc);
        }
    };

    class Texture final {
      public:
        ULONG Release() {
            return Detail::Call<ULONG>(this, 2);
        }
        HRESULT LockRect(UINT level, LockedRect *locked, const RECT *rect, DWORD flags) {
            return Detail::Call<HRESULT>(this, 16, level, locked, rect, flags);
        }
        HRESULT UnlockRect(UINT level) {
            return Detail::Call<HRESULT>(this, 17, level);
        }
    };

    class Device final {
      public:
        HRESULT TestCooperativeLevel() {
            return Detail::Call<HRESULT>(this, 3);
        }
        HRESULT GetBackBuffer(UINT index, uint32_t type, Surface **surface) {
            return Detail::Call<HRESULT>(this, 16, index, type, surface);
        }
        HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, uint32_t format, uint32_t pool, Texture **texture) {
            return Detail::Call<HRESULT>(this, 20, width, height, levels, usage, format, pool, texture);
        }
        HRESULT BeginScene() {
            return Detail::Call<HRESULT>(this, 34);
        }
        HRESULT EndScene() {
            return Detail::Call<HRESULT>(this, 35);
        }
        HRESULT SetViewport(const Viewport *viewport) {
            return Detail::Call<HRESULT>(this, 40, viewport);
        }
        HRESULT SetRenderState(uint32_t state, DWORD value) {
            return Detail::Call<HRESULT>(this, 50, state, value);
        }
        HRESULT ApplyStateBlock(DWORD token) {
            return Detail::Call<HRESULT>(this, 54, token);
        }
        HRESULT DeleteStateBlock(DWORD token) {
            return Detail::Call<HRESULT>(this, 56, token);
        }
        HRESULT CreateStateBlock(uint32_t type, DWORD *token) {
            return Detail::Call<HRESULT>(this, 57, type, token);
        }
        HRESULT SetTexture(DWORD stage, Texture *texture) {
            return Detail::Call<HRESULT>(this, 61, stage, texture);
        }
        HRESULT SetTextureStageState(DWORD stage, uint32_t type, DWORD value) {
            return Detail::Call<HRESULT>(this, 63, stage, type, value);
        }
        HRESULT DrawPrimitiveUP(uint32_t type, UINT count, const void *vertices, UINT stride) {
            return Detail::Call<HRESULT>(this, 72, type, count, vertices, stride);
        }
        // Direct3D 8 takes a fixed-function FVF through the vertex shader slot.
        HRESULT SetVertexShader(DWORD handle) {
            return Detail::Call<HRESULT>(this, 76, handle);
        }
        HRESULT SetPixelShader(DWORD handle) {
            return Detail::Call<HRESULT>(this, 88, handle);
        }
    };
} // namespace Framework::Graphics::D3D8
