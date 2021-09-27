#include "project.h"

#include <Windows.h>

namespace Framework::Launcher {
    using PreInitFunction  = void (*)(LPVOID);
    using InitFunction     = void (*)(LPVOID);
    using PostInitFunction = void (*)(LPVOID);

    bool Project::Init() {
        _appHandle = LoadLibraryA(_handleName.c_str());
        if (_appHandle) {
            return true;
        }

        MessageBox(nullptr, L"Failed to acquire the destination DLL pointer", L"Error", MB_ICONERROR);
        return false;
    }

    bool Project::DoInnerPreInit() {
        if (!_appHandle) {
            MessageBox(nullptr, L"Failed to do inner pre init due to invalid app handle", L"Error", MB_ICONERROR);
            return false;
        }

        auto preInitFn = (PreInitFunction)(GetProcAddress((HMODULE)_appHandle, "PreInit"));
        if (!preInitFn) {
            return false;
        }

        // TODO: call with base address
        return true;
    }

    bool Project::DoInnerInit() {
        if (!_appHandle) {
            MessageBox(nullptr, L"Failed to do inner init due to invalid app handle", L"Error", MB_ICONERROR);
            return false;
        }

        auto initFn = (InitFunction)(GetProcAddress((HMODULE)_appHandle, "Init"));
        if (!initFn) {
            return false;
        }

        // TODO: call with base address
        return true;
    }

    bool Project::DoInnerPostInit() {
        if (!_appHandle) {
            MessageBox(nullptr, L"Failed to do inner init due to invalid app handle", L"Error", MB_ICONERROR);
            return false;
        }

        auto postInitFn = (PostInitFunction)(GetProcAddress((HMODULE)_appHandle, "PostInit"));
        if (!postInitFn) {
            return false;
        }

        // TODO: call with base address
        return true;
    }
} // namespace Framework::Launcher
