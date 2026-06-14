/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "loader.h"

#if defined(_WIN32)
#    include <utils/safe_win32.h>
#else
#    include <dlfcn.h>
#endif

namespace Framework::Integrations::Server::Plugins {

#if defined(_WIN32)
    static std::string FormatWin32Error(DWORD code) {
        if (code == 0) return {};
        LPSTR  buf  = nullptr;
        DWORD  size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buf), 0, nullptr);
        std::string msg(buf, size);
        LocalFree(buf);
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
        return msg;
    }
#endif

    SharedLibrary::~SharedLibrary() {
        Close();
    }

    SharedLibrary::SharedLibrary(SharedLibrary &&other) noexcept: _handle(other._handle), _path(std::move(other._path)), _lastError(std::move(other._lastError)) {
        other._handle = nullptr;
    }

    SharedLibrary &SharedLibrary::operator=(SharedLibrary &&other) noexcept {
        if (this != &other) {
            Close();
            _handle       = other._handle;
            _path         = std::move(other._path);
            _lastError    = std::move(other._lastError);
            other._handle = nullptr;
        }
        return *this;
    }

    void SharedLibrary::Close() {
        if (!_handle) return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(_handle));
#else
        dlclose(_handle);
#endif
        _handle = nullptr;
    }

    bool SharedLibrary::Open(const std::string &path) {
        Close();
        _path = path;
        _lastError.clear();
#if defined(_WIN32)
        _handle = LoadLibraryA(path.c_str());
        if (!_handle) {
            _lastError = FormatWin32Error(GetLastError());
            return false;
        }
#else
        _handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!_handle) {
            const char *err = dlerror();
            _lastError      = err ? err : "unknown dlopen error";
            return false;
        }
#endif
        return true;
    }

    void *SharedLibrary::Symbol(const char *name) const {
        if (!_handle) return nullptr;
#if defined(_WIN32)
        return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(_handle), name));
#else
        /* Clear any prior error so the nullptr-vs-actually-null check works */
        dlerror();
        return dlsym(_handle, name);
#endif
    }

    std::string SharedLibrary::PlatformFilename(const std::string &entry) {
#if defined(_WIN32)
        return entry + ".dll";
#elif defined(__APPLE__)
        return "lib" + entry + ".dylib";
#else
        return "lib" + entry + ".so";
#endif
    }

} // namespace Framework::Integrations::Server::Plugins
