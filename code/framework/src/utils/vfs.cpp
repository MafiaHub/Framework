/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "vfs.h"

#include "crypto.h"

#include <logging/logger.h>

#include <physfs.h>

#include <algorithm>
#include <fstream>
#include <memory>

namespace Framework::Utils {
    namespace {
        // Package contents are server-supplied; bound what one entry can allocate.
        constexpr PHYSFS_sint64 kMaxReadSize = 64ll * 1024 * 1024;

        std::string LastError() {
            const PHYSFS_ErrorCode code = PHYSFS_getLastErrorCode();
            const char *text            = PHYSFS_getErrorByCode(code);
            return text ? text : "unknown PhysicsFS error";
        }

    } // namespace

    Vfs &Vfs::Get() {
        static Vfs instance;
        return instance;
    }

    bool Vfs::Init(const char *argv0) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_initialized) {
            return true;
        }
        if (PHYSFS_init(argv0) == 0) {
            Logging::GetLogger(FRAMEWORK_INNER_UTILS)->error("PhysicsFS init failed: {}", LastError());
            return false;
        }
        PHYSFS_permitSymbolicLinks(0);
        _initialized = true;
        return true;
    }

    void Vfs::Shutdown() {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized) {
            return;
        }
        // Unmounts everything, so the buffers are only free to wipe afterwards.
        PHYSFS_deinit();
        for (auto &buffer : _buffers) {
            Crypto::SecureZero(buffer.second->data(), buffer.second->size());
        }
        _buffers.clear();
        _mounts.clear();
        _initialized = false;
    }

    bool Vfs::MountMemory(std::string bytes, const std::string &id, const std::string &mountPoint, std::string &outError) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized) {
            outError = "virtual file system is not initialized";
            return false;
        }

        if (_buffers.contains(id)) {
            outError = "a package is already mounted as '" + id + "'";
            return false;
        }

        // Owned here rather than by PhysicsFS: its deleter takes no length, so it cannot wipe.
        auto owned = std::make_unique<std::string>(std::move(bytes));
        if (PHYSFS_mountMemory(owned->data(), owned->size(), nullptr, id.c_str(), mountPoint.c_str(), 0) == 0) {
            outError = LastError();
            Crypto::SecureZero(owned->data(), owned->size());
            return false;
        }

        _buffers.emplace(id, std::move(owned));
        if (std::find(_mounts.begin(), _mounts.end(), id) == _mounts.end()) {
            _mounts.push_back(id);
        }
        return true;
    }

    bool Vfs::Unmount(const std::string &id) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized) {
            return false;
        }
        const int result = PHYSFS_unmount(id.c_str());
        _mounts.erase(std::remove(_mounts.begin(), _mounts.end(), id), _mounts.end());

        if (auto buffer = _buffers.find(id); buffer != _buffers.end()) {
            Crypto::SecureZero(buffer->second->data(), buffer->second->size());
            _buffers.erase(buffer);
        }
        return result != 0;
    }

    bool Vfs::Contains(const std::string &vpath) const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized) {
            return false;
        }
        PHYSFS_Stat stat {};
        if (PHYSFS_stat(vpath.c_str(), &stat) == 0) {
            return false;
        }
        return stat.filetype == PHYSFS_FILETYPE_REGULAR;
    }

    bool Vfs::Read(const std::string &path, std::string &outContents) const {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_initialized) {
                PHYSFS_File *handle = PHYSFS_openRead(path.c_str());
                if (handle) {
                    const PHYSFS_sint64 length = PHYSFS_fileLength(handle);
                    if (length < 0 || length > kMaxReadSize) {
                        PHYSFS_close(handle);
                        Logging::GetLogger(FRAMEWORK_INNER_UTILS)->warn("Refusing oversized virtual file '{}' ({} bytes)", path, length);
                        return false;
                    }
                    outContents.resize(static_cast<size_t>(length));
                    const PHYSFS_sint64 read = length > 0 ? PHYSFS_readBytes(handle, outContents.data(), static_cast<PHYSFS_uint64>(length)) : 0;
                    PHYSFS_close(handle);
                    if (read != length) {
                        outContents.clear();
                        return false;
                    }
                    return true;
                }
            }
        }

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }
        const auto size = file.tellg();
        if (size < 0) {
            return false;
        }
        file.seekg(0, std::ios::beg);
        outContents.resize(static_cast<size_t>(size));
        if (size > 0) {
            file.read(outContents.data(), size);
            if (file.fail() || file.gcount() != size) {
                outContents.clear();
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> Vfs::Enumerate(const std::string &vpath) const {
        std::vector<std::string> names;
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized) {
            return names;
        }
        char **list = PHYSFS_enumerateFiles(vpath.c_str());
        if (!list) {
            return names;
        }
        for (char **entry = list; *entry != nullptr; ++entry) {
            names.emplace_back(*entry);
        }
        PHYSFS_freeList(list);
        return names;
    }

    std::vector<std::string> Vfs::EnumerateDirectories(const std::string &vpath) const {
        std::vector<std::string> dirs;
        for (const auto &name : Enumerate(vpath)) {
            const std::string child = vpath + "/" + name;
            std::lock_guard<std::mutex> lock(_mutex);
            PHYSFS_Stat stat {};
            if (PHYSFS_stat(child.c_str(), &stat) != 0 && stat.filetype == PHYSFS_FILETYPE_DIRECTORY) {
                dirs.push_back(name);
            }
        }
        return dirs;
    }

    std::string Vfs::ResourcePath(const std::string &resourceName) {
        return std::string(kResourceMountRoot) + "/" + resourceName;
    }

    bool Vfs::IsVirtualPath(const std::string &path) {
        const std::string root = kResourceMountRoot;
        if (path.size() < root.size() || path.compare(0, root.size(), root) != 0) {
            return false;
        }
        return path.size() == root.size() || path[root.size()] == '/';
    }

    std::string Vfs::NormalizeVirtual(const std::string &path) {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= path.size()) {
            size_t end = path.size();
            for (size_t i = start; i < path.size(); ++i) {
                if (path[i] == '/' || path[i] == '\\') {
                    end = i;
                    break;
                }
            }
            const std::string part = path.substr(start, end - start);
            if (part == "..") {
                if (parts.empty()) {
                    return {}; // escapes the virtual root
                }
                parts.pop_back();
            }
            else if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
            if (end == path.size()) {
                break;
            }
            start = end + 1;
        }

        std::string out;
        for (const auto &part : parts) {
            out.push_back('/');
            out += part;
        }
        return out.empty() ? "/" : out;
    }
} // namespace Framework::Utils
