/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "swap_chain_upgrade.h"

#include "logging/logger.h"

#include <dxgi.h>
#include <minhook.h>

#include <algorithm>

namespace Framework::Launcher {
    namespace {
        using CreateFactoryProc    = HRESULT(WINAPI *)(REFIID, void **);
        using CreateFactory2Proc   = HRESULT(WINAPI *)(UINT, REFIID, void **);
        using CreateSwapChainProc  = HRESULT(STDMETHODCALLTYPE *)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);

        // IDXGIFactory::CreateSwapChain, after IUnknown (3), IDXGIObject (4) and the factory's first three
        constexpr size_t kCreateSwapChainSlot = 10;

        CreateFactoryProc gCreateFactory          = nullptr;
        CreateFactoryProc gCreateFactory1         = nullptr;
        CreateFactory2Proc gCreateFactory2        = nullptr;
        CreateSwapChainProc gOrigCreateSwapChain  = nullptr;

        HRESULT STDMETHODCALLTYPE CreateSwapChainStub(IDXGIFactory *factory, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapChain) {
            if (!desc || !desc->Windowed || desc->SampleDesc.Count != 1 || (desc->SwapEffect != DXGI_SWAP_EFFECT_DISCARD && desc->SwapEffect != DXGI_SWAP_EFFECT_SEQUENTIAL)) {
                return gOrigCreateSwapChain(factory, device, desc, swapChain);
            }

            // The flip model wants at least two buffers and no multisampling, as Windows' own upgrade does
            DXGI_SWAP_CHAIN_DESC flipDesc = *desc;
            flipDesc.SwapEffect           = desc->SwapEffect == DXGI_SWAP_EFFECT_DISCARD ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            flipDesc.BufferCount          = std::max<UINT>(desc->BufferCount, 2);

            const HRESULT result = gOrigCreateSwapChain(factory, device, &flipDesc, swapChain);
            if (SUCCEEDED(result)) {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->info("Game swap chain created with the flip model (swap effect {} -> {})", static_cast<int>(desc->SwapEffect), static_cast<int>(flipDesc.SwapEffect));
                return result;
            }

            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->warn("Flip-model swap chain refused ({:#x}), keeping the game's own description", static_cast<uint32_t>(result));
            return gOrigCreateSwapChain(factory, device, desc, swapChain);
        }

        void HookFactory(HRESULT result, void *factory) {
            if (FAILED(result) || !factory || gOrigCreateSwapChain) {
                return;
            }

            // Every factory of a DXGI version shares its vtable, so hooking the first covers the rest
            void *target = (*static_cast<void ***>(factory))[kCreateSwapChainSlot];
            MH_Initialize();
            if (MH_CreateHook(target, reinterpret_cast<void *>(&CreateSwapChainStub), reinterpret_cast<void **>(&gOrigCreateSwapChain)) == MH_OK) {
                MH_EnableHook(target);
            }
        }

        HRESULT WINAPI CreateDXGIFactoryStub(REFIID riid, void **factory) {
            const HRESULT result = gCreateFactory(riid, factory);
            HookFactory(result, factory ? *factory : nullptr);
            return result;
        }

        HRESULT WINAPI CreateDXGIFactory1Stub(REFIID riid, void **factory) {
            const HRESULT result = gCreateFactory1(riid, factory);
            HookFactory(result, factory ? *factory : nullptr);
            return result;
        }

        HRESULT WINAPI CreateDXGIFactory2Stub(UINT flags, REFIID riid, void **factory) {
            const HRESULT result = gCreateFactory2(flags, riid, factory);
            HookFactory(result, factory ? *factory : nullptr);
            return result;
        }
    } // namespace

    void *ResolveFlipModelFactoryExport(HMODULE dxgi, const std::string &exportName) {
        if (exportName == "CreateDXGIFactory") {
            gCreateFactory = reinterpret_cast<CreateFactoryProc>(GetProcAddress(dxgi, "CreateDXGIFactory"));
            return gCreateFactory ? reinterpret_cast<void *>(&CreateDXGIFactoryStub) : nullptr;
        }
        if (exportName == "CreateDXGIFactory1") {
            gCreateFactory1 = reinterpret_cast<CreateFactoryProc>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
            return gCreateFactory1 ? reinterpret_cast<void *>(&CreateDXGIFactory1Stub) : nullptr;
        }
        if (exportName == "CreateDXGIFactory2") {
            gCreateFactory2 = reinterpret_cast<CreateFactory2Proc>(GetProcAddress(dxgi, "CreateDXGIFactory2"));
            return gCreateFactory2 ? reinterpret_cast<void *>(&CreateDXGIFactory2Stub) : nullptr;
        }
        return nullptr;
    }
} // namespace Framework::Launcher
