/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <Windows.h>

#include <string>

namespace Framework::Utils {
    class MiniDump {
      private:
        static inline std::string symbolPath;
        static inline bool isCaptureEnabled = true;
        void InitExceptionOverride();

      public:
        MiniDump();

        inline void SetCaptureEnabled(bool enabled) {
            isCaptureEnabled = enabled;
        }

        inline void SetSymbolPath(const std::string &path) {
            symbolPath = path;
        }

        static LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS *exceptionInfo);
    };
}; // namespace Framework::Utils
