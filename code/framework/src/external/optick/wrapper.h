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
