/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "crash_trail.h"

#ifdef _WIN32
#include <utils/safe_win32.h>

#include <logging/logger.h>

#include <fmt/format.h>

#include <atomic>
#include <cstdint>
#include <string>
#endif

namespace Framework::External::Sentry {
#ifdef _WIN32
    namespace {
        constexpr int kMaxFrames = 48;

        // Words read off the stack when the unwind stops short, and how many
        // of the code addresses among them are written.
        constexpr int kMaxScannedWords = 2048;
        constexpr int kMaxScannedHits  = 24;

        // An unwind shorter than this most likely broke on a frame with no
        // unwind data, so the raw scan is written as well.
        constexpr int kShortWalk = 6;

        LPTOP_LEVEL_EXCEPTION_FILTER gNext = nullptr;
        bool gInstalled                    = false;

        // One trail per process: a second thread faulting while the first is
        // being written goes straight on to crashpad.
        std::atomic_flag gWriting = ATOMIC_FLAG_INIT;

        /** `module+0xoffset`, or the bare address when no module holds it. */
        std::string Describe(std::uint64_t address) {
            HMODULE module = nullptr;
            if (address == 0 || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(address)), &module)) {
                return fmt::format("0x{:X}", address);
            }

            char path[MAX_PATH] {};
            const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
            const char *name   = path;
            for (DWORD index = 0; index < length; ++index) {
                if (path[index] == '\\' || path[index] == '/') {
                    name = path + index + 1;
                }
            }
            return fmt::format("{}+0x{:X}", name, address - reinterpret_cast<std::uint64_t>(module));
        }

        std::string ThreadName() {
            // Looked up rather than linked: it is newer than the SDK floor.
            using GetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PWSTR *);
            const auto getDescription    = reinterpret_cast<GetThreadDescriptionFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
            if (getDescription == nullptr) {
                return {};
            }

            PWSTR description = nullptr;
            if (FAILED(getDescription(GetCurrentThread(), &description)) || description == nullptr) {
                return {};
            }

            char utf8[256] {};
            WideCharToMultiByte(CP_UTF8, 0, description, -1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
            LocalFree(description);
            return utf8;
        }

        bool OnStack(std::uint64_t address, std::uint64_t low, std::uint64_t high) {
            return address >= low && address + sizeof(ULONG_PTR) <= high && (address & (sizeof(ULONG_PTR) - 1)) == 0;
        }

        std::uint64_t StackPointer(const CONTEXT &context) {
#if defined(_M_X64)
            return context.Rsp;
#else
            return context.Esp;
#endif
        }

        /**
         * Unwinds the faulting context with each module's own unwind data.
         *
         * No C++ objects here, so the walk can sit in `__try`: a frame whose
         * unwind data is wrong for where it stopped must end the walk, not
         * fault inside the filter.
         */
#if defined(_M_X64)
        int Walk(CONTEXT context, std::uint64_t low, std::uint64_t high, std::uint64_t *frames) {
            int count = 0;
            __try {
                while (count < kMaxFrames && context.Rip != 0) {
                    frames[count++] = context.Rip;

                    DWORD64 imageBase              = 0;
                    PRUNTIME_FUNCTION functionEntry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
                    if (functionEntry == nullptr) {
                        // A leaf, or code without unwind data: the return
                        // address is the word on top of the stack.
                        if (!OnStack(context.Rsp, low, high)) {
                            break;
                        }
                        context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
                        context.Rsp += sizeof(DWORD64);
                        continue;
                    }

                    PVOID handlerData        = nullptr;
                    DWORD64 establisherFrame = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, functionEntry, &context, &handlerData, &establisherFrame, nullptr);
                    if (!OnStack(context.Rsp, low, high)) {
                        break;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            return count;
        }
#else
        /**
         * x86 has no table-based unwind: follow the EBP chain. A frame built
         * without a frame pointer ends it early, and the stack scan below
         * covers what it misses.
         */
        int Walk(CONTEXT context, std::uint64_t low, std::uint64_t high, std::uint64_t *frames) {
            int count = 0;
            __try {
                frames[count++]     = context.Eip;
                std::uint64_t frame = context.Ebp;
                while (count < kMaxFrames && OnStack(frame, low, high) && OnStack(frame + sizeof(DWORD), low, high)) {
                    const DWORD returnAddress = *reinterpret_cast<const DWORD *>(static_cast<ULONG_PTR>(frame + sizeof(DWORD)));
                    const DWORD nextFrame     = *reinterpret_cast<const DWORD *>(static_cast<ULONG_PTR>(frame));
                    if (returnAddress == 0) {
                        break;
                    }
                    frames[count++] = returnAddress;
                    if (nextFrame <= frame) {
                        break;
                    }
                    frame = nextFrame;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            return count;
        }
#endif

        bool IsCode(std::uint64_t address) {
            MEMORY_BASIC_INFORMATION region {};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(static_cast<ULONG_PTR>(address)), &region, sizeof(region)) == 0 || region.State != MEM_COMMIT) {
                return false;
            }
            return (region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        }

        const char *AccessKind(ULONG_PTR kind) {
            switch (kind) {
            case 0: return "read";
            case 1: return "write";
            case 8: return "execute";
            }
            return "access";
        }

        void Write(const EXCEPTION_POINTERS *exceptionInfo) {
            const EXCEPTION_RECORD *record = exceptionInfo->ExceptionRecord;
            const CONTEXT *context         = exceptionInfo->ContextRecord;

            ULONG_PTR stackLow  = 0;
            ULONG_PTR stackHigh = 0;
            GetCurrentThreadStackLimits(&stackLow, &stackHigh);

            std::string text = fmt::format("Unhandled exception 0x{:08X} at {} on thread {} '{}'\n", record->ExceptionCode, Describe(reinterpret_cast<std::uint64_t>(record->ExceptionAddress)), GetCurrentThreadId(), ThreadName());
            if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && record->NumberParameters >= 2) {
                text += fmt::format("Faulting {} of 0x{:X}\n", AccessKind(record->ExceptionInformation[0]), record->ExceptionInformation[1]);
            }

#if defined(_M_X64)
            text += fmt::format("RIP {:016X} RSP {:016X} RBP {:016X}\n", context->Rip, context->Rsp, context->Rbp);
            text += fmt::format("RAX {:016X} RBX {:016X} RCX {:016X} RDX {:016X}\n", context->Rax, context->Rbx, context->Rcx, context->Rdx);
            text += fmt::format("RSI {:016X} RDI {:016X} R8  {:016X} R9  {:016X}\n", context->Rsi, context->Rdi, context->R8, context->R9);
            text += fmt::format("R10 {:016X} R11 {:016X} R12 {:016X} R13 {:016X}\n", context->R10, context->R11, context->R12, context->R13);
            text += fmt::format("R14 {:016X} R15 {:016X}\n", context->R14, context->R15);
#else
            text += fmt::format("EIP {:08X} ESP {:08X} EBP {:08X}\n", context->Eip, context->Esp, context->Ebp);
            text += fmt::format("EAX {:08X} EBX {:08X} ECX {:08X} EDX {:08X}\n", context->Eax, context->Ebx, context->Ecx, context->Edx);
            text += fmt::format("ESI {:08X} EDI {:08X}\n", context->Esi, context->Edi);
#endif

            std::uint64_t frames[kMaxFrames] {};
            const int frameCount = Walk(*context, stackLow, stackHigh, frames);
            text += "Unwound stack:\n";
            for (int index = 0; index < frameCount; ++index) {
                text += fmt::format("  #{:02} {}\n", index, Describe(frames[index]));
            }

            // Code with no unwind data -- a manually mapped image, generated
            // code -- ends the walk early. The words on the stack that point
            // into executable memory are then the best account of the callers.
            if (frameCount < kShortWalk) {
                text += "Code addresses on the stack:\n";
                int hits = 0;
                for (int word = 0; word < kMaxScannedWords && hits < kMaxScannedHits; ++word) {
                    const std::uint64_t slot = StackPointer(*context) + word * sizeof(ULONG_PTR);
                    if (!OnStack(slot, stackLow, stackHigh)) {
                        break;
                    }
                    const std::uint64_t value = *reinterpret_cast<const ULONG_PTR *>(static_cast<ULONG_PTR>(slot));
                    if (IsCode(value)) {
                        text += fmt::format("  [SP+0x{:X}] {}\n", word * sizeof(ULONG_PTR), Describe(value));
                        ++hits;
                    }
                }
            }

            // Synchronous, so the trail is on disk before crashpad suspends the
            // process; the async loggers get a moment to drain their last lines.
            const auto logger = Framework::Logging::GetLogger("CrashTrail", false);
            logger->critical(text);
            logger->flush();
            Sleep(500);
        }

        LONG WINAPI Filter(EXCEPTION_POINTERS *exceptionInfo) {
            if (!gWriting.test_and_set()) {
                Write(exceptionInfo);
            }
            return gNext != nullptr ? gNext(exceptionInfo) : EXCEPTION_CONTINUE_SEARCH;
        }
    } // namespace

    void CrashTrail::Install() {
        if (gInstalled) {
            return;
        }
        gInstalled = true;
        gNext      = SetUnhandledExceptionFilter(Filter);
    }
#else
    void CrashTrail::Install() {}
#endif
} // namespace Framework::External::Sentry
