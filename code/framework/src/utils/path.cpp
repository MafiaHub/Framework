/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "safe_win32.h"
#include "path.h"

#ifdef WIN32
#include <Shlwapi.h>
#endif

namespace Framework::Utils {
    std::wstring GetAbsolutePathW(const std::wstring &relative) {
        #ifdef WIN32
        static wchar_t executable_path[MAX_PATH] = {'\0'};

        if (executable_path[0] == '\0') {
            wchar_t buf[MAX_PATH];
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            _wsplitpath(buf, &executable_path[0], &executable_path[_MAX_DRIVE - 1], nullptr, nullptr);
        }

        wchar_t buf[MAX_PATH];
        lstrcpyW(buf, executable_path);
        lstrcatW(buf, relative.c_str());

        wchar_t final_buf[MAX_PATH] = {'\0'};
        PathCanonicalizeW(final_buf, buf);
        return final_buf;
        #else
        return NULL;
        #endif
    }

    std::string GetAbsolutePathA(const std::string &relative) {
        #ifdef WIN32
        static char executable_path[MAX_PATH] = {'\0'};

        if (executable_path[0] == '\0') {
            char buf[MAX_PATH];
            GetModuleFileNameA(nullptr, buf, MAX_PATH);
            _splitpath(buf, &executable_path[0], &executable_path[_MAX_DRIVE - 1], nullptr, nullptr);
        }

        char buf[MAX_PATH];
        strcpy(buf, executable_path);
        strcat(buf, relative.c_str());

        char final_buf[MAX_PATH] = {'\0'};
        PathCanonicalizeA(final_buf, buf);
        return final_buf;
        #else
        return NULL;
        #endif
    }
}
