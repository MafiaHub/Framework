/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

/*
 * This file is based on ExecutableLoader from: https://github.com/citizenfx/fivem/blob/cbe56f78f86bebb68d7960a38c3cdc31c7d76790/code/client/launcher/ExecutableLoader.h
 * See:https://github.com/citizenfx/fivem/blob/master/code/LICENSE
 */

#include <Windows.h>
#include <cstdint>
#include <function2.hpp>
#include <vector>
#include <winnt.h>

namespace Framework::Launcher::Loaders {
    class ExecutableLoader {
      public:
        using FunctionResolverProc = fu2::function<LPVOID(HMODULE, const char *) const>;
        using LibraryLoaderProc    = fu2::function<HMODULE(const char *) const>;

      private:
        const uint8_t *_origBinary;
        HMODULE _module {};
        uintptr_t _loadLimit;

        void *_entryPoint {};

        LibraryLoaderProc _libraryLoader;
        FunctionResolverProc _functionResolver;

        fu2::function<void(void **base, uint32_t *index)> _tlsInitializer;
        std::vector<std::tuple<void *, DWORD, DWORD>> _targetProtections;

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

        inline void SetTLSInitializer(const fu2::function<void(void **base, uint32_t *index)> &callback) {
            _tlsInitializer = callback;
        }

        inline void *GetEntryPoint() {
            return _entryPoint;
        }

        void Protect();
        void LoadIntoModule(HMODULE module);
    };
} // namespace Framework::Launcher::Loaders
