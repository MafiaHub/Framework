/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "package.h"

#include <appmodel.h>

#include <vector>

namespace Framework::External::MicrosoftStore {
    namespace {
        // Windows' own activator for running a full-trust exe in a desktop package's context. It is
        // not in the SDK headers; the declaration below matches the one the Appx PowerShell module
        // (Microsoft.Windows.Appx.PackageManager.Commands) marshals Invoke-CommandInDesktopPackage
        // through.
        constexpr CLSID CLSID_DesktopAppXActivator = {0x168eb462, 0x775f, 0x42ae, {0x91, 0x11, 0xd7, 0x14, 0xb2, 0x30, 0x6c, 0x2e}};
        constexpr IID IID_IDesktopAppXActivator    = {0xf158268a, 0xd5a5, 0x45ce, {0x99, 0xcf, 0x00, 0xd6, 0xc3, 0xf3, 0xfc, 0x0a}};

        // DESKTOPAPPXACTIVATEOPTIONS
        constexpr DWORD DAXAO_NONPACKAGED_EXE = 0x2;
        constexpr DWORD DAXAO_NO_ERROR_UI     = 0x8;

        struct IDesktopAppXActivator: IUnknown {
            virtual HRESULT STDMETHODCALLTYPE Activate(LPCWSTR applicationUserModelId, LPCWSTR packageRelativeExecutable, LPCWSTR arguments, HANDLE *processHandle) = 0;
            virtual HRESULT STDMETHODCALLTYPE ActivateWithOptions(LPCWSTR applicationUserModelId, LPCWSTR executable, LPCWSTR arguments, DWORD activationOptions, DWORD parentProcessId, HANDLE *processHandle) = 0;
        };
    } // namespace

    InstalledPackage FindInstalledPackage(const std::wstring &packageFamilyName) {
        UINT32 count = 0, bufferLength = 0;
        if (GetPackagesByPackageFamily(packageFamilyName.c_str(), &count, nullptr, &bufferLength, nullptr) != ERROR_INSUFFICIENT_BUFFER || count == 0) {
            return {};
        }

        std::vector<PWSTR> names(count);
        std::vector<wchar_t> buffer(bufferLength);
        if (GetPackagesByPackageFamily(packageFamilyName.c_str(), &count, names.data(), &bufferLength, buffer.data()) != ERROR_SUCCESS) {
            return {};
        }

        // A family is installed once per user and architecture, so the first one is the one
        for (UINT32 i = 0; i < count; ++i) {
            UINT32 pathLength = 0;
            if (GetPackagePathByFullName(names[i], &pathLength, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
                continue;
            }

            std::wstring path(pathLength, L'\0');
            if (GetPackagePathByFullName(names[i], &pathLength, path.data()) != ERROR_SUCCESS) {
                continue;
            }

            path.resize(wcslen(path.c_str()));
            return {names[i], path};
        }
        return {};
    }

    bool IsRunningInPackage(const std::wstring &packageFamilyName) {
        wchar_t familyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
        UINT32 length                                          = _countof(familyName);
        if (GetCurrentPackageFamilyName(&length, familyName) != ERROR_SUCCESS) {
            return false;
        }

        return _wcsicmp(familyName, packageFamilyName.c_str()) == 0;
    }

    HANDLE StartInPackage(const std::wstring &packageFamilyName, const std::wstring &appId, const std::wstring &executable, const std::wstring &arguments, HRESULT &error) {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        IDesktopAppXActivator *activator = nullptr;
        error = CoCreateInstance(CLSID_DesktopAppXActivator, nullptr, CLSCTX_INPROC_SERVER, IID_IDesktopAppXActivator, reinterpret_cast<void **>(&activator));

        HANDLE process = nullptr;
        if (SUCCEEDED(error)) {
            const std::wstring appUserModelId = packageFamilyName + L"!" + appId;
            error = activator->ActivateWithOptions(appUserModelId.c_str(), executable.c_str(), arguments.c_str(), DAXAO_NONPACKAGED_EXE | DAXAO_NO_ERROR_UI, GetCurrentProcessId(), &process);
            activator->Release();
        }

        if (SUCCEEDED(init)) {
            CoUninitialize();
        }
        return SUCCEEDED(error) ? process : nullptr;
    }
} // namespace Framework::External::MicrosoftStore
