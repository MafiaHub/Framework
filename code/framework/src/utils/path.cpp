/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "path.h"
#include "safe_win32.h"

#ifdef WIN32
#include <Shlwapi.h>
#include <ShlObj.h>
#else
#include <climits>
#include <cstdlib>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <limits.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
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
        // On Linux/Mac, use the ASCII version and convert
        std::string relativeA(relative.begin(), relative.end());
        std::string resultA = GetAbsolutePathA(relativeA);
        return std::wstring(resultA.begin(), resultA.end());
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
        static char executable_path[PATH_MAX] = {'\0'};

        if (executable_path[0] == '\0') {
            char buf[PATH_MAX];
            
            #if defined(__APPLE__) || defined(__linux__)
            ssize_t count = readlink("/proc/self/exe", buf, PATH_MAX);
            if (count == -1) {
                // On macOS, try an alternative approach
                #if defined(__APPLE__)
                uint32_t bufsize = PATH_MAX;
                if (_NSGetExecutablePath(buf, &bufsize) != 0) {
                    return "";
                }
                // Get the real path if it's a symbolic link
                char resolvedPath[PATH_MAX];
                if (realpath(buf, resolvedPath) != nullptr) {
                    strcpy(buf, resolvedPath);
                }
                #else
                return "";
                #endif
            }
            else {
                buf[count] = '\0';
            }
            #endif
            
            // Get directory part of the path
            char* dir = dirname(buf);
            strcpy(executable_path, dir);
            strcat(executable_path, "/");
        }

        // Combine executable path with relative path
        char combined_path[PATH_MAX];
        strcpy(combined_path, executable_path);
        strcat(combined_path, relative.c_str());
        
        // Canonicalize path (resolve "..", ".", etc.)
        char final_path[PATH_MAX];
        if (realpath(combined_path, final_path) != nullptr) {
            return final_path;
        }
        
        return combined_path; // If realpath fails, return the combined path
#endif
    }

    std::wstring GetAppDataPathW() {
#ifdef WIN32
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
            return std::wstring(path);
        }
        return std::wstring();
#else
        // On non-Windows platforms, return an empty string or implement equivalent
        return std::wstring();
#endif
    }

    std::string GetAppDataPathA() {
#ifdef WIN32
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
            return std::string(path);
        }
        return std::string();
#else
        // On non-Windows platforms, typically use $HOME/.config or $XDG_CONFIG_HOME
        const char* configDir = getenv("XDG_CONFIG_HOME");
        if (configDir) {
            return std::string(configDir);
        }
        
        const char* homeDir = getenv("HOME");
        if (homeDir) {
            return std::string(homeDir) + "/.config";
        }
        
        return std::string();
#endif
    }
} // namespace Framework::Utils
