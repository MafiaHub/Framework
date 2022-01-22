/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <Windows.h>

namespace Framework::Launcher {
    class MiniDump {
      private:
        static inline bool isCaptureEnabled = true;
        void InitExceptionOverride();

        static LONG WINAPI MiniDumpExceptionHandler(EXCEPTION_POINTERS *exceptionInfo);

      public:
        MiniDump();

        inline void SetCaptureEnabled(bool enabled) {
            isCaptureEnabled = enabled;
        }
    };
}; // namespace Framework::Launcher
