/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <Windows.h>

#include <atomic>
#include <string>

namespace Framework::Utils {
    class MiniDump final {
      private:
        static inline std::string _symbolPath;
        static inline std::wstring _dumpDirectory;
        static inline std::atomic_bool _isCaptureEnabled {true};
        void InitExceptionOverride();

      public:
        MiniDump();

        inline void SetCaptureEnabled(bool enabled) {
            _isCaptureEnabled.store(enabled);
        }

        inline void SetSymbolPath(const std::string &path) {
            _symbolPath = path;
        }

        inline void SetDumpDirectory(const std::wstring &path) {
            _dumpDirectory = path;
        }

        static LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS *exceptionInfo);
    };
} // namespace Framework::Utils
