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
    // Byte source for one response body. Driven from a CEF file thread, never
    // the IO thread, so an implementation may block.
    class ResourceStream {
      public:
        virtual ~ResourceStream() = default;

        // Returns the count copied; 0 is end of stream, negative is failure.
        virtual std::int64_t Read(void *out, std::size_t bytesToRead) = 0;

        // Same return convention as Read().
        virtual std::int64_t Skip(std::int64_t bytesToSkip) = 0;
    };

    class MemoryStream final: public ResourceStream {
      private:
        std::string _data;
        std::size_t _offset = 0;

      public:
        explicit MemoryStream(std::string data): _data(std::move(data)) {}

        std::int64_t Read(void *out, std::size_t bytesToRead) override;
        std::int64_t Skip(std::int64_t bytesToSkip) override;
    };

    // Holds the file open for the life of the response, so Read() stays a plain
    // sequential read rather than a re-open per chunk.
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
