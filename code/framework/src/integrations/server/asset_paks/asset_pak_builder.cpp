/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "asset_pak_builder.h"

#include <logging/logger.h>
#include <utils/crypto.h>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace Framework::Integrations::Server {
    namespace {
        // Bumped whenever the pak layout changes, so an index written by an older build never
        // claims a pak this one would write differently.
        constexpr int kPakFormat = 1;
    } // namespace

    void AssetPakBuilder::Init(std::shared_ptr<const Utils::StreamedAssets::Policy> policy, std::filesystem::path stagingDir) {
        _policy     = std::move(policy);
        _stagingDir = std::move(stagingDir);
    }

    std::vector<Networking::RPC::AssetPakInfo> AssetPakBuilder::Build(const std::string &resource, const std::filesystem::path &folder) {
        std::vector<Networking::RPC::AssetPakInfo> paks;
        if (!IsActive()) {
            return paks;
        }

        std::vector<BuiltLane> built;
        std::error_code code;
        for (const std::string &lane : _policy->GetLanes()) {
            const std::filesystem::path laneRoot = folder / lane;
            if (!std::filesystem::is_directory(laneRoot, code)) {
                continue;
            }
            if (!Utils::StreamedAssets::IsValidResourceName(resource)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Resource '{}' ships assets but its name is not lowercase [a-z0-9_-]; skipped", resource);
                break;
            }
            BuiltLane lanePak;
            if (BuildLane(laneRoot, resource, lane, lanePak)) {
                paks.push_back(lanePak.info);
                built.push_back(std::move(lanePak));
            }
        }

        if (built.empty()) {
            _resources.erase(resource);
        }
        else {
            _resources[resource] = std::move(built);
        }
        return paks;
    }

    std::filesystem::path AssetPakBuilder::PathOf(const std::string &resource, const Networking::RPC::AssetPakInfo &pak) const {
        return _stagingDir / Utils::StreamedAssets::PakFileName(resource, pak.lane, pak.sha256);
    }

    void AssetPakBuilder::Forget(const std::string &resource) {
        _resources.erase(resource);
    }

    bool AssetPakBuilder::HasEntry(std::string_view lane, std::string_view path) const {
        for (const auto &[name, lanes] : _resources) {
            for (const BuiltLane &built : lanes) {
                if (built.info.lane != lane) {
                    continue;
                }
                const auto found = std::lower_bound(built.entries.begin(), built.entries.end(), path, [](const Utils::StreamedAssets::PakEntry &entry, std::string_view value) { return entry.path < value; });
                if (found != built.entries.end() && found->path == path) {
                    return true;
                }
            }
        }
        return false;
    }

    bool AssetPakBuilder::BuildLane(const std::filesystem::path &laneRoot, const std::string &resource, const std::string &lane, BuiltLane &out) {
        std::error_code code;
        std::vector<Utils::StreamedAssets::PakSourceEntry> entries;
        for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(laneRoot, code)) {
            if (!entry.is_regular_file(code)) {
                continue;
            }
            const std::string relative = std::filesystem::relative(entry.path(), laneRoot, code).generic_string();
            std::string normalized;
            std::string reason;
            if (!_policy->NormalizeEntry(lane, resource, relative, normalized, reason)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("'{}' {}: {}: {}; the lane is not shipped", resource, lane, relative, reason);
                return false;
            }
            entries.push_back({normalized, entry.path().string()});
        }
        if (code || entries.empty()) {
            return false;
        }

        std::sort(entries.begin(), entries.end(), [](const Utils::StreamedAssets::PakSourceEntry &a, const Utils::StreamedAssets::PakSourceEntry &b) { return a.path < b.path; });
        std::string fingerprint = "format " + std::to_string(kPakFormat) + "\n";
        for (const Utils::StreamedAssets::PakSourceEntry &entry : entries) {
            const auto size     = std::filesystem::file_size(entry.sourceFile, code);
            const auto modified = std::filesystem::last_write_time(entry.sourceFile, code).time_since_epoch().count();
            fingerprint += fmt::format("{}|{}|{}\n", entry.path, size, modified);
        }
        const std::string fingerprintHash = Utils::Crypto::Sha256Hex(fingerprint);

        const auto adopt = [&](const std::string &sha256, const char *how) {
            const std::filesystem::path path = _stagingDir / Utils::StreamedAssets::PakFileName(resource, lane, sha256);
            std::string error;
            std::vector<Utils::StreamedAssets::PakEntry> listed;
            // Checked against the policy the client applies, whether just built or reused, so a pak
            // the client would refuse is never announced.
            if (!Utils::StreamedAssets::PakArchive::Validate(path.string(), *_policy, lane, resource, listed, error)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("'{}' {}: the pak fails validation: {}", resource, lane, error);
                return false;
            }
            out.info    = {lane, sha256, static_cast<std::uint64_t>(std::filesystem::file_size(path, code))};
            out.entries = std::move(listed);
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Asset pak '{}' {}: {} files, {} bytes ({})", resource, lane, out.entries.size(), out.info.size, how);
            return true;
        };

        // An unchanged folder keeps its pak: no rewrite, no rehash.
        const std::filesystem::path indexPath = _stagingDir / (resource + "." + lane + ".index.json");
        if (std::ifstream index(indexPath); index) {
            const nlohmann::json cached = nlohmann::json::parse(index, nullptr, false);
            if (cached.is_object() && cached.value("fingerprint", "") == fingerprintHash) {
                const std::string sha256 = cached.value("sha256", "");
                if (Utils::StreamedAssets::IsSha256(sha256) && std::filesystem::is_regular_file(_stagingDir / Utils::StreamedAssets::PakFileName(resource, lane, sha256), code)) {
                    return adopt(sha256, "unchanged");
                }
            }
        }

        const std::filesystem::path building = _stagingDir / (resource + "." + lane + ".building");
        std::string error;
        if (!Utils::StreamedAssets::PakArchive::Write(building.string(), entries, *_policy, error)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("'{}' {}: {}", resource, lane, error);
            std::filesystem::remove(building, code);
            return false;
        }
        const std::string sha256 = Utils::Crypto::Sha256FileHex(building.string());
        if (sha256.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("'{}' {}: cannot hash the built pak", resource, lane);
            std::filesystem::remove(building, code);
            return false;
        }

        // The lane's previous paks go; a client mid-download of one simply re-syncs.
        const std::string prefix = resource + "." + lane + ".";
        for (const std::filesystem::directory_entry &staged : std::filesystem::directory_iterator(_stagingDir, code)) {
            const std::string name = staged.path().filename().string();
            if (name.rfind(prefix, 0) == 0 && staged.path().extension() == ".pak") {
                std::filesystem::remove(staged.path(), code);
            }
        }
        const std::filesystem::path path = _stagingDir / Utils::StreamedAssets::PakFileName(resource, lane, sha256);
        std::filesystem::rename(building, path, code);
        if (code) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("'{}' {}: cannot move the built pak into place", resource, lane);
            return false;
        }
        std::ofstream(indexPath, std::ios::trunc) << nlohmann::json {{"fingerprint", fingerprintHash}, {"sha256", sha256}}.dump();
        return adopt(sha256, "built");
    }
} // namespace Framework::Integrations::Server
