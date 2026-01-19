/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "project_config.h"
#include "utils/config.h"
#include "utils/minidump.h"
#include <external/steam/wrapper.h>

#include <Windows.h>

#include <function2.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Framework::Launcher {
    class Project {
      public:
        using FunctionResolverProc = fu2::function<LPVOID(HMODULE, const char *) const>;
        using LibraryLoaderProc    = fu2::function<HMODULE(const char *) const>;
        using PreLaunchProc        = fu2::function<void() const>;

      private:
        ProjectConfiguration _config;
        std::unique_ptr<Utils::Config> _fileConfig;
        std::wstring _gamePath;
        std::unique_ptr<External::Steam::Wrapper> _steamWrapper;
        std::unique_ptr<Utils::MiniDump> _minidump;

        LibraryLoaderProc _libraryLoader;
        FunctionResolverProc _functionResolver;
        PreLaunchProc _preLaunchFunctor;

      public:
        explicit Project(ProjectConfiguration &);
        ~Project() = default;

        bool Launch();

        inline void SetLibraryLoader(LibraryLoaderProc loader) {
            _libraryLoader = std::move(loader);
        }

        inline void SetFunctionResolver(FunctionResolverProc functionResolver) {
            _functionResolver = std::move(functionResolver);
        }

        inline void SetPreLaunchFunctor(PreLaunchProc preLaunchFunctor) {
            _preLaunchFunctor = std::move(preLaunchFunctor);
        }

        ProjectConfiguration &GetConfig() {
            return _config;
        }

      private:
        static bool EnsureFilesExist(const std::vector<std::string> &);
        static bool EnsureAtLeastOneFileExists(const std::vector<std::string> &);
        bool EnsureGameExecutableIsCompatible(uint32_t);
        uint32_t GetGameVersion() const;

        bool RunInnerSteamChecks();
        bool RunInnerClassicChecks();

        bool LoadJSONConfig();
        void SaveJSONConfig() const;

        static void InvokeEntryPoint(void (*entryPoint)());

        void AllocateDeveloperConsole() const;

        bool RunWithPELoading();
        bool RunWithDLLInjection();
    };
} // namespace Framework::Launcher
