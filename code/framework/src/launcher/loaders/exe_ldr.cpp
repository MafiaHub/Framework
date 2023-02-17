/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

/*
 * This file is based on ExecutableLoader from: https://github.com/citizenfx/fivem/blob/cbe56f78f86bebb68d7960a38c3cdc31c7d76790/code/client/launcher/ExecutableLoader.cpp
 * See: https://github.com/citizenfx/fivem/blob/master/code/LICENSE
 */

#include "exe_ldr.h"

#include "logging/logger.h"

#include <utils/hooking/hooking.h>

namespace Framework::Launcher::Loaders {
    ExecutableLoader::ExecutableLoader(const uint8_t *origBinary) {
        hook::set_base();

        _origBinary = origBinary;
        _loadLimit  = SIZE_MAX;

        SetLibraryLoader([](const char *name) {
            return LoadLibraryA(name);
        });

        SetFunctionResolver([](HMODULE module, const char *name) {
            return (LPVOID)GetProcAddress(module, name);
        });
    }

    void ExecutableLoader::LoadImports(IMAGE_NT_HEADERS *ntHeader) {
        IMAGE_DATA_DIRECTORY *importDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

        auto descriptor = GetTargetRVA<IMAGE_IMPORT_DESCRIPTOR>(importDirectory->VirtualAddress);

        while (descriptor->Name) {
            const char *name = GetTargetRVA<char>(descriptor->Name);

            HMODULE module = ResolveLibrary(name);

            if (!module) {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Could not load dependent module {}. Error code was {}.", name, GetLastError());
                exit(0);
            }

            // "don't load"
            if (reinterpret_cast<uintptr_t>(module) == 0xFFFFFFFF) {
                descriptor++;
                continue;
            }

            auto nameTableEntry    = GetTargetRVA<uintptr_t>(descriptor->OriginalFirstThunk);
            auto addressTableEntry = GetTargetRVA<uintptr_t>(descriptor->FirstThunk);

            // GameShield (Payne) uses FirstThunk for original name addresses
            if (!descriptor->OriginalFirstThunk) {
                nameTableEntry = GetTargetRVA<uintptr_t>(descriptor->FirstThunk);
            }

            while (*nameTableEntry) {
                FARPROC function;
                const char *functionName;

                // is this an ordinal-only import?
                if (IMAGE_SNAP_BY_ORDINAL(*nameTableEntry)) {
                    uint64_t ordinalId = IMAGE_ORDINAL(*nameTableEntry);
                    function           = GetProcAddress(module, MAKEINTRESOURCEA(ordinalId));
                    static char _backingFunctionNameBuf[4096];
                    ::snprintf(_backingFunctionNameBuf, 4096, "#%lld", ordinalId);
                    functionName = _backingFunctionNameBuf;
                }
                else {
                    auto import = GetTargetRVA<IMAGE_IMPORT_BY_NAME>(*nameTableEntry);

                    function     = (FARPROC)_functionResolver(module, import->Name);
                    functionName = import->Name;
                }

                if (!function) {
                    char pathName[MAX_PATH];
                    GetModuleFileNameA(module, pathName, sizeof(pathName));

                    Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Could not load function {} in dependent module {} ({}).", functionName, name, pathName);
                }

                *addressTableEntry = (uintptr_t)function;

                nameTableEntry++;
                addressTableEntry++;
            }

            descriptor++;
        }
    }

    void ExecutableLoader::LoadSection(IMAGE_SECTION_HEADER *section) {
        void *targetAddress       = GetTargetRVA<uint8_t>(section->VirtualAddress);
        if (!targetAddress) {
            return;
        }

        // Check if the target address is within the allowed bounds
        if ((uintptr_t)targetAddress >= (_loadLimit + hook::baseAddressDifference)) {
            return;
        }

        // Check if the section has any data to be copied
        if (section->SizeOfRawData == 0) {
            return;
        }

        const void *sourceAddress = _origBinary + section->PointerToRawData;
        if (!sourceAddress) {
            return;
        }

        // Calculate the size of data to be copied
        uint32_t sizeOfData = std::min(section->SizeOfRawData, section->Misc.VirtualSize);

        // Copy the data
        memcpy(targetAddress, sourceAddress, sizeOfData);

        // Change the protection attributes of the target address
        DWORD oldProtect;
        if (!VirtualProtect(targetAddress, section->Misc.VirtualSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return;
        }

        DWORD protection = 0;
        if (section->Characteristics & IMAGE_SCN_MEM_NOT_CACHED) {
            protection |= PAGE_NOCACHE;
        }

        if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            if (section->Characteristics & IMAGE_SCN_MEM_READ) {
                if (section->Characteristics & IMAGE_SCN_MEM_WRITE) {
                    protection |= PAGE_EXECUTE_READWRITE;
                }
                else {
                    protection |= PAGE_EXECUTE_READ;
                }
            }
            else {
                if (section->Characteristics & IMAGE_SCN_MEM_WRITE) {
                    protection |= PAGE_EXECUTE_WRITECOPY;
                }
                else {
                    protection |= PAGE_EXECUTE;
                }
            }
        }
        else {
            if (section->Characteristics & IMAGE_SCN_MEM_READ) {
                if (section->Characteristics & IMAGE_SCN_MEM_WRITE) {
                    protection |= PAGE_READWRITE;
                }
                else {
                    protection |= PAGE_READONLY;
                }
            }
            else {
                if (section->Characteristics & IMAGE_SCN_MEM_WRITE) {
                    protection |= PAGE_WRITECOPY;
                }
                else {
                    protection |= PAGE_NOACCESS;
                }
            }
        }

        if (protection) {
            _targetProtections.push_back({targetAddress, section->Misc.VirtualSize, protection});
        }
    }

    void ExecutableLoader::LoadSections(IMAGE_NT_HEADERS *ntHeader) {
        IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(ntHeader);

        for (int i = 0; i < ntHeader->FileHeader.NumberOfSections; i++) {
            LoadSection(section);
            section++;
        }
    }

    void ExecutableLoader::Protect() {
        for (const auto &protection : _targetProtections) {
            DWORD op;
            VirtualProtect(std::get<0>(protection), std::get<1>(protection), std::get<2>(protection), &op);
        }
    }

#if defined(_M_AMD64)
    void ExecutableLoader::LoadExceptionTable(IMAGE_NT_HEADERS *ntHeader) {
        IMAGE_DATA_DIRECTORY *exceptionDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];

        RUNTIME_FUNCTION *functionList = GetTargetRVA<RUNTIME_FUNCTION>(exceptionDirectory->VirtualAddress);
        DWORD entryCount               = exceptionDirectory->Size / sizeof(RUNTIME_FUNCTION);

        if (HMODULE coreRT = GetModuleHandleW(L"FrameworkLoaderData.dll")) {
            auto sehMapper = (void (*)(void *, void *, PRUNTIME_FUNCTION, DWORD))GetProcAddress(coreRT, "CoreRT_SetupSEHHandler");
            sehMapper(_module, ((char *)_module) + ntHeader->OptionalHeader.SizeOfImage, functionList, entryCount);
        }
    }
#endif
    void ExecutableLoader::LoadIntoModule(HMODULE module) {
        _module = module;

        IMAGE_DOS_HEADER *header = (IMAGE_DOS_HEADER *)_origBinary;

        if (header->e_magic != IMAGE_DOS_SIGNATURE) {
            return;
        }

        IMAGE_DOS_HEADER *sourceHeader   = (IMAGE_DOS_HEADER *)module;
        IMAGE_NT_HEADERS *sourceNtHeader = GetTargetRVA<IMAGE_NT_HEADERS>(sourceHeader->e_lfanew);

        auto origCheckSum  = sourceNtHeader->OptionalHeader.CheckSum;
        auto origTimeStamp = sourceNtHeader->FileHeader.TimeDateStamp;
        auto origDebugDir  = sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];

        IMAGE_NT_HEADERS *ntHeader = (IMAGE_NT_HEADERS *)(_origBinary + header->e_lfanew);
        _entryPoint                = GetTargetRVA<void>(ntHeader->OptionalHeader.AddressOfEntryPoint);

        DWORD oldProtect1;
        VirtualProtect(sourceNtHeader, 0x1000, PAGE_EXECUTE_READWRITE, &oldProtect1);
        sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC] = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        ApplyRelocations();

        LoadSections(ntHeader);
        LoadImports(ntHeader);

        uint32_t tlsIndex = 0;
        void *tlsInit     = nullptr;

        if (_tlsInitializer)
            _tlsInitializer(&tlsInit, &tlsIndex);

#if defined(_M_AMD64)
        LoadExceptionTable(ntHeader);
#endif

        if (ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
            const IMAGE_TLS_DIRECTORY *targetTls = GetTargetRVA<IMAGE_TLS_DIRECTORY>(sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
            const IMAGE_TLS_DIRECTORY *sourceTls = GetTargetRVA<IMAGE_TLS_DIRECTORY>(ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);

            if (sourceTls->AddressOfIndex) {
                *(DWORD *)(sourceTls->AddressOfIndex) = 0;
            }

#if defined(_M_IX86)
            LPVOID *tlsBase = (LPVOID *)__readfsdword(0x2C);
#elif defined(_M_AMD64)
            LPVOID *tlsBase = (LPVOID *)__readgsqword(0x58);
#endif

            if (sourceTls->StartAddressOfRawData && tlsInit != nullptr) {
                DWORD oldProtect;

                VirtualProtect(tlsInit, sourceTls->EndAddressOfRawData - sourceTls->StartAddressOfRawData, PAGE_READWRITE, &oldProtect);
                memcpy(tlsBase[tlsIndex], reinterpret_cast<void *>(sourceTls->StartAddressOfRawData), sourceTls->EndAddressOfRawData - sourceTls->StartAddressOfRawData);
                memcpy(tlsInit, reinterpret_cast<void *>(sourceTls->StartAddressOfRawData), sourceTls->EndAddressOfRawData - sourceTls->StartAddressOfRawData);
            }

            if (sourceTls->AddressOfIndex) {
                hook::put(sourceTls->AddressOfIndex, tlsIndex);
            }
        }

        // copy over the offset to the new imports directory
        sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        memcpy(sourceNtHeader, ntHeader, sizeof(IMAGE_NT_HEADERS) + (ntHeader->FileHeader.NumberOfSections * (sizeof(IMAGE_SECTION_HEADER))));

        sourceNtHeader->OptionalHeader.CheckSum  = origCheckSum;
        sourceNtHeader->FileHeader.TimeDateStamp = origTimeStamp;

        sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG] = origDebugDir;
    }

    bool ExecutableLoader::ApplyRelocations() {
        IMAGE_DOS_HEADER *dosHeader = reinterpret_cast<IMAGE_DOS_HEADER *>(_module);

        IMAGE_NT_HEADERS *ntHeader = GetTargetRVA<IMAGE_NT_HEADERS>(dosHeader->e_lfanew);

        IMAGE_DATA_DIRECTORY *relocationDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

        IMAGE_BASE_RELOCATION *relocation    = GetTargetRVA<IMAGE_BASE_RELOCATION>(relocationDirectory->VirtualAddress);
        IMAGE_BASE_RELOCATION *endRelocation = reinterpret_cast<IMAGE_BASE_RELOCATION *>((char *)relocation + relocationDirectory->Size);

        intptr_t relocOffset = reinterpret_cast<intptr_t>(_module) - reinterpret_cast<intptr_t>(GetModuleHandle(NULL));

        if (relocOffset == 0) {
            return true;
        }

        // loop
        while (true) {
            // are we past the size?
            if (relocation >= endRelocation) {
                break;
            }

            // is this an empty block?
            if (relocation->SizeOfBlock == 0) {
                break;
            }

            // go through each and every relocation
            size_t numRelocations = (relocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
            uint16_t *relocStart  = reinterpret_cast<uint16_t *>(relocation + 1);

            for (size_t i = 0; i < numRelocations; i++) {
                uint16_t type = relocStart[i] >> 12;
                uint32_t rva  = (relocStart[i] & 0xFFF) + relocation->VirtualAddress;

                void *addr = GetTargetRVA<void>(rva);
                DWORD oldProtect;
                VirtualProtect(addr, 4, PAGE_EXECUTE_READWRITE, &oldProtect);

                if (type == IMAGE_REL_BASED_HIGHLOW) {
                    *reinterpret_cast<int32_t *>(addr) += relocOffset;
                }
                else if (type == IMAGE_REL_BASED_DIR64) {
                    *reinterpret_cast<int64_t *>(addr) += relocOffset;
                }
                else if (type != IMAGE_REL_BASED_ABSOLUTE) {
                    return false;
                }

                VirtualProtect(addr, 4, oldProtect, &oldProtect);
            }

            // on to the next one!
            relocation = reinterpret_cast<IMAGE_BASE_RELOCATION *>((char *)relocation + relocation->SizeOfBlock);
        }

        return true;
    }

    HMODULE ExecutableLoader::ResolveLibrary(const char *name) {
        return _libraryLoader(name);
    }
} // namespace Framework::Launcher::Loaders
