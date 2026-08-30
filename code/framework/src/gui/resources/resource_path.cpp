/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_path.h"

#include <utility>

namespace Framework::GUI::Resources {
    namespace {
        constexpr const char *kIndexDocument = "index.html";

        int HexValue(char c) {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }

        // Percent-decoding written out rather than delegated, so the rule about
        // what may not come out of an escape is stated once and cannot drift
        // with a library's unescape flags.
        //
        // An escape that would produce a path separator or a NUL is rejected
        // outright instead of being passed through: %2F and %5C exist in a
        // request only to smuggle a separator past the segment split below, and
        // a NUL only to truncate the name a syscall finally sees.
        bool PercentDecode(const std::string &input, std::string &output) {
            output.clear();
            output.reserve(input.size());

            for (std::size_t index = 0; index < input.size(); ++index) {
                if (input[index] != '%') {
                    output += input[index];
                    continue;
                }

                if (index + 2 >= input.size()) {
                    return false;
                }
                const int high = HexValue(input[index + 1]);
                const int low  = HexValue(input[index + 2]);
                if (high < 0 || low < 0) {
                    return false;
                }

                const char decoded = static_cast<char>((high << 4) | low);
                if (decoded == '/' || decoded == '\\' || decoded == '\0') {
                    return false;
                }

                output += decoded;
                index += 2;
            }
            return true;
        }

        // A segment that can only exist to leave the root, or to mean something
        // different to the filesystem than it does to the URL. A backslash is a
        // separator on Windows and a colon opens a drive or an NTFS alternate
        // stream, so neither may survive into a path a provider resolves.
        bool IsUnsafeSegment(const std::string &segment) {
            if (segment == "." || segment == "..") {
                return true;
            }
            return segment.find_first_of("\\:") != std::string::npos || segment.find('\0') != std::string::npos;
        }
    } // namespace

    bool NormalizeResourcePath(const std::string &urlPath, std::string &normalized) {
        const std::string trimmed = urlPath.substr(0, urlPath.find_first_of("?#"));
        if (trimmed.empty() || trimmed.front() != '/') {
            return false;
        }

        std::string path;
        if (!PercentDecode(trimmed, path)) {
            return false;
        }

        const bool wantsIndex = !path.empty() && path.back() == '/';

        std::string result;
        std::size_t cursor = 1;
        while (cursor <= path.size()) {
            const std::size_t next    = path.find('/', cursor);
            const std::string segment = path.substr(cursor, next == std::string::npos ? std::string::npos : next - cursor);

            if (!segment.empty()) {
                if (IsUnsafeSegment(segment)) {
                    return false;
                }
                if (!result.empty()) {
                    result += '/';
                }
                result += segment;
            }

            if (next == std::string::npos) {
                break;
            }
            cursor = next + 1;
        }

        if (result.empty()) {
            result = kIndexDocument;
        }
        else if (wantsIndex) {
            result += '/';
            result += kIndexDocument;
        }

        normalized = std::move(result);
        return true;
    }
} // namespace Framework::GUI::Resources
