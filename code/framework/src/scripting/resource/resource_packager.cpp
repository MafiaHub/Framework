/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_packager.h"

#include <logging/logger.h>
#include <utils/package/package.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace Framework::Scripting {
    namespace {
        const std::set<std::string> kClientAssetExtensions = {
            ".js",
            ".mjs",
            ".cjs",
            ".json",
            ".html",
            ".htm",
            ".css",
            ".png",
            ".jpg",
            ".jpeg",
            ".gif",
            ".svg",
            ".webp",
            ".ico",
            ".ttf",
            ".otf",
            ".woff",
            ".woff2",
            ".patch",
            ".txt",
            ".md",
        };

        std::string ToLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string ToRelativePosix(const std::filesystem::path &path, const std::filesystem::path &root) {
            std::error_code ec;
            auto relative = std::filesystem::relative(path, root, ec);
            if (ec) {
                return {};
            }
            std::string out = relative.generic_string();
            if (out.rfind("..", 0) == 0) {
                return {};
            }
            return out;
        }

        bool MatchGlobAt(const std::string &pattern, size_t p, const std::string &path, size_t s) {
            while (p < pattern.size()) {
                const char c = pattern[p];

                if (c == '*') {
                    const bool doubleStar = (p + 1 < pattern.size() && pattern[p + 1] == '*');
                    size_t next           = p + (doubleStar ? 2 : 1);
                    // "a/**/b" must also match "a/b".
                    if (doubleStar && next < pattern.size() && pattern[next] == '/') {
                        ++next;
                    }

                    for (size_t k = s; k <= path.size(); ++k) {
                        if (MatchGlobAt(pattern, next, path, k)) {
                            return true;
                        }
                        if (!doubleStar && k < path.size() && path[k] == '/') {
                            break;
                        }
                    }
                    return false;
                }

                if (s >= path.size()) {
                    return false;
                }
                if (c == '?') {
                    if (path[s] == '/') {
                        return false;
                    }
                }
                else if (c != path[s]) {
                    return false;
                }
                ++p;
                ++s;
            }
            return s == path.size();
        }

        bool ReadFile(const std::filesystem::path &path, std::string &out) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                return false;
            }
            const auto size = file.tellg();
            if (size < 0) {
                return false;
            }
            file.seekg(0, std::ios::beg);
            out.resize(static_cast<size_t>(size));
            if (size > 0) {
                file.read(out.data(), size);
                if (file.fail() || file.gcount() != size) {
                    out.clear();
                    return false;
                }
            }
            return true;
        }
    } // namespace

    bool ResourcePackager::IsClientAssetExtension(const std::string &extension) {
        return kClientAssetExtensions.contains(ToLower(extension));
    }

    bool ResourcePackager::MatchGlob(const std::string &pattern, const std::string &path) {
        return MatchGlobAt(pattern, 0, path, 0);
    }

    bool ResourcePackager::Package(const std::string &resourceName, const std::string &resourcePath, const PackageManifest &manifest, const Utils::Crypto::Key *key, PackagedResource &out, std::string &outError) {
        const auto &config = manifest.GetMafiaHubConfig();
        if (!config.HasClientContent()) {
            outError = "resource has no client scripts";
            return false;
        }

        const std::filesystem::path root = std::filesystem::path(resourcePath);
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || ec) {
            outError = "resource directory does not exist";
            return false;
        }

        // Ordered, so the container stays byte-identical across rebuilds.
        std::set<std::string> selected;

        if (std::filesystem::exists(root / "package.json", ec)) {
            selected.insert("package.json");
        }

        for (const auto &script : config.GetShippedPaths()) {
            const std::string relative = std::filesystem::path(script).generic_string();
            if (!std::filesystem::exists(root / relative, ec)) {
                outError = "script '" + relative + "' does not exist";
                return false;
            }
            selected.insert(relative);
        }

        if (!config.files.empty()) {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file(ec) || ec) {
                    continue;
                }
                const std::string relative = ToRelativePosix(entry.path(), root);
                if (relative.empty()) {
                    continue;
                }
                for (const auto &pattern : config.files) {
                    if (MatchGlob(pattern, relative)) {
                        selected.insert(relative);
                        break;
                    }
                }
            }
        }
        else {
            // Scanning cannot tell a server bundle from a client one, so drop what a server
            // script names.
            std::set<std::string> excluded;
            for (const auto &script : config.serverScripts) {
                const std::string relative = std::filesystem::path(script).generic_string();
                excluded.insert(relative);
                excluded.insert(relative + ".map");
            }

            std::set<std::filesystem::path> scanned;
            for (const auto &script : config.GetShippedPaths()) {
                const auto dir = (root / std::filesystem::path(script)).parent_path();
                if (dir == root || !std::filesystem::exists(dir, ec)) {
                    continue;
                }
                if (!scanned.insert(dir).second) {
                    continue;
                }

                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resource '{}' declares no mafiahub.files; scanning '{}'. Declare files to control exactly what reaches clients.", resourceName, dir.generic_string());

                for (const auto &entry : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (ec) {
                        break;
                    }
                    if (!entry.is_regular_file(ec) || ec) {
                        continue;
                    }
                    if (!IsClientAssetExtension(entry.path().extension().string())) {
                        continue;
                    }
                    const std::string relative = ToRelativePosix(entry.path(), root);
                    if (relative.empty() || excluded.contains(relative)) {
                        continue;
                    }
                    selected.insert(relative);
                }
            }
        }

        Utils::Package::Writer writer;
        for (const auto &relative : selected) {
            std::string contents;
            if (!ReadFile(root / relative, contents)) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resource '{}': could not read '{}'; skipping", resourceName, relative);
                continue;
            }
            writer.Add(relative, std::move(contents));
        }

        if (writer.GetEntryCount() == 0) {
            outError = "no client files selected";
            return false;
        }

        if (!writer.Build(key, out.blob)) {
            outError = "failed to build the container";
            return false;
        }

        out.name      = resourceName;
        out.sha256    = Utils::Crypto::Sha256Hex(out.blob);
        out.fileCount = writer.GetEntryCount();
        return true;
    }
} // namespace Framework::Scripting
