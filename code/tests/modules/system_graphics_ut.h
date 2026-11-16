/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "launcher/system_graphics.h"

MODULE(system_graphics, {
    using Framework::Launcher::SystemGraphicsLibraryName;

    IT("sends a graphics library to System32 whatever folder the load names", {
        EQUALS(SystemGraphicsLibraryName(L"dxgi.dll").value_or(L"") == L"dxgi.dll", true);
        EQUALS(SystemGraphicsLibraryName(L"D:\\Games\\Game\\Binaries\\Win64\\DXGI.dll").value_or(L"") == L"dxgi.dll", true);
        EQUALS(SystemGraphicsLibraryName(L"../../../Game/Binaries/Win64/d3d12.dll").value_or(L"") == L"d3d12.dll", true);
        EQUALS(SystemGraphicsLibraryName(L"d3d11").value_or(L"") == L"d3d11.dll", true);
    });

    IT("leaves every other library to the caller", {
        EQUALS(SystemGraphicsLibraryName(L"dxgi.dll.bak").has_value(), false);
        EQUALS(SystemGraphicsLibraryName(L"C:\\dxgi.dll\\nvngx_dlss.dll").has_value(), false);
        EQUALS(SystemGraphicsLibraryName(L"mydxgi.dll").has_value(), false);
        EQUALS(SystemGraphicsLibraryName(L"d3d12core.dll").has_value(), false);
        EQUALS(SystemGraphicsLibraryName(L"").has_value(), false);
    });
});
