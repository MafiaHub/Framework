/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "policy.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Utils::StreamedAssets {
    // What the server and the client agree on about a file's transfer, beside the file itself.
    //
    // A chunk list names a file's content piece by piece, one SHA-256 each, so a client that holds an
    // older version of the file fetches only the pieces that changed, and fetches those in parallel.
    // A pak is cut at its entries, and a long entry every kChunkSize, so an edit moves the pieces
    // after it without changing them and the client finds them by hash in the copy it has; any
    // other file is cut every kChunkSize. The list is served beside the file as <name>.chunks: one
    // "<length> <sha256>" per line, in file order, the lengths adding up to the file.

    inline constexpr std::uint64_t kChunkSize = 4ull * 1024 * 1024;

    struct Chunk {
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
        std::string sha256;
    };

    inline std::string ChunkListFileName(std::string_view fileName) {
        return std::string(fileName) + ".chunks";
    }

    // The most chunks a file of |size| bytes is cut into: one per pak entry, the splits of the long
    // ones, the central directory. Bounds what a client reads of a list before it parses it.
    inline std::uint64_t MaxChunkCount(std::uint64_t size) {
        return kMaxPakEntries + (size + kChunkSize - 1) / kChunkSize + 1;
    }

    // Hashes |path| piece by piece along its cuts; empty when the file cannot be read or is empty.
    std::vector<Chunk> HashFileChunks(const std::string &path);

    std::string FormatChunkList(const std::vector<Chunk> &chunks);

    // Accepts only a list that names exactly a file of |size| bytes: well-formed digests, no piece
    // longer than kChunkSize, the lengths adding up to |size|.
    bool ParseChunkList(std::string_view text, std::uint64_t size, std::vector<Chunk> &chunks);

    // Where the server hands out download URLs: one or more http(s) origins separated by whitespace
    // or commas, tried in order. Empty entries are dropped; what is not an http(s) URL is kept for
    // the caller to refuse.
    std::vector<std::string> SplitUrlList(std::string_view text);

    /** At most this many mirrors of a server's list are used; the rest are ignored. */
    inline constexpr std::size_t kMaxDownloadMirrors = 8;

    /**
     * Whether a download mirror a server announced may be requested. The server chooses the list, so
     * it must not be able to aim a player's client at the player's own machine or network: a mirror
     * is accepted when it is plain http(s) with no credentials, query or fragment, and its host is
     * either the server's own (`serverHost`, what the player connected to) or not a loopback,
     * private, link-local or unspecified address. Judged on the URL text alone; a name that
     * resolves to a private address is not caught here.
     */
    bool IsAllowedMirrorUrl(std::string_view url, std::string_view serverHost);

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
