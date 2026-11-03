/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "transfer.h"

#include "policy.h"

#include <utils/crypto.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Framework::Utils::StreamedAssets {
    namespace {
        std::int64_t WriteTimeTicks(const std::string &path, std::error_code &code) {
            const auto time = std::filesystem::last_write_time(path, code);
            return code ? 0 : static_cast<std::int64_t>(time.time_since_epoch().count());
        }
    } // namespace

    std::vector<std::string> HashFileChunks(const std::string &path) {
        std::vector<std::string> chunks;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return chunks;
        }
        std::vector<char> buffer(static_cast<size_t>(kChunkSize));
        while (file.good()) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = file.gcount();
            if (read > 0) {
                chunks.push_back(Crypto::Sha256Hex(buffer.data(), static_cast<size_t>(read)));
            }
        }
        return chunks;
    }

    std::string FormatChunkList(const std::vector<std::string> &chunks) {
        std::string text;
        for (const auto &chunk : chunks) {
            text += chunk;
            text += '\n';
        }
        return text;
    }

    bool ParseChunkList(std::string_view text, std::uint64_t size, std::vector<std::string> &chunks) {
        std::vector<std::string> parsed;
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos) {
                end = text.size();
            }
            std::string_view line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (!line.empty()) {
                if (!IsSha256(line)) {
                    return false;
                }
                parsed.emplace_back(line);
            }
            start = end + 1;
        }
        if (parsed.size() != ChunkCount(size)) {
            return false;
        }
        chunks = std::move(parsed);
        return true;
    }

    std::vector<std::string> SplitUrlList(std::string_view text) {
        std::vector<std::string> urls;
        std::string current;
        const auto flush = [&urls, &current] {
            while (!current.empty() && current.back() == '/') {
                current.pop_back();
            }
            if (!current.empty()) {
                urls.push_back(current);
            }
            current.clear();
        };
        for (const char c : text) {
            if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';') {
                flush();
            }
            else {
                current += c;
            }
        }
        flush();
        return urls;
    }

    bool IsVerified(const std::string &path, std::string_view sha256) {
        std::ifstream note(VerifiedNotePath(path));
        if (!note.is_open()) {
            return false;
        }
        std::uint64_t size = 0;
        std::int64_t ticks = 0;
        std::string hash;
        note >> size >> ticks >> hash;
        if (!note || hash != sha256) {
            return false;
        }
        std::error_code code;
        const auto actualSize = std::filesystem::file_size(path, code);
        if (code || actualSize != size) {
            return false;
        }
        const std::int64_t actualTicks = WriteTimeTicks(path, code);
        return !code && actualTicks == ticks;
    }

    void WriteVerifiedNote(const std::string &path, std::string_view sha256) {
        std::error_code code;
        const auto size = std::filesystem::file_size(path, code);
        if (code) {
            return;
        }
        const std::int64_t ticks = WriteTimeTicks(path, code);
        if (code) {
            return;
        }
        std::ofstream note(VerifiedNotePath(path), std::ios::trunc);
        note << size << ' ' << ticks << ' ' << sha256 << '\n';
    }

    void RemoveVerifiedNote(const std::string &path) {
        std::error_code code;
        std::filesystem::remove(VerifiedNotePath(path), code);
    }
} // namespace Framework::Utils::StreamedAssets
