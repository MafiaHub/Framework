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

namespace Framework::Launcher {

    class StackWalkerSentry final : public StackWalker {
        private:
        std::string outputDump;
        protected:
        virtual void OnOutput(LPCSTR dump) override {
            outputDump = dump;
        }
        public:
        StackWalkerSentry() {
            StackWalker(StackWalkOptions::RetrieveSymbol | StackWalkOptions::RetrieveLine);
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
        sw.ShowCallstack(GetCurrentThread(), exceptionInfo->ContextRecord);
        Framework::Logging::GetLogger("MiniDump")->error(sw.GetOutputDump());

        // todo send to sentry

        return EXCEPTION_EXECUTE_HANDLER;
    }

    void MiniDump::InitExceptionOverride() {
        const auto coreSetExceptionOverride = reinterpret_cast<CoreSetExceptionOverride>(GetProcAddress(GetModuleHandleA("FrameworkLoaderData.dll"), "CoreSetExceptionOverride"));
        if (coreSetExceptionOverride) {
            coreSetExceptionOverride(MiniDumpExceptionHandler);
        }
    }

    MiniDump::MiniDump() {
        InitExceptionOverride();
    }
};
