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

    std::string GetAbsolutePathA(std::string_view relative) {
#ifdef WIN32
        static char executable_path[MAX_PATH] = {'\0'};

        if (executable_path[0] == '\0') {
            char buf[MAX_PATH];
            GetModuleFileNameA(nullptr, buf, MAX_PATH);
            _splitpath(buf, &executable_path[0], &executable_path[_MAX_DRIVE - 1], nullptr, nullptr);
        }

        char buf[MAX_PATH];
        strcpy(buf, executable_path);
        std::string relStr(relative);
        strcat(buf, relStr.c_str());

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
        std::string relStr(relative);
        strcat(combined_path, relStr.c_str());
        
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

    std::wstring GetFileExtensionW(const std::wstring &path) {
        if (path.empty()) {
            return std::wstring();
        }

        size_t pos = path.find_last_of(L'.');
        if (pos == std::wstring::npos || pos == 0 || pos == path.length() - 1) {
            return std::wstring(); // No extension found or only dot at the beginning/end
        }

        // Check if the last dot is actually part of a directory/filename
        size_t lastSlash = path.find_last_of(L"/\\");
        if (lastSlash != std::wstring::npos && lastSlash > pos) {
            return std::wstring(); // Dot is in a directory component
        }

        return path.substr(pos); // Include the dot in the extension
    }

    std::string GetFileExtensionA(std::string_view path) {
        if (path.empty()) {
            return std::string();
        }

        size_t pos = path.find_last_of('.');
        if (pos == std::string_view::npos || pos == 0 || pos == path.length() - 1) {
            return std::string(); // No extension found or only dot at the beginning/end
        }

        // Check if the last dot is actually part of a directory/filename
        size_t lastSlash = path.find_last_of("/\\");
        if (lastSlash != std::string_view::npos && lastSlash > pos) {
            return std::string(); // Dot is in a directory component
        }

        return std::string(path.substr(pos)); // Include the dot in the extension
    }

    std::filesystem::path ResolvePathUnderRoot(const std::filesystem::path &root, std::string urlPath) {
        if (urlPath.empty() || urlPath == "/") {
            urlPath = "/index.html";
        }
        if (urlPath.front() == '/') {
            urlPath.erase(0, 1);
        }

        std::error_code ec;
        const auto file = std::filesystem::weakly_canonical(root / urlPath, ec);
        if (ec) {
            return {};
        }
        const auto rootCanonical = std::filesystem::weakly_canonical(root, ec);
        if (ec) {
            return {};
        }

        // lexically_relative is empty when the paths have different roots
        // (drive injection) and ".."-prefixed when the input escaped upward
        const auto relative = file.lexically_relative(rootCanonical);
        if (relative.empty() || *relative.begin() == "..") {
            return {};
        }
        return file;
    }
} // namespace Framework::Utils
