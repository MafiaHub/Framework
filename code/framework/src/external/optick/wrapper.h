/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <optick.h>

namespace Framework::External::Optick {
    class Wrapper final {
      public:
        /**
         * Automated profiling
         */
        static void StartCapture();

        static void StopCapture();

        static void Dump(const char *path);

        /**
         * Advanced controls
         */

        static void Update();

        static void Shutdown();

        static void SetAllocator(::Optick::AllocateFn allocateFn, ::Optick::DeallocateFn deallocateFn, ::Optick::InitThreadCb initThreadCb);

        static void SetStateChangedCallback(::Optick::StateCallback stateCallback);
    };
} // namespace Framework::External::Optick
