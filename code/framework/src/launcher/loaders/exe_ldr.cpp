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
        _imageSize = 0;

        SetLibraryLoader([](const char *name) {
            return LoadLibraryA(name);
        });

        SetFunctionResolver([](HMODULE module, const char *name) {
            return (LPVOID)GetProcAddress(module, name);
        });
    }

    ExecutableLoader::~ExecutableLoader() {
        CleanupOnError();
    }

    void ExecutableLoader::CleanupOnError() {
        for (auto mem : _allocatedMemory) {
            if (mem) {
                VirtualFree(mem, 0, MEM_RELEASE);
            }
        }
        _allocatedMemory.clear();
    }

    bool ExecutableLoader::ValidatePEHeaders(const IMAGE_NT_HEADERS *ntHeader) const {
        // Validate NT signature
        if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid NT signature: 0x{:X}", ntHeader->Signature);
            return false;
        }

        // Validate machine type
#ifdef _M_AMD64
        if (ntHeader->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid machine type for x64: 0x{:X}", ntHeader->FileHeader.Machine);
            return false;
        }
#elif defined(_M_IX86)
        if (ntHeader->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid machine type for x86: 0x{:X}", ntHeader->FileHeader.Machine);
            return false;
        }
#endif

        // Validate section count (Windows limit is 96)
        if (ntHeader->FileHeader.NumberOfSections > 96) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Too many sections: {}", ntHeader->FileHeader.NumberOfSections);
            return false;
        }

        // Validate optional header magic
#ifdef _M_AMD64
        if (ntHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid optional header magic for x64: 0x{:X}", ntHeader->OptionalHeader.Magic);
            return false;
        }
#elif defined(_M_IX86)
        if (ntHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid optional header magic for x86: 0x{:X}", ntHeader->OptionalHeader.Magic);
            return false;
        }
#endif

        // Validate data directory count
        if (ntHeader->OptionalHeader.NumberOfRvaAndSizes > IMAGE_NUMBEROF_DIRECTORY_ENTRIES) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid number of data directories: {}", ntHeader->OptionalHeader.NumberOfRvaAndSizes);
            return false;
        }

        return true;
    }

    bool ExecutableLoader::LoadImports(IMAGE_NT_HEADERS *ntHeader) {
        const IMAGE_DATA_DIRECTORY *importDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

        if (importDirectory->VirtualAddress == 0 || importDirectory->Size == 0) {
            return true; // No imports
        }

        auto descriptor = GetTargetRVA<IMAGE_IMPORT_DESCRIPTOR>(importDirectory->VirtualAddress);
        if (!descriptor) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid import descriptor RVA: 0x{:X}", importDirectory->VirtualAddress);
            return false;
        }

        while (descriptor->Name) {
            const char *name = GetTargetRVA<char>(descriptor->Name);
            if (!name) {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid import module name RVA: 0x{:X}", descriptor->Name);
                return false;
            }

            HMODULE module = ResolveLibrary(name);

            if (!module) {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Could not load dependent module {}. Error code was {}.", name, GetLastError());
                return false;
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
                    thread_local char ordinalNameBuf[64];
                    ::snprintf(ordinalNameBuf, sizeof(ordinalNameBuf), "#%lld", ordinalId);
                    functionName = ordinalNameBuf;
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
        return true;
    }

    bool ExecutableLoader::LoadSection(IMAGE_SECTION_HEADER *section) {
        // Validate section virtual address and size
        if (section->VirtualAddress + section->Misc.VirtualSize > _imageSize) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Section virtual address + size (0x{:X}) exceeds image size (0x{:X})", 
                section->VirtualAddress + section->Misc.VirtualSize, _imageSize);
            return false;
        }

        void *targetAddress = GetTargetRVA<uint8_t>(section->VirtualAddress);
        if (!targetAddress) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid target address for section at RVA 0x{:X}", section->VirtualAddress);
            return false;
        }

        // Check if the target address is within the allowed bounds
        if ((uintptr_t)targetAddress >= (_loadLimit + hook::baseAddressDifference)) {
            return true; // Skip section but don't fail
        }

        // Check if the section has any data to be copied
        if (section->SizeOfRawData == 0) {
            return true; // No data to copy, not an error
        }

        // Check if section data exceeds binary size
        if (section->PointerToRawData + section->SizeOfRawData > _totalBinarySize) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Section data exceeds binary size: offset=0x{:X}, size=0x{:X}, binary_size=0x{:X}",
                section->PointerToRawData, section->SizeOfRawData, _totalBinarySize);
            return false;
        }

        // Additional validation: check for integer overflow
        if (section->VirtualAddress > UINT32_MAX - section->Misc.VirtualSize) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Section virtual address causes overflow");
            return false;
        }
        
        if (section->PointerToRawData > UINT32_MAX - section->SizeOfRawData) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Section raw data pointer causes overflow");
            return false;
        }

        const void *sourceAddress = _origBinary + section->PointerToRawData;
        if (!sourceAddress) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid source address for section");
            return false;
        }

        // Calculate the size of data to be copied
        const uint32_t sizeOfData = (section->SizeOfRawData < section->Misc.VirtualSize) ? section->SizeOfRawData : section->Misc.VirtualSize;

        // Copy the data
        memcpy(targetAddress, sourceAddress, sizeOfData);

        // Zero out any remaining space in virtual size
        if (section->Misc.VirtualSize > section->SizeOfRawData) {
            const uint32_t remainingSize = section->Misc.VirtualSize - section->SizeOfRawData;
            memset((uint8_t*)targetAddress + sizeOfData, 0, remainingSize);
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

        // Store protection information to be applied later
        if (protection) {
            _targetProtections.push_back({targetAddress, section->Misc.VirtualSize, protection});
        }
        
        return true;
    }

    bool ExecutableLoader::LoadSections(IMAGE_NT_HEADERS *ntHeader) {
        auto section = IMAGE_FIRST_SECTION(ntHeader);

        for (int i = 0; i < ntHeader->FileHeader.NumberOfSections; i++) {
            if (!LoadSection(section)) {
                Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to load section {}", i);
                return false;
            }
            section++;
        }
        return true;
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

    bool ExecutableLoader::LoadDelayImports(IMAGE_NT_HEADERS *ntHeader) {
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
        return true;
    }

    void ExecutableLoader::ExecuteTLSCallbacks(IMAGE_NT_HEADERS *ntHeader) {
        const IMAGE_DATA_DIRECTORY *tlsDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
        
        if (tlsDirectory->VirtualAddress == 0 || tlsDirectory->Size == 0) {
            return; // No TLS data
        }
        
        const IMAGE_TLS_DIRECTORY *tlsData = GetTargetRVA<IMAGE_TLS_DIRECTORY>(tlsDirectory->VirtualAddress);
        if (!tlsData) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid TLS directory RVA: 0x{:X}", tlsDirectory->VirtualAddress);
            return;
        }
        
        // Execute TLS callbacks if they exist
        if (tlsData->AddressOfCallBacks) {
#ifdef _M_AMD64
            typedef void (NTAPI *TLS_CALLBACK_PROC)(PVOID, DWORD, PVOID);
            TLS_CALLBACK_PROC *callbacks = reinterpret_cast<TLS_CALLBACK_PROC *>(tlsData->AddressOfCallBacks);
#else
            typedef void (NTAPI *TLS_CALLBACK_PROC)(PVOID, DWORD, PVOID);
            TLS_CALLBACK_PROC *callbacks = reinterpret_cast<TLS_CALLBACK_PROC *>(tlsData->AddressOfCallBacks);
#endif
            
            for (size_t i = 0; callbacks[i] != nullptr; ++i) {
                try {
                    callbacks[i](_module, DLL_PROCESS_ATTACH, nullptr);
                }
                catch (...) {
                    Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("TLS callback {} threw an exception", i);
                }
            }
        }
    }

    bool ExecutableLoader::LoadIntoModule(HMODULE module) {
        _module = module;

        const auto header = (IMAGE_DOS_HEADER *)_origBinary;

        if (header->e_magic != IMAGE_DOS_SIGNATURE) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid DOS signature: 0x{:X}", header->e_magic);
            return false;
        }

        const auto ntHeader = (IMAGE_NT_HEADERS *)(_origBinary + header->e_lfanew);
        
        // Validate PE headers first
        if (!ValidatePEHeaders(ntHeader)) {
            return false;
        }
        
        // Set the image size for bounds checking
        _imageSize = ntHeader->OptionalHeader.SizeOfImage;

        const auto sourceHeader          = (IMAGE_DOS_HEADER *)module;
        IMAGE_NT_HEADERS *sourceNtHeader = GetTargetRVA<IMAGE_NT_HEADERS>(sourceHeader->e_lfanew);

        const auto origCheckSum  = sourceNtHeader->OptionalHeader.CheckSum;
        const auto origTimeStamp = sourceNtHeader->FileHeader.TimeDateStamp;
        const auto origDebugDir  = sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];

        _entryPoint = GetTargetRVA<void>(ntHeader->OptionalHeader.AddressOfEntryPoint);
        if (!_entryPoint) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid entry point RVA: 0x{:X}", ntHeader->OptionalHeader.AddressOfEntryPoint);
            return false;
        }

        DWORD oldProtect1;
        if (!VirtualProtect(sourceNtHeader, 0x1000, PAGE_EXECUTE_READWRITE, &oldProtect1)) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to change protection for NT headers");
            return false;
        }
        
        sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC] = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        
        if (!ApplyRelocations()) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to apply relocations");
            CleanupOnError();
            return false;
        }

        if (!LoadSections(ntHeader)) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to load sections");
            CleanupOnError();
            return false;
        }
        
        if (!LoadImports(ntHeader)) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to load imports");
            CleanupOnError();
            return false;
        }
        
        if (!LoadDelayImports(ntHeader)) {
            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to load delay imports");
            CleanupOnError();
            return false;
        }

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
            auto tlsBase = (LPVOID *)__readgsqword(0x58);
#endif

            if (sourceTls && sourceTls->StartAddressOfRawData && sourceTls->EndAddressOfRawData > sourceTls->StartAddressOfRawData && tlsInit != nullptr && tlsBase != nullptr && tlsIndex < TLS_MINIMUM_AVAILABLE) {
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
        
        // Execute TLS callbacks
        ExecuteTLSCallbacks(const_cast<IMAGE_NT_HEADERS*>(ntHeader));
        
        return true;
    }

    bool ExecutableLoader::ApplyRelocations() {
        const auto dosHeader = reinterpret_cast<IMAGE_DOS_HEADER *>(_module);

        const IMAGE_NT_HEADERS *ntHeader = GetTargetRVA<IMAGE_NT_HEADERS>(dosHeader->e_lfanew);

        const IMAGE_DATA_DIRECTORY *relocationDirectory = &ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

        IMAGE_BASE_RELOCATION *relocation = GetTargetRVA<IMAGE_BASE_RELOCATION>(relocationDirectory->VirtualAddress);
        const auto endRelocation          = reinterpret_cast<IMAGE_BASE_RELOCATION *>((char *)relocation + relocationDirectory->Size);

        const intptr_t relocOffset = reinterpret_cast<intptr_t>(_module) - reinterpret_cast<intptr_t>(GetModuleHandle(NULL));

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
                if (!addr) {
                    Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Invalid relocation target RVA: 0x{:X}", rva);
                    return false;
                }
                
                DWORD oldProtect;
                SIZE_T relocSize = 4; // Default for most types
                
                switch (type) {
                    case IMAGE_REL_BASED_ABSOLUTE:
                        // No relocation required
                        continue;
                        
                    case IMAGE_REL_BASED_HIGH:
                        relocSize = 2;
                        if (!VirtualProtect(addr, relocSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to change protection for HIGH relocation at 0x{:X}", rva);
                            return false;
                        }
                        *reinterpret_cast<uint16_t *>(addr) += static_cast<uint16_t>((relocOffset >> 16) & 0xFFFF);
                        break;
                        
                    case IMAGE_REL_BASED_LOW:
                        relocSize = 2;
                        if (!VirtualProtect(addr, relocSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to change protection for LOW relocation at 0x{:X}", rva);
                            return false;
                        }
                        *reinterpret_cast<uint16_t *>(addr) += static_cast<uint16_t>(relocOffset & 0xFFFF);
                        break;
                        
                    case IMAGE_REL_BASED_HIGHLOW:
                        if (!VirtualProtect(addr, relocSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to change protection for HIGHLOW relocation at 0x{:X}", rva);
                            return false;
                        }
                        *reinterpret_cast<int32_t *>(addr) += static_cast<int32_t>(relocOffset);
                        break;
                        
                    case IMAGE_REL_BASED_DIR64:
                        relocSize = 8;
                        if (!VirtualProtect(addr, relocSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                            Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to change protection for DIR64 relocation at 0x{:X}", rva);
                            return false;
                        }
                        *reinterpret_cast<int64_t *>(addr) += relocOffset;
                        break;
                        
                    default:
                        Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Unsupported relocation type {} at RVA 0x{:X}", type, rva);
                        return false;
                }

                if (!VirtualProtect(addr, relocSize, oldProtect, &oldProtect)) {
                    Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER)->error("Failed to restore protection for relocation at 0x{:X}", rva);
                    return false;
                }
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
