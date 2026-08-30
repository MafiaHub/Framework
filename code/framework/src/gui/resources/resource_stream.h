/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace Framework::GUI::Resources {
    // A pull-based byte source for one response body.
    //
    // Read() and Skip() are driven from a CEF file-thread task, never from the
    // IO thread, so an implementation is free to block on real disk access.
    // Both are called in sequence for a given stream, so no internal locking is
    // needed, but the calls may arrive on different threads.
    class ResourceStream {
      public:
        virtual ~ResourceStream() = default;

        // Copies at most |bytesToRead| bytes into |out|. Returns the count
        // copied; 0 means end of stream and a negative value means failure.
        virtual std::int64_t Read(void *out, std::size_t bytesToRead) = 0;

        // Discards at most |bytesToSkip| bytes, same return convention as Read().
        virtual std::int64_t Skip(std::int64_t bytesToSkip) = 0;
    };

    // Serves a body that is already in memory: generated pages, error documents,
    // and anything a MemoryProvider holds.
    class MemoryStream final: public ResourceStream {
      private:
        std::string _data;
        std::size_t _offset = 0;

      public:
        explicit MemoryStream(std::string data): _data(std::move(data)) {}

        std::int64_t Read(void *out, std::size_t bytesToRead) override;
        std::int64_t Skip(std::int64_t bytesToSkip) override;
    };

    // Serves a body straight off disk. The file handle is held open for the
    // lifetime of the response, which is what keeps Read() a plain sequential
    // read rather than a re-open per chunk.
    class FileStream final: public ResourceStream {
      private:
        std::ifstream _file;

      public:
        explicit FileStream(const std::filesystem::path &path): _file(path, std::ios::binary) {}

        bool IsOpen() const {
            return _file.is_open();
        }

        std::int64_t Read(void *out, std::size_t bytesToRead) override;
        std::int64_t Skip(std::int64_t bytesToSkip) override;
    };
} // namespace Framework::GUI::Resources
