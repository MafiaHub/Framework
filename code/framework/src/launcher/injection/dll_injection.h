/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../project_config.h"

#include <Windows.h>

namespace Framework::Launcher::Injection {
    /**
     * Inject a library into an already opened process.
     *
     * @param hProcess Handle to the target process
     * @param szLibraryPath Full path to the library to inject
     * @return Result code indicating success or failure reason
     */
    DLLInjectionResults InjectLibraryIntoProcess(HANDLE hProcess, const wchar_t *szLibraryPath);

    /**
     * Inject a library into a process by its ID.
     *
     * @param dwProcessId Process ID of the target process
     * @param szLibraryPath Full path to the library to inject
     * @return Result code indicating success or failure reason
     */
    DLLInjectionResults InjectLibraryIntoProcess(DWORD dwProcessId, const wchar_t *szLibraryPath);

    /**
     * Convert an injection result code to a human-readable string.
     *
     * @param result The result code to convert
     * @return String description of the result
     */
    const char *InjectLibraryResultToString(DLLInjectionResults result);
} // namespace Framework::Launcher::Injection
