/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "minidump.h"
#include <winnt.h>

#include "logging/logger.h"

#include <StackWalker.h>

#include <string>

typedef void(__cdecl *CoreSetExceptionOverride)(LONG (*handler)(EXCEPTION_POINTERS *));

namespace Framework::Utils {
    class StackWalkerSentry final: public StackWalker {
      private:
        std::string outputDump;

      protected:
        virtual void OnOutput(LPCSTR dump) override {
            outputDump += dump;
        }

        // todo implement these to get extra information
        //
        // virtual void OnSymInit(LPCSTR szSearchPath, DWORD symOptions, LPCSTR szUserName);
        // virtual void OnLoadModule(LPCSTR img, LPCSTR mod, DWORD64 baseAddr, DWORD size,
        //                         DWORD result, LPCSTR symType, LPCSTR pdbName,
        //                         ULONGLONG fileVersion);
        // virtual void OnCallstackEntry(CallstackEntryType eType, CallstackEntry &entry);
        // virtual void OnDbgHelpErr(LPCSTR szFuncName, DWORD gle, DWORD64 addr);

      public:
        StackWalkerSentry() {
            StackWalker();
        }

        inline std::string GetOutputDump() const {
            return outputDump;
        }
    };

    LONG WINAPI MiniDump::MiniDumpExceptionHandler(EXCEPTION_POINTERS *exceptionInfo) {
        if (!isCaptureEnabled) {
            return EXCEPTION_EXECUTE_HANDLER;
        }
        if (exceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        StackWalkerSentry sw;
        sw.SetSymPath(symbolPath.c_str());
        sw.LoadModules();
        sw.ShowCallstack(GetCurrentThread(), exceptionInfo->ContextRecord);
        Framework::Logging::GetLogger("MiniDump")->error(sw.GetOutputDump());
        Framework::Logging::GetLogger("MiniDump")->flush();

        Sleep(1000);

        // uncomment to break here
        //__debugbreak();

        // todo send to sentry

        return EXCEPTION_EXECUTE_HANDLER;
    }

    void MiniDump::InitExceptionOverride() {
        const auto coreSetExceptionOverride = reinterpret_cast<CoreSetExceptionOverride>(GetProcAddress(GetModuleHandleA("FrameworkLoaderData.dll"), "CoreSetExceptionOverride"));
        if (coreSetExceptionOverride) {
            coreSetExceptionOverride(MiniDumpExceptionHandler);
        }

        SetUnhandledExceptionFilter(MiniDumpExceptionHandler);
    }

    MiniDump::MiniDump() {
        InitExceptionOverride();
    }
}; // namespace Framework::Utils
