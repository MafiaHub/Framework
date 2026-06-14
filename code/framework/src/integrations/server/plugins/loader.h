/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

namespace Framework::Integrations::Server::Plugins {

    /*
     * RAII wrapper around platform-specific shared-library loading.
     * Move-only; closes the handle on destruction.
     */
    class SharedLibrary final {
      public:
        SharedLibrary() = default;
        ~SharedLibrary();

        SharedLibrary(const SharedLibrary &)            = delete;
        SharedLibrary &operator=(const SharedLibrary &) = delete;

        SharedLibrary(SharedLibrary &&other) noexcept;
        SharedLibrary &operator=(SharedLibrary &&other) noexcept;

        /* Open the library at the given absolute path. Returns true on success;
         * on failure, GetLastError() describes the platform error. */
        bool Open(const std::string &path);

        /* Returns the address of an exported symbol or nullptr if missing. */
        void *Symbol(const char *name) const;

        bool IsOpen() const {
            return _handle != nullptr;
        }

        const std::string &GetPath() const {
            return _path;
        }
        const std::string &GetLastError() const {
            return _lastError;
        }

        /* Returns the platform-appropriate filename for a plugin "entry" stem
         * (e.g. "hello-plugin" -> "hello-plugin.dll" on Windows,
         * "libhello-plugin.so" on Linux, "libhello-plugin.dylib" on macOS). */
        static std::string PlatformFilename(const std::string &entry);

      private:
        void Close();

        void       *_handle = nullptr;
        std::string _path;
        std::string _lastError;
    };

} // namespace Framework::Integrations::Server::Plugins
