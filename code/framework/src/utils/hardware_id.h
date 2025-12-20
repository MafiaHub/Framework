/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

#ifdef _WIN32
#include "safe_win32.h"
#endif

namespace Framework::Utils {
    /**
     * Retrieves a unique hardware identifier for the current machine.
     * On Windows, this returns the hard drive serial number.
     * On other platforms, returns an empty string.
     *
     * @return Hardware ID string, or empty string if unavailable
     */
    inline std::string GetHardwareId() {
#ifdef _WIN32
        char volumeName[MAX_PATH + 1] = {0};
        char fileSystemName[MAX_PATH + 1] = {0};
        DWORD serialNumber = 0;
        DWORD maxComponentLen = 0;
        DWORD fileSystemFlags = 0;

        // Get the serial number of the system drive (C:)
        if (GetVolumeInformationA(
                "C:\\",
                volumeName,
                sizeof(volumeName),
                &serialNumber,
                &maxComponentLen,
                &fileSystemFlags,
                fileSystemName,
                sizeof(fileSystemName))) {
            return std::to_string(serialNumber);
        }
        return "";
#else
        return "";
#endif
    }
} // namespace Framework::Utils
