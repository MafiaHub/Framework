/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

/*
 * This file is based on ExecutableLoader from:
 * https://github.com/citizenfx/fivem/blob/cbe56f78f86bebb68d7960a38c3cdc31c7d76790/code/client/launcher/ExecutableLoader.cpp
 * See: https://github.com/citizenfx/fivem/blob/master/code/LICENSE
 */

#include "exe_ldr.h"

#include <delayimp.h>

#include "logging/logger.h"
#include <utils/hooking/hooking.h>

namespace Framework::Launcher::Loaders {
    ExecutableLoader::ExecutableLoader(const uint8_t *origBinary, size_t binarySize) {
        hook::set_base();

        _origBinary = origBinary;
        _totalBinarySize = binarySize;
        _loadLimit  = SIZE_MAX;

        SetLibraryLoader([](const char *name) {
            return LoadLibraryA(name);
        });

        SetFunctionResolver([](HMODULE module, const char *name) {
            return (LPVOID)GetProcAddress(module, name);
        });
    }

    void ExecutableLoader::LoadImports(IMAGE_NT_HEADERS *ntHeader) {
        const IMAGE_DATA_DIRECTORY *importDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

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
                    const uint64_t ordinalId = IMAGE_ORDINAL(*nameTableEntry);
                    function                 = GetProcAddress(module, MAKEINTRESOURCEA(ordinalId));
                    static char _backingFunctionNameBuf[4096];
                    ::snprintf(_backingFunctionNameBuf, 4096, "#%lld", ordinalId);
                    functionName = _backingFunctionNameBuf;
                }
                else {
                    const auto import = GetTargetRVA<IMAGE_IMPORT_BY_NAME>(*nameTableEntry);

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
        void *targetAddress = GetTargetRVA<uint8_t>(section->VirtualAddress);
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

        // Check if section data exceeds binary size
        if (section->PointerToRawData + section->SizeOfRawData > _totalBinarySize) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Section data exceeds binary size");
            return;
        }

        const void *sourceAddress = _origBinary + section->PointerToRawData;
        if (!sourceAddress) {
            return;
        }

        // Calculate the size of data to be copied
        const uint32_t sizeOfData = (section->SizeOfRawData < section->Misc.VirtualSize) ? section->SizeOfRawData : section->Misc.VirtualSize;

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
        auto section = IMAGE_FIRST_SECTION(ntHeader);

        for (int i = 0; i < ntHeader->FileHeader.NumberOfSections; i++) {
            LoadSection(section);
            section++;
        }
    }

    void ExecutableLoader::Protect() const {
        for (const auto &protection : _targetProtections) {
            DWORD op;
            VirtualProtect(std::get<0>(protection), std::get<1>(protection), std::get<2>(protection), &op);
        }
    }

#if defined(_M_AMD64)
    void ExecutableLoader::LoadExceptionTable(IMAGE_NT_HEADERS *ntHeader) {
        const IMAGE_DATA_DIRECTORY *exceptionDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];

        RUNTIME_FUNCTION *functionList = GetTargetRVA<RUNTIME_FUNCTION>(exceptionDirectory->VirtualAddress);
        const DWORD entryCount         = exceptionDirectory->Size / sizeof(RUNTIME_FUNCTION);

        if (const HMODULE coreRT = GetModuleHandleW(L"FrameworkLoaderData.dll")) {
            const auto sehMapper = (void (*)(void *, void *, PRUNTIME_FUNCTION, DWORD))GetProcAddress(coreRT, "CoreRT_SetupSEHHandler");
            sehMapper(_module, ((char *)_module) + ntHeader->OptionalHeader.SizeOfImage, functionList, entryCount);
        }
    }
#endif

    void ExecutableLoader::LoadDelayImports(IMAGE_NT_HEADERS *ntHeader) {
        const IMAGE_DATA_DIRECTORY *delayImportDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        
        // Check if there are any delay imports
        if (delayImportDirectory->VirtualAddress == 0 || delayImportDirectory->Size == 0) {
            return;
        }
        
        // Get the first delay import descriptor
        auto descriptor = GetTargetRVA<IMAGE_DELAYLOAD_DESCRIPTOR>(delayImportDirectory->VirtualAddress);
        
        while (descriptor->DllNameRVA != 0) {
            const char *name = GetTargetRVA<char>(descriptor->DllNameRVA);
            
            HMODULE module = ResolveLibrary(name);
            
            if (!module) {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Could not load delayed dependent module {}. Error code was {}.", name, GetLastError());
                // Continue anyway, delay-loaded DLLs are optional at load time
                descriptor++;
                continue;
            }
            
            // "don't load"
            if (reinterpret_cast<uintptr_t>(module) == 0xFFFFFFFF) {
                descriptor++;
                continue;
            }
            
            // Load each import name and function
            auto nameArray = GetTargetRVA<IMAGE_THUNK_DATA>(descriptor->ImportNameTableRVA);
            auto addressArray = GetTargetRVA<IMAGE_THUNK_DATA>(descriptor->ImportAddressTableRVA);
            
            // Process each import in the array
            for (size_t i = 0; nameArray[i].u1.AddressOfData != 0; i++) {
                FARPROC function;
                const char *functionName;
                
                // Check if this is an ordinal import
                if (IMAGE_SNAP_BY_ORDINAL(nameArray[i].u1.Ordinal)) {
                    WORD ordinal = IMAGE_ORDINAL(nameArray[i].u1.Ordinal);
                    function = GetProcAddress(module, (LPCSTR)(ULONG_PTR)ordinal);
                    
                    static char ordinalNameBuf[32];
                    sprintf_s(ordinalNameBuf, "#%u", ordinal);
                    functionName = ordinalNameBuf;
                } else {
                    // Get the function name from the import by name structure
                    auto importByName = GetTargetRVA<IMAGE_IMPORT_BY_NAME>(nameArray[i].u1.AddressOfData);
                    functionName = importByName->Name;
                    
                    // Resolve the function
                    function = (FARPROC)_functionResolver(module, functionName);
                }
                
                if (!function) {
                    char pathName[MAX_PATH];
                    GetModuleFileNameA(module, pathName, sizeof(pathName));
                    
                    Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Could not load delayed function {} in dependent module {} ({}).", functionName, name, pathName);
                }
                
                // Store the function address
                addressArray[i].u1.Function = reinterpret_cast<DWORD_PTR>(function);
            }
            
            descriptor++;
        }
    }

    void ExecutableLoader::LoadIntoModule(HMODULE module) {
        _module = module;

        const auto header = (IMAGE_DOS_HEADER *)_origBinary;

        if (header->e_magic != IMAGE_DOS_SIGNATURE) {
            return;
        }

        const auto sourceHeader          = (IMAGE_DOS_HEADER *)module;
        IMAGE_NT_HEADERS *sourceNtHeader = GetTargetRVA<IMAGE_NT_HEADERS>(sourceHeader->e_lfanew);

        const auto origCheckSum  = sourceNtHeader->OptionalHeader.CheckSum;
        const auto origTimeStamp = sourceNtHeader->FileHeader.TimeDateStamp;
        const auto origDebugDir  = sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];

        const auto ntHeader = (IMAGE_NT_HEADERS *)(_origBinary + header->e_lfanew);
        _entryPoint         = GetTargetRVA<void>(ntHeader->OptionalHeader.AddressOfEntryPoint);

        DWORD oldProtect1;
        VirtualProtect(sourceNtHeader, 0x1000, PAGE_EXECUTE_READWRITE, &oldProtect1);
        sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC] = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

        LoadSections(ntHeader);

        // Apply relocations AFTER sections are loaded, so we can read the relocation data
        // from the now-copied sections and apply fixups to absolute addresses
        ApplyRelocations();

        LoadImports(ntHeader);
        LoadDelayImports(ntHeader);

#if defined(_M_AMD64)
        LoadExceptionTable(ntHeader);
#endif

        // TLS Setup - Two approaches supported:
        // 1. Direct slot 0: Launcher EXE has sacrificial TLS buffer claiming slot 0
        // 2. Allocated slot: Framework allocates a TLS slot for the game via _tlsInitializer
        if (ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
            const IMAGE_TLS_DIRECTORY *targetTls = GetTargetRVA<IMAGE_TLS_DIRECTORY>(sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
            const IMAGE_TLS_DIRECTORY *sourceTls = GetTargetRVA<IMAGE_TLS_DIRECTORY>(ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);

            const size_t tlsDataSize = sourceTls->EndAddressOfRawData - sourceTls->StartAddressOfRawData;

#if defined(_M_IX86)
            LPVOID *tlsBase = (LPVOID *)__readfsdword(0x2C);
#elif defined(_M_AMD64)
            LPVOID *tlsBase = (LPVOID *)__readgsqword(0x58);
#endif

            // Get slot 0 pointer (for direct slot 0 approach)
            LPVOID tlsSlot0 = tlsBase ? tlsBase[0] : nullptr;

            uint32_t tlsIndex = 0;
            void *tlsInit = nullptr;

            // Get allocated TLS from framework DLL (for traditional approach)
            if (_tlsInitializer) {
                _tlsInitializer(&tlsInit, &tlsIndex);
            }

            // Use direct slot 0 if configured (requires sacrificial TLS buffer in launcher EXE)
            // Otherwise use the allocated slot from _tlsInitializer
            const bool useDirectSlot0 = _useDirectTlsSlot0 && (tlsSlot0 != nullptr);

            if (sourceTls->AddressOfIndex) {
                *(DWORD *)(sourceTls->AddressOfIndex) = 0;
            }

            if (sourceTls->StartAddressOfRawData && tlsDataSize > 0) {
                if (useDirectSlot0) {
                    // Direct slot 0 approach (for launchers with sacrificial TLS buffer)
                    DWORD oldProtect;
                    VirtualProtect(reinterpret_cast<LPVOID>(targetTls->StartAddressOfRawData), tlsDataSize, PAGE_READWRITE, &oldProtect);

                    std::memcpy(tlsSlot0, reinterpret_cast<void *>(sourceTls->StartAddressOfRawData), tlsDataSize);
                    std::memcpy(reinterpret_cast<void *>(targetTls->StartAddressOfRawData),
                               reinterpret_cast<void *>(sourceTls->StartAddressOfRawData), tlsDataSize);
                }
                else if (tlsInit != nullptr && tlsBase != nullptr && tlsIndex < TLS_MINIMUM_AVAILABLE) {
                    // Allocated slot approach (traditional, for MafiaMP etc.)
                    DWORD oldProtect;
                    VirtualProtect(tlsInit, tlsDataSize, PAGE_READWRITE, &oldProtect);

                    memcpy(tlsBase[tlsIndex], reinterpret_cast<void *>(sourceTls->StartAddressOfRawData), tlsDataSize);
                    memcpy(tlsInit, reinterpret_cast<void *>(sourceTls->StartAddressOfRawData), tlsDataSize);
                }
            }

            // Set final TLS index
            if (sourceTls->AddressOfIndex) {
                hook::put(sourceTls->AddressOfIndex, useDirectSlot0 ? 0 : tlsIndex);
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
        const auto dosHeader = reinterpret_cast<IMAGE_DOS_HEADER *>(_module);

        const IMAGE_NT_HEADERS *ntHeader = GetTargetRVA<IMAGE_NT_HEADERS>(dosHeader->e_lfanew);

        const IMAGE_DATA_DIRECTORY *relocationDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

        IMAGE_BASE_RELOCATION *relocation = GetTargetRVA<IMAGE_BASE_RELOCATION>(relocationDirectory->VirtualAddress);
        const auto endRelocation          = reinterpret_cast<IMAGE_BASE_RELOCATION *>((char *)relocation + relocationDirectory->Size);

        // Calculate relocation offset: difference between where we're loading (_module)
        // and where the original binary was designed to load (its ImageBase)
        const auto origDosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(_origBinary);
        const auto origNtHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(_origBinary + origDosHeader->e_lfanew);
        const intptr_t relocOffset = reinterpret_cast<intptr_t>(_module) - static_cast<intptr_t>(origNtHeader->OptionalHeader.ImageBase);

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
            const size_t numRelocations = (relocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
            const auto relocStart       = reinterpret_cast<uint16_t *>(relocation + 1);

            for (size_t i = 0; i < numRelocations; i++) {
                const uint16_t type = relocStart[i] >> 12;
                const uint32_t rva  = (relocStart[i] & 0xFFF) + relocation->VirtualAddress;

                void *addr = GetTargetRVA<void>(rva);
                DWORD oldProtect;
                VirtualProtect(addr, (type == IMAGE_REL_BASED_DIR64) ? 8 : 4, PAGE_EXECUTE_READWRITE, &oldProtect);

                if (type == IMAGE_REL_BASED_HIGHLOW) {
                    *reinterpret_cast<int32_t *>(addr) += relocOffset;
                }
                else if (type == IMAGE_REL_BASED_DIR64) {
                    *reinterpret_cast<int64_t *>(addr) += relocOffset;
                }
                else if (type != IMAGE_REL_BASED_ABSOLUTE) {
                    return false;
                }

                VirtualProtect(addr, (type == IMAGE_REL_BASED_DIR64) ? 8 : 4, oldProtect, &oldProtect);
            }

            // on to the next one!
            relocation = reinterpret_cast<IMAGE_BASE_RELOCATION *>((char *)relocation + relocation->SizeOfBlock);
        }

        return true;
    }

    HMODULE ExecutableLoader::ResolveLibrary(const char *name) const {
        return _libraryLoader(name);
    }
} // namespace Framework::Launcher::Loaders
