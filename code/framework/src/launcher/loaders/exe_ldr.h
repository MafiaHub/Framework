/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

/*
 * This file is based on ExecutableLoader from: https://github.com/citizenfx/fivem/blob/cbe56f78f86bebb68d7960a38c3cdc31c7d76790/code/client/launcher/ExecutableLoader.h
 * See:https://github.com/citizenfx/fivem/blob/master/code/LICENSE
 */

#include <cstdint>
#include <functional>
#include <vector>
#include <windows.h>
#include <winnt.h>

namespace Framework::Launcher::Loaders {
    class ExecutableLoader {
      public:
        using FunctionResolverProc = std::function<LPVOID(HMODULE, const char *)>;
        using LibraryLoaderProc    = std::function<HMODULE(const char *)>;

      private:
        const uint8_t *_origBinary;
        HMODULE _module;
        uintptr_t _loadLimit;

        void *_entryPoint;

        LibraryLoaderProc _libraryLoader;
        FunctionResolverProc _functionResolver;

        std::function<void(void **base, uint32_t *index)> _tlsInitializer;

      private:
        void LoadSection(IMAGE_SECTION_HEADER *section);
        void LoadSections(IMAGE_NT_HEADERS *ntHeader);

        bool ApplyRelocations();

#ifdef _M_AMD64
        void LoadExceptionTable(IMAGE_NT_HEADERS *ntHeader);
#endif

        void LoadImports(IMAGE_NT_HEADERS *ntHeader);

        HMODULE ResolveLibrary(const char *name);

        template <typename T>
        inline const T *GetRVA(uint32_t rva) {
            return (T *)(_origBinary + rva);
        }

        template <typename T>
        inline T *GetTargetRVA(uint32_t rva) {
            return (T *)((uint8_t *)_module + rva);
        }

      public:
        ExecutableLoader(const uint8_t *origBinary);

        inline void SetLoadLimit(uintptr_t loadLimit) {
            _loadLimit = loadLimit;
        }

        inline void SetLibraryLoader(LibraryLoaderProc loader) {
            _libraryLoader = loader;
        }

        inline void SetFunctionResolver(FunctionResolverProc functionResolver) {
            _functionResolver = functionResolver;
        }

        inline void SetTLSInitializer(const std::function<void(void **base, uint32_t *index)> &callback) {
            _tlsInitializer = callback;
        }

        inline void *GetEntryPoint() {
            return _entryPoint;
        }

        void LoadIntoModule(HMODULE module);
    };
} // namespace Framework::Launcher::Loaders
