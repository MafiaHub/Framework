/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <Windows.h>

#include <string>

namespace Framework::Utils {
    class MiniDump final {
      private:
        static inline std::string _symbolPath;
        static inline bool _isCaptureEnabled = true;
        void InitExceptionOverride();

      public:
        MiniDump();

        inline void SetCaptureEnabled(bool enabled) {
            _isCaptureEnabled = enabled;
        }

        inline void SetSymbolPath(const std::string &path) {
            _symbolPath = path;
        }

        static LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS *exceptionInfo);
    };
} // namespace Framework::Utils
