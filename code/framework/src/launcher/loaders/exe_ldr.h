/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

/*
 * This file is based on ExecutableLoader from:
 * https://github.com/citizenfx/fivem/blob/cbe56f78f86bebb68d7960a38c3cdc31c7d76790/code/client/launcher/ExecutableLoader.h
 * See:https://github.com/citizenfx/fivem/blob/master/code/LICENSE
 */

#include <Windows.h>
#include <cstdint>
#include <function2.hpp>
#include <vector>
#include <winnt.h>

namespace Framework::Launcher::Loaders {
    class ExecutableLoader final {
      public:
        using FunctionResolverProc = fu2::function<LPVOID(HMODULE, const char *) const>;
        using LibraryLoaderProc    = fu2::function<HMODULE(const char *) const>;

      private:
        const uint8_t *_origBinary;
        size_t _totalBinarySize;
        HMODULE _module {};
        uintptr_t _loadLimit;

        void *_entryPoint {};

        LibraryLoaderProc _libraryLoader;
        FunctionResolverProc _functionResolver;

        fu2::function<void(void **base, uint32_t *index)> _tlsInitializer;
        fu2::function<void(HMODULE module) const> _sectionsMapped;
        std::vector<std::tuple<void *, DWORD, DWORD>> _targetProtections;
        bool _useDirectTlsSlot0 = false;

      private:
        void LoadSection(IMAGE_SECTION_HEADER *section, DWORD sectionAlignment);
        void LoadSections(IMAGE_NT_HEADERS *ntHeader);

        bool ApplyRelocations();

#ifdef _M_AMD64
        void LoadExceptionTable(IMAGE_NT_HEADERS *ntHeader);
#endif

        void LoadImports(IMAGE_NT_HEADERS *ntHeader);
        void LoadDelayImports(IMAGE_NT_HEADERS *ntHeader);

        HMODULE ResolveLibrary(const char *name) const;

        template <typename T>
        inline const T *GetRVA(uint32_t rva) {
            return reinterpret_cast<const T *>(_origBinary + rva);
        }

        template <typename T>
        inline T *GetTargetRVA(uint32_t rva) {
            return reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(_module) + rva);
        }

      public:
        ExecutableLoader(const uint8_t *origBinary, size_t binarySize);

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

        // Runs once the sections are mapped and relocated, before imports are resolved. This is the
        // only point where an image can still be rewritten wholesale - see ImageSnapshot.
        inline void SetSectionsMappedCallback(fu2::function<void(HMODULE module) const> callback) {
            _sectionsMapped = std::move(callback);
        }

        inline void SetUseDirectTlsSlot0(bool useDirectSlot0) {
            _useDirectTlsSlot0 = useDirectSlot0;
        }

        inline void *GetEntryPoint() const {
            return _entryPoint;
        }

        void Protect() const;
        void LoadIntoModule(HMODULE module);
        void RunTLSCallbacks() const;
    };
} // namespace Framework::Launcher::Loaders
