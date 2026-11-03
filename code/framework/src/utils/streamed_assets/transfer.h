/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Utils::StreamedAssets {
    // What the server and the client agree on about a file's transfer, beside the file itself.
    //
    // A chunk list names a file's content in kChunkSize pieces, one SHA-256 per piece, so a client
    // that holds an older version of the file fetches only the pieces that changed, and fetches
    // those in parallel. The list is served beside the file as <name>.chunks: one lowercase hex
    // digest per line, in order, the last chunk being whatever is left of the file.

    inline constexpr std::uint64_t kChunkSize = 4ull * 1024 * 1024;

    inline std::string ChunkListFileName(std::string_view fileName) {
        return std::string(fileName) + ".chunks";
    }

    inline std::uint64_t ChunkCount(std::uint64_t size) {
        return (size + kChunkSize - 1) / kChunkSize;
    }

    inline std::uint64_t ChunkLength(std::uint64_t size, std::uint64_t index) {
        const std::uint64_t offset = index * kChunkSize;
        return offset >= size ? 0 : std::min(kChunkSize, size - offset);
    }

    // Hashes |path| chunk by chunk; empty when the file cannot be read.
    std::vector<std::string> HashFileChunks(const std::string &path);

    std::string FormatChunkList(const std::vector<std::string> &chunks);

    // Accepts only a list that names exactly a file of |size| bytes in well-formed digests.
    bool ParseChunkList(std::string_view text, std::uint64_t size, std::vector<std::string> &chunks);

    // Where the server hands out download URLs: one or more http(s) origins separated by whitespace
    // or commas, tried in order. Empty entries are dropped; what is not an http(s) URL is kept for
    // the caller to refuse.
    std::vector<std::string> SplitUrlList(std::string_view text);

    // A note beside a verified cache file (<path>.verified) with the size and write time it had when
    // its SHA-256 was last checked: a later check trusts the file as long as both still match.
    inline std::string VerifiedNotePath(std::string_view path) {
        return std::string(path) + ".verified";
    }

    // True when the note says |path| was verified to be |sha256| at its current size and write time.
    bool IsVerified(const std::string &path, std::string_view sha256);
    void WriteVerifiedNote(const std::string &path, std::string_view sha256);
    void RemoveVerifiedNote(const std::string &path);
} // namespace Framework::Utils::StreamedAssets
