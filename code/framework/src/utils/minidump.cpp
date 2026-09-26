/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "minidump.h"
#include <DbgHelp.h>
#include <winnt.h>

#include "logging/logger.h"
#include "utils/string_utils.h"

#include <StackWalker.h>

#include <string>

using CoreSetExceptionOverride = LONG (*)(EXCEPTION_POINTERS *);

namespace Framework::Utils {
    class StackWalkerSentry final: public StackWalker {
      private:
        std::string outputDump;

      protected:
        void OnOutput(LPCSTR dump) override {
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
            m_options = StackWalkOptions::RetrieveSymbol | StackWalkOptions::RetrieveLine | StackWalkOptions::SymUseSymSrv | StackWalkOptions::SymBuildPath;
        }

        inline std::string GetOutputDump() const {
            return outputDump;
        }
    };

    LONG WINAPI MiniDump::ExceptionFilter(EXCEPTION_POINTERS *exceptionInfo) {
        if (exceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (_isCaptureEnabled.exchange(false) == false) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        std::wstring dumpPath;
        DWORD dumpError = ERROR_SUCCESS;
        if (!_dumpDirectory.empty()) {
            SYSTEMTIME now {};
            GetSystemTime(&now);
            wchar_t filename[128];
            swprintf_s(filename, L"Crash-%04u%02u%02u-%02u%02u%02u-%lu-%lu.dmp", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId(), GetCurrentThreadId());
            dumpPath = _dumpDirectory + L"\\" + filename;

            if (!CreateDirectoryW(_dumpDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
                dumpError = GetLastError();
            }
            else {
                const HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) {
                    dumpError = GetLastError();
                }
                else {
                    MINIDUMP_EXCEPTION_INFORMATION exception {GetCurrentThreadId(), exceptionInfo, FALSE};
                    constexpr auto dumpType = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithFullMemoryInfo);
                    if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, dumpType, &exception, nullptr, nullptr)) {
                        dumpError = GetLastError();
                    }
                    CloseHandle(file);
                    if (dumpError != ERROR_SUCCESS) {
                        DeleteFileW(dumpPath.c_str());
                    }
                }
            }
        }

        StackWalkerSentry sw;
        sw.SetSymPath(_symbolPath.c_str());
        sw.ShowCallstack(GetCurrentThread(), exceptionInfo->ContextRecord);
#ifdef _M_AMD64
        std::string crashInfo = fmt::format("{:X} with code: {:X} \n\nRegisters: \n"
                                            "RAX: {:<12x} RCX: {:<12x} \n"
                                            "RDX: {:<12x} RBX: {:<12x} \n"
                                            "RSP: {:<12x} RBP: {:<12x} \n"
                                            "RSI: {:<12x} RDI: {:<12x} \n"
                                            "R8 : {:<12x} R9 : {:<12x} \n"
                                            "R10: {:<12x} R11: {:<12x} \n"
                                            "R12: {:<12x} R13: {:<12x} \n"
                                            "R14: {:<12x} R15: {:<12x} \n",
            (DWORD64)exceptionInfo->ExceptionRecord->ExceptionAddress, exceptionInfo->ExceptionRecord->ExceptionCode, exceptionInfo->ContextRecord->Rax, exceptionInfo->ContextRecord->Rcx, exceptionInfo->ContextRecord->Rdx, exceptionInfo->ContextRecord->Rbx,
            exceptionInfo->ContextRecord->Rsp, exceptionInfo->ContextRecord->Rbp, exceptionInfo->ContextRecord->Rsi, exceptionInfo->ContextRecord->Rdi, exceptionInfo->ContextRecord->R8, exceptionInfo->ContextRecord->R9, exceptionInfo->ContextRecord->R10,
            exceptionInfo->ContextRecord->R11, exceptionInfo->ContextRecord->R12, exceptionInfo->ContextRecord->R13, exceptionInfo->ContextRecord->R14, exceptionInfo->ContextRecord->R15);
#elif _M_IX86
        std::string crashInfo = fmt::format("{:X} with code: {:X} \n\nRegisters: \n"
                                            "EAX: {:<12x} ECX: {:<12x} \n"
                                            "EDX: {:<12x} EBX: {:<12x} \n"
                                            "ESP: {:<12x} EBP: {:<12x} \n"
                                            "ESI: {:<12x} EDI: {:<12x} \n"
                                            "CS : {:<12x} SS : {:<12x} \n",
            (DWORD)exceptionInfo->ExceptionRecord->ExceptionAddress, exceptionInfo->ExceptionRecord->ExceptionCode, exceptionInfo->ContextRecord->Eax, exceptionInfo->ContextRecord->Ecx, exceptionInfo->ContextRecord->Edx, exceptionInfo->ContextRecord->Ebx,
            exceptionInfo->ContextRecord->Esp, exceptionInfo->ContextRecord->Ebp, exceptionInfo->ContextRecord->Esi, exceptionInfo->ContextRecord->Edi, exceptionInfo->ContextRecord->SegCs, exceptionInfo->ContextRecord->SegSs);
#endif

        const auto logger = Framework::Logging::GetLogger("MiniDump", false);
        if (!dumpPath.empty()) {
            if (dumpError == ERROR_SUCCESS) {
                logger->error("Crash dump written to {}", StringUtils::WideToNormal(dumpPath));
            }
            else {
                logger->error("Could not write crash dump to {} (Win32 error {})", StringUtils::WideToNormal(dumpPath), dumpError);
            }
        }
        logger->error("Unhandled exception at address: {}\nStack trace:\n\n{}", crashInfo, sw.GetOutputDump());
        logger->flush();

        // uncomment to break here
        //__debugbreak();

        // todo send to sentry

        return EXCEPTION_CONTINUE_SEARCH;
    }

    void MiniDump::InitExceptionOverride() {
        /*const auto coreSetExceptionOverride = reinterpret_cast<void(*
        __cdecl)(CoreSetExceptionOverride)>(GetProcAddress(GetModuleHandleA("FrameworkLoaderData.dll"),
        "CoreSetExceptionOverride")); if (coreSetExceptionOverride) { coreSetExceptionOverride(ExceptionFilter);
        }*/

        SetUnhandledExceptionFilter(ExceptionFilter);
    }

    MiniDump::MiniDump() {
        InitExceptionOverride();
    }
} // namespace Framework::Utils
