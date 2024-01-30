/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Graphics {
    template <typename TDevice, typename TContext, typename TSwapChain, typename TCommandQueue> class Backend {
      protected:
        TDevice _device;
        TContext _context;

      public:
        Backend() = default;

        virtual bool Init(TDevice device, TContext context, TSwapChain swapChain, TCommandQueue commandList) = 0;
        virtual bool Shutdown() = 0;

        virtual void Update() = 0;

        bool IsInitialized() const {
            return _device != nullptr;
        }

        TDevice GetDevice() const {
            return _device;
        }

        void SetDevice(TDevice device) {
            _device = device;
        }

        TContext GetContext() const {
            return _context;
        }

        void SetContext(TContext ctx) {
            _context = ctx;
        }
    };
} // namespace Framework::Graphics
