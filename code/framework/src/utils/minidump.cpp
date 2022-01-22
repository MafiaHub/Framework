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
#include <fmt/core.h>

#include <StackWalker.h>

#include <string>

#include <psapi.h>

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
            StackWalker(StackWalkOptions::RetrieveSymbol|StackWalkOptions::RetrieveLine|StackWalkOptions::SymUseSymSrv);
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

        isCaptureEnabled = false;
        StackWalkerSentry sw;
        sw.SetSymPath(symbolPath.c_str());
        sw.ShowCallstack(GetCurrentThread(), exceptionInfo->ContextRecord);
#ifdef _M_AMD64
        std::string crashInfo = fmt::format("Crash address: {:X} Code: {:X} \nRegisters: \n"
		"RAX: {:<8x} RCX: {:<8x} \n"
		"RDX: {:<8x} RBX: {:<8x} \n"
		"RSP: {:<8x} RBP: {:<8x} \n"
		"RSI: {:<8x} RDI: {:<8x} \n",
		(DWORD64)exceptionInfo->ExceptionRecord->ExceptionAddress,
		exceptionInfo->ExceptionRecord->ExceptionCode, exceptionInfo->ContextRecord->Rax,
		exceptionInfo->ContextRecord->Rcx, exceptionInfo->ContextRecord->Rdx, exceptionInfo->ContextRecord->Rbx,
		exceptionInfo->ContextRecord->Rsp, exceptionInfo->ContextRecord->Rbp, exceptionInfo->ContextRecord->Rsi, exceptionInfo->ContextRecord->Rdi);
#elif _M_X86
        std::string crashInfo = fmt::format("Crash address: {:X} Code: {:X} \nRegisters: \n"
		"EAX: {:<8x} ECX: {:<8x} \n"
		"EDX: {:<8x} EBX: {:<8x} \n"
		"ESP: {:<8x} EBP: {:<8x} \n"
		"ESI: {:<8x} EDI: {:<8x} \n",
		(DWORD)exceptionInfo->ExceptionRecord->ExceptionAddress,
		exceptionInfo->ExceptionRecord->ExceptionCode, exceptionInfo->ContextRecord->Eax,
		exceptionInfo->ContextRecord->Ecx, exceptionInfo->ContextRecord->Edx, exceptionInfo->ContextRecord->Ebx,
		exceptionInfo->ContextRecord->Esp, exceptionInfo->ContextRecord->Ebp, exceptionInfo->ContextRecord->Esi,
		exceptionInfo->ContextRecord->Edi);
#endif

        Framework::Logging::GetLogger("MiniDump")->error(fmt::format("{}\nStack trace:\n {}", crashInfo, sw.GetOutputDump()));
        Framework::Logging::GetLogger("MiniDump")->flush();
        isCaptureEnabled = true;

        Sleep(2000);

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
