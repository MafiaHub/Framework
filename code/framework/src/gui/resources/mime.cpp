/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "mime.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace Framework::GUI::Resources {
    namespace {
        const std::unordered_map<std::string, std::string> &ExtensionTable() {
            static const std::unordered_map<std::string, std::string> table {
                {"html", "text/html"},
                {"htm", "text/html"},
                {"xhtml", "application/xhtml+xml"},
                {"css", "text/css"},
                {"js", "text/javascript"},
                {"mjs", "text/javascript"},
                {"cjs", "text/javascript"},
                {"json", "application/json"},
                {"map", "application/json"},
                {"webmanifest", "application/manifest+json"},
                {"wasm", "application/wasm"},
                {"xml", "text/xml"},
                {"txt", "text/plain"},
                {"csv", "text/csv"},
                {"vtt", "text/vtt"},
                {"svg", "image/svg+xml"},
                {"png", "image/png"},
                {"jpg", "image/jpeg"},
                {"jpeg", "image/jpeg"},
                {"gif", "image/gif"},
                {"webp", "image/webp"},
                {"avif", "image/avif"},
                {"bmp", "image/bmp"},
                {"ico", "image/x-icon"},
                {"woff", "font/woff"},
                {"woff2", "font/woff2"},
                {"ttf", "font/ttf"},
                {"otf", "font/otf"},
                {"mp3", "audio/mpeg"},
                {"ogg", "audio/ogg"},
                {"oga", "audio/ogg"},
                {"wav", "audio/wav"},
                {"m4a", "audio/mp4"},
                {"flac", "audio/flac"},
                {"mp4", "video/mp4"},
                {"webm", "video/webm"},
                {"pdf", "application/pdf"},
            };
            return table;
        }

        const std::unordered_set<std::string> &TextualTypes() {
            static const std::unordered_set<std::string> types {
                "application/javascript",
                "application/ecmascript",
                "application/json",
                "application/manifest+json",
                "application/ld+json",
                "application/xml",
                "application/xhtml+xml",
                "image/svg+xml",
            };
            return types;
        }

        std::string ExtensionOf(const std::string &path) {
            const std::size_t slash = path.find_last_of('/');
            const std::size_t dot   = path.find_last_of('.');
            if (dot == std::string::npos || (slash != std::string::npos && dot < slash) || dot + 1 == path.size()) {
                return {};
            }

            std::string extension = path.substr(dot + 1);
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }
    } // namespace

    std::string MimeTypeForPath(const std::string &path) {
        const std::string extension = ExtensionOf(path);
        if (extension.empty()) {
            return "application/octet-stream";
        }

        const auto &table = ExtensionTable();
        const auto known  = table.find(extension);
        return known != table.end() ? known->second : "application/octet-stream";
    }

    bool MimeTypeIsTextual(const std::string &mimeType) {
        // Trim any parameters a provider may have supplied ("text/html; v=1").
        // A type and subtype are case-insensitive, and a provider override is
        // whatever its caller passed, so normalize before matching.
        std::string base = mimeType.substr(0, mimeType.find(';'));
        std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (base.rfind("text/", 0) == 0) {
            return true;
        }
        return TextualTypes().count(base) > 0;
    }
} // namespace Framework::GUI::Resources
