/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "process_identity.h"

#include "logging/logger.h"

#include <Windows.h>

namespace Framework::Launcher::Loaders {
    namespace {
        // Prefixes of the documented loader structures, declared locally to stay
        // independent of which Windows headers the launcher pulls in. Field offsets
        // match both x86 and x64.
        struct LoaderString {
            USHORT Length;
            USHORT MaximumLength;
            PWSTR Buffer;
        };

        struct LoaderTableEntry {
            LIST_ENTRY InLoadOrderLinks;
            LIST_ENTRY InMemoryOrderLinks;
            LIST_ENTRY InInitializationOrderLinks;
            PVOID DllBase;
            PVOID EntryPoint;
            ULONG SizeOfImage;
            LoaderString FullDllName;
            LoaderString BaseDllName;
        };

        struct LoaderData {
            ULONG Length;
            BOOLEAN Initialized;
            PVOID SsHandle;
            LIST_ENTRY InLoadOrderModuleList;
        };

        struct ProcessParametersPrefix {
            BYTE Reserved1[16];
            PVOID Reserved2[10];
            LoaderString ImagePathName;
            LoaderString CommandLine;
        };

        struct ProcessEnvironmentBlock {
            BYTE InheritedAddressSpace;
            BYTE ReadImageFileExecOptions;
            BYTE BeingDebugged;
            BYTE BitField;
            PVOID Mutant;
            PVOID ImageBaseAddress;
            LoaderData *Ldr;
            ProcessParametersPrefix *ProcessParameters;
        };

        ProcessEnvironmentBlock *CurrentPeb() {
#ifdef _M_AMD64
            return reinterpret_cast<ProcessEnvironmentBlock *>(__readgsqword(0x60));
#else
            return reinterpret_cast<ProcessEnvironmentBlock *>(__readfsdword(0x30));
#endif
        }

        // The loader keeps these pointers for the lifetime of the process, so the
        // replacements are intentionally never freed.
        void AssignLoaderString(LoaderString &target, const std::wstring &value) {
            const auto buffer = new wchar_t[value.size() + 1];
            std::char_traits<wchar_t>::copy(buffer, value.c_str(), value.size() + 1);

            target.Buffer        = buffer;
            target.Length        = static_cast<USHORT>(value.size() * sizeof(wchar_t));
            target.MaximumLength = static_cast<USHORT>(target.Length + sizeof(wchar_t));
        }

        LoaderTableEntry *FindMainImageEntry(ProcessEnvironmentBlock *peb) {
            const auto head = &peb->Ldr->InLoadOrderModuleList;
            for (auto link = head->Flink; link != head; link = link->Flink) {
                const auto entry = CONTAINING_RECORD(link, LoaderTableEntry, InLoadOrderLinks);
                if (entry->DllBase == peb->ImageBaseAddress) {
                    return entry;
                }
            }
            return nullptr;
        }
    } // namespace

    bool ApplyMappedImageIdentity(const std::wstring &imagePath) {
        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER);

        if (imagePath.empty()) {
            return false;
        }

        // Loader string lengths are 16-bit byte counts; a longer path would truncate.
        if ((imagePath.size() + 1) * sizeof(wchar_t) > MAXUINT16) {
            logger->warn("Game path is too long to publish as the process identity");
            return false;
        }

        const auto peb = CurrentPeb();
        if (!peb || !peb->Ldr || !peb->ProcessParameters) {
            logger->warn("Loader data is unavailable, the process keeps reporting the launcher path");
            return false;
        }

        const auto entry = FindMainImageEntry(peb);
        if (!entry) {
            logger->warn("No loader entry matches the process image base, the process keeps reporting the launcher path");
            return false;
        }

        const auto separator      = imagePath.find_last_of(L"\\/");
        const auto executableName = separator == std::wstring::npos ? imagePath : imagePath.substr(separator + 1);

        AssignLoaderString(entry->FullDllName, imagePath);
        AssignLoaderString(entry->BaseDllName, executableName);
        AssignLoaderString(peb->ProcessParameters->ImagePathName, imagePath);

        logger->info("Process identity now reports the game executable to every loaded module");
        return true;
    }
} // namespace Framework::Launcher::Loaders
