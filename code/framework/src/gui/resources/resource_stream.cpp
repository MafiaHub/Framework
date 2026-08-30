/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_stream.h"

#include <algorithm>
#include <cstring>

namespace Framework::GUI::Resources {
    std::int64_t MemoryStream::Read(void *out, std::size_t bytesToRead) {
        const std::size_t remaining = _data.size() - _offset;
        const std::size_t count     = std::min(remaining, bytesToRead);
        if (count > 0) {
            std::memcpy(out, _data.data() + _offset, count);
            _offset += count;
        }
        return static_cast<std::int64_t>(count);
    }

    std::int64_t MemoryStream::Skip(std::int64_t bytesToSkip) {
        if (bytesToSkip < 0) {
            return -1;
        }
        const std::uint64_t remaining = _data.size() - _offset;
        const std::uint64_t count     = std::min(remaining, static_cast<std::uint64_t>(bytesToSkip));
        _offset += static_cast<std::size_t>(count);
        return static_cast<std::int64_t>(count);
    }

    std::int64_t FileStream::Read(void *out, std::size_t bytesToRead) {
        if (!_file.is_open()) {
            return -1;
        }

        _file.read(static_cast<char *>(out), static_cast<std::streamsize>(bytesToRead));
        const std::streamsize count = _file.gcount();

        // eof alongside a short read is the normal end of a body; only a bad
        // bit that is not eof means the read itself failed.
        if (_file.bad() || (_file.fail() && !_file.eof())) {
            return -1;
        }
        if (_file.eof()) {
            _file.clear();
        }
        return static_cast<std::int64_t>(count);
    }

    std::int64_t FileStream::Skip(std::int64_t bytesToSkip) {
        if (!_file.is_open() || bytesToSkip < 0) {
            return -1;
        }

        // Seeking past the end of an input stream succeeds and only fails on the
        // next read, so the distance is clamped here instead. Otherwise a Range
        // request would be told bytes were skipped that were never there.
        const std::streampos current = _file.tellg();
        _file.seekg(0, std::ios::end);
        const std::streampos end = _file.tellg();
        if (_file.fail()) {
            _file.clear();
            return -1;
        }

        const std::int64_t remaining = static_cast<std::int64_t>(end - current);
        const std::int64_t skipped   = std::min(bytesToSkip, remaining);
        _file.seekg(current + static_cast<std::streamoff>(skipped));
        if (_file.fail()) {
            _file.clear();
            return -1;
        }
        return skipped;
    }
} // namespace Framework::GUI::Resources
