/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Framework::Utils {
    // Adapter over PhysicsFS (vendors/physfs). Owns the process-wide init lifetime and a mutex
    // around mount changes, which PhysicsFS does not guard itself.
    // See docs/research/client_resource_protection.md.
    class Vfs final {
      public:
        static Vfs &Get();

        // |argv0| may be null. Idempotent.
        bool Init(const char *argv0);
        void Shutdown();

        // Mounts a ZIP held in memory. Takes ownership of |bytes|, which PhysicsFS reads from for
        // the life of the mount. |id| must be unique and is what Unmount() takes.
        bool MountMemory(std::string bytes, const std::string &id, const std::string &mountPoint, std::string &outError);

        bool Unmount(const std::string &id);

        // Virtual tree only; a real file on disk is not a match.
        bool Contains(const std::string &vpath) const;

        // Virtual tree first, then the real filesystem.
        bool Read(const std::string &path, std::string &outContents) const;

        // Immediate child directories of a mounted virtual path.
        std::vector<std::string> EnumerateDirectories(const std::string &vpath) const;

        static constexpr const char *kResourceMountRoot = "/resources";

        static std::string ResourcePath(const std::string &resourceName);

        // Virtual paths must never reach std::filesystem: absolute() and weakly_canonical()
        // prefix them with the current drive on Windows, which leaves the mount unreachable.
        // Every caller that does path maths branches on this.
        static bool IsVirtualPath(const std::string &path);

        // Lexical join; empty when the result escapes the virtual root.
        static std::string NormalizeVirtual(const std::string &path);

      private:
        Vfs()  = default;
        ~Vfs() = default;

        Vfs(const Vfs &)            = delete;
        Vfs &operator=(const Vfs &) = delete;

        std::vector<std::string> Enumerate(const std::string &vpath) const;

        mutable std::mutex _mutex;
        bool _initialized = false;
        std::vector<std::string> _mounts;
        // Read from directly by PhysicsFS, so wiped only after unmount.
        std::unordered_map<std::string, std::unique_ptr<std::string>> _buffers;
    };
} // namespace Framework::Utils
