/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "transfer.h"

#include "pak_archive.h"

#include <utils/crypto.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace Framework::Utils::StreamedAssets {
    namespace {
        std::string Lower(std::string_view text) {
            std::string out(text);
            for (char &c : out) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return out;
        }

        // Four decimal parts, each 0-255 with no leading zero, nothing else: a leading zero reads as
        // octal to some resolvers.
        bool ParseIpv4(std::string_view host, std::array<unsigned, 4> &parts) {
            std::size_t begin = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                const auto end = i < 3 ? host.find('.', begin) : host.size();
                if (end == std::string_view::npos || end == begin || end - begin > 3 || (end - begin > 1 && host[begin] == '0')) {
                    return false;
                }
                unsigned value = 0;
                for (std::size_t k = begin; k < end; ++k) {
                    if (host[k] < '0' || host[k] > '9') {
                        return false;
                    }
                    value = value * 10 + static_cast<unsigned>(host[k] - '0');
                }
                if (value > 255) {
                    return false;
                }
                parts[i] = value;
                begin    = end + 1;
            }
            return begin == host.size() + 1;
        }

        bool IsPrivateIpv4(const std::array<unsigned, 4> &p) {
            return p[0] == 0 || p[0] == 10 || p[0] == 127 || (p[0] == 100 && p[1] >= 64 && p[1] <= 127) || (p[0] == 169 && p[1] == 254) || (p[0] == 172 && p[1] >= 16 && p[1] <= 31) || (p[0] == 192 && p[1] == 168);
        }

        // Colon-separated groups of one to four hex digits appended to `groups`; the last one may be a
        // dotted IPv4 address when `ipv4Tail` allows it. An empty text is no groups.
        bool ParseIpv6Groups(std::string_view text, bool ipv4Tail, std::vector<unsigned> &groups) {
            std::size_t begin = 0;
            while (begin < text.size()) {
                auto end = text.find(':', begin);
                if (end == std::string_view::npos) {
                    end = text.size();
                }
                else if (end + 1 == text.size()) {
                    return false;
                }
                const auto piece = text.substr(begin, end - begin);
                if (piece.contains('.')) {
                    std::array<unsigned, 4> v4 {};
                    if (!ipv4Tail || end != text.size() || !ParseIpv4(piece, v4)) {
                        return false;
                    }
                    groups.push_back(v4[0] << 8 | v4[1]);
                    groups.push_back(v4[2] << 8 | v4[3]);
                }
                else {
                    if (piece.empty() || piece.size() > 4) {
                        return false;
                    }
                    unsigned value = 0;
                    for (const char c : piece) {
                        const bool digit = c >= '0' && c <= '9';
                        if (!digit && (c < 'a' || c > 'f')) {
                            return false;
                        }
                        value = value * 16 + static_cast<unsigned>(digit ? c - '0' : c - 'a' + 10);
                    }
                    groups.push_back(value);
                }
                begin = end + 1;
            }
            return true;
        }

        // A lowercase IPv6 literal, without brackets or zone, as its eight groups.
        bool ParseIpv6(std::string_view host, std::array<unsigned, 8> &groups) {
            std::vector<unsigned> head, tail;
            const auto gap = host.find("::");
            if (gap == std::string_view::npos) {
                if (!ParseIpv6Groups(host, true, head) || head.size() != 8) {
                    return false;
                }
            }
            else {
                const auto after = host.substr(gap + 2);
                if (after.contains("::") || !ParseIpv6Groups(host.substr(0, gap), false, head) || !ParseIpv6Groups(after, true, tail) || head.size() + tail.size() > 7) {
                    return false;
                }
            }
            groups.fill(0);
            std::copy(head.begin(), head.end(), groups.begin());
            std::copy(tail.begin(), tail.end(), groups.end() - static_cast<std::ptrdiff_t>(tail.size()));
            return true;
        }

        // Unspecified, loopback, IPv4-compatible and IPv4-mapped (::/95 and ::ffff:0:0/96), unique
        // local (fc00::/7) and link local (fe80::/10).
        bool IsPrivateIpv6(const std::array<unsigned, 8> &g) {
            const bool embedsIpv4 = g[0] == 0 && g[1] == 0 && g[2] == 0 && g[3] == 0 && g[4] == 0 && (g[5] == 0 || g[5] == 0xffff);
            return embedsIpv4 || (g[0] & 0xfe00) == 0xfc00 || (g[0] & 0xffc0) == 0xfe80;
        }

        bool StartsWith(std::string_view text, std::string_view prefix) {
            return text.substr(0, prefix.size()) == prefix;
        }

        std::int64_t WriteTimeTicks(const std::string &path, std::error_code &code) {
            const auto time = std::filesystem::last_write_time(path, code);
            return code ? 0 : static_cast<std::int64_t>(time.time_since_epoch().count());
        }

        // Where a file of |size| bytes is cut: at every entry of a pak and at its central directory,
        // nowhere in particular otherwise, and every kChunkSize within what is longer than that.
        // Strictly increasing, from 0 to |size|.
        std::vector<std::uint64_t> ChunkBoundaries(const std::string &path, std::uint64_t size) {
            std::vector<std::uint64_t> cuts;
            if (std::filesystem::path(path).extension() == ".pak" && !PakArchive::EntryOffsets(path, cuts)) {
                cuts.clear();
            }
            cuts.push_back(0);
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
            std::erase_if(cuts, [size](std::uint64_t cut) {
                return cut >= size;
            });
            cuts.push_back(size);

            std::vector<std::uint64_t> boundaries;
            for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
                for (std::uint64_t at = cuts[i]; at < cuts[i + 1]; at += kChunkSize) {
                    boundaries.push_back(at);
                }
            }
            boundaries.push_back(size);
            return boundaries;
        }
    } // namespace

    std::vector<Chunk> HashFileChunks(const std::string &path) {
        std::vector<Chunk> chunks;
        std::error_code code;
        const std::uint64_t size = std::filesystem::file_size(path, code);
        if (code || size == 0) {
            return chunks;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return chunks;
        }
        const std::vector<std::uint64_t> boundaries = ChunkBoundaries(path, size);
        std::vector<char> buffer(static_cast<size_t>(kChunkSize));
        for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
            const std::uint64_t length = boundaries[i + 1] - boundaries[i];
            file.seekg(static_cast<std::streamoff>(boundaries[i]));
            file.read(buffer.data(), static_cast<std::streamsize>(length));
            if (static_cast<std::uint64_t>(file.gcount()) != length) {
                return {};
            }
            chunks.push_back({boundaries[i], length, Crypto::Sha256Hex(buffer.data(), static_cast<size_t>(length))});
        }
        return chunks;
    }

    std::string FormatChunkList(const std::vector<Chunk> &chunks) {
        std::string text;
        for (const auto &chunk : chunks) {
            text += std::to_string(chunk.length);
            text += ' ';
            text += chunk.sha256;
            text += '\n';
        }
        return text;
    }

    bool ParseChunkList(std::string_view text, std::uint64_t size, std::vector<Chunk> &chunks) {
        std::vector<Chunk> parsed;
        std::uint64_t offset = 0;
        size_t start         = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos) {
                end = text.size();
            }
            std::string_view line = text.substr(start, end - start);
            start                 = end + 1;
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (line.empty()) {
                continue;
            }
            const size_t space = line.find(' ');
            if (space == std::string_view::npos) {
                return false;
            }
            std::uint64_t length          = 0;
            const auto [ptr, ec]          = std::from_chars(line.data(), line.data() + space, length);
            const std::string_view digest = line.substr(space + 1);
            if (ec != std::errc() || ptr != line.data() + space || length == 0 || length > kChunkSize || length > size - offset || !IsSha256(digest) || parsed.size() >= MaxChunkCount(size)) {
                return false;
            }
            parsed.push_back({offset, length, std::string(digest)});
            offset += length;
        }
        if (offset != size) {
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

    bool IsAllowedMirrorUrl(std::string_view url, std::string_view serverHost) {
        std::size_t schemeEnd = 0;
        if (url.size() >= 8 && Lower(url.substr(0, 8)) == "https://") {
            schemeEnd = 8;
        }
        else if (url.size() >= 7 && Lower(url.substr(0, 7)) == "http://") {
            schemeEnd = 7;
        }
        else {
            return false;
        }
        if (url.contains('?') || url.contains('#')) {
            return false;
        }
        const auto rest      = url.substr(schemeEnd);
        const auto authority = rest.substr(0, rest.find('/'));
        if (authority.empty() || authority.contains('@')) {
            return false;
        }
        const auto isPort = [](std::string_view port) {
            return !port.empty() && std::all_of(port.begin(), port.end(), [](char c) {
                return c >= '0' && c <= '9';
            });
        };
        std::string_view hostText = authority;
        const bool bracketed      = authority.front() == '[';
        if (bracketed) {
            const auto close = authority.find(']');
            if (close == std::string_view::npos) {
                return false;
            }
            const auto tail = authority.substr(close + 1);
            if (!tail.empty() && (tail.front() != ':' || !isPort(tail.substr(1)))) {
                return false;
            }
            hostText = authority.substr(1, close - 1);
        }
        else {
            const auto colon = authority.rfind(':');
            if (colon != std::string_view::npos) {
                hostText = authority.substr(0, colon);
                if (!isPort(authority.substr(colon + 1)) || hostText.contains(':')) {
                    return false;
                }
            }
        }
        if (hostText.empty()) {
            return false;
        }
        std::string host = Lower(hostText);
        if (host == Lower(serverHost)) {
            return true;
        }
        // Everything below accepts only what it can read: a form it does not know is refused, since
        // another parser may read it as a private address.
        if (bracketed) {
            std::array<unsigned, 8> groups {};
            return ParseIpv6(host, groups) && !IsPrivateIpv6(groups);
        }
        if (host.back() == '.') {
            host.pop_back();
        }
        if (host.empty() || !std::all_of(host.begin(), host.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
            })) {
            return false;
        }
        if (host == "localhost" || (host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0)) {
            return false;
        }
        // A last label that is a number, decimal or hex, makes the host an address to a resolver.
        const auto dot              = host.rfind('.');
        const std::string_view last = std::string_view(host).substr(dot == std::string::npos ? 0 : dot + 1);
        if (StartsWith(last, "0x") || std::all_of(last.begin(), last.end(), [](char c) {
                return c >= '0' && c <= '9';
            })) {
            std::array<unsigned, 4> parts {};
            return ParseIpv4(host, parts) && !IsPrivateIpv4(parts);
        }
        return true;
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
