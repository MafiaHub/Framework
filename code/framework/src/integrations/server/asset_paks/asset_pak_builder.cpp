/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "asset_pak_builder.h"

#include "../asset_file_server.h"

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
        return Adopt(resource, Prepare(resource, folder));
    }

    AssetPakBuilder::Prepared AssetPakBuilder::Prepare(const std::string &resource, const std::filesystem::path &folder) const {
        Prepared prepared;
        if (!IsActive()) {
            return prepared;
        }
        // For the whole build, GetLanes included: the policy is promised one caller at a time, and two builds of
        // one lane would write the same file.
        const std::lock_guard lock(_policyMutex);

        std::error_code code;
        for (const std::string &lane : _policy->GetLanes()) {
            if (!Utils::StreamedAssets::IsValidLaneName(lane)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Asset lane '{}' is not [A-Za-z0-9_-], at most {} characters; skipped", lane, Utils::StreamedAssets::kMaxLaneName);
                continue;
            }
            const std::filesystem::path laneRoot = folder / lane;
            if (!std::filesystem::is_directory(laneRoot, code)) {
                continue;
            }
            if (!Utils::StreamedAssets::IsValidResourceName(resource)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Resource '{}' ships assets but its name is not lowercase [a-z0-9_-]; skipped", resource);
                break;
            }
            BuiltLane lanePak;
            switch (BuildLane(laneRoot, resource, lane, lanePak, prepared._retired)) {
            case LaneResult::Built: prepared._lanes.push_back(std::move(lanePak)); break;
            case LaneResult::Failed: prepared._failed = true; break;
            case LaneResult::None: break;
            }
        }
        return prepared;
    }

    std::vector<Networking::RPC::AssetPakInfo> AssetPakBuilder::Adopt(const std::string &resource, Prepared prepared) {
        std::vector<Networking::RPC::AssetPakInfo> paks;
        // A client mid-download of a retired pak simply re-syncs.
        std::error_code code;
        for (const std::filesystem::path &retired : prepared._retired) {
            std::filesystem::remove(retired, code);
            std::filesystem::remove(AssetFileServer::ChunkListPath(retired), code);
        }
        for (const BuiltLane &lane : prepared._lanes) {
            paks.push_back(lane.info);
        }
        if (prepared._lanes.empty()) {
            _resources.erase(resource);
        }
        else {
            _resources[resource] = std::move(prepared._lanes);
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
                const auto found = std::lower_bound(built.entries.begin(), built.entries.end(), path, [](const Utils::StreamedAssets::PakEntry &entry, std::string_view value) {
                    return entry.path < value;
                });
                if (found != built.entries.end() && found->path == path) {
                    return true;
                }
            }
        }
        return false;
    }

    AssetPakBuilder::LaneResult AssetPakBuilder::BuildLane(const std::filesystem::path &laneRoot, const std::string &resource, const std::string &lane, BuiltLane &out, std::vector<std::filesystem::path> &retired) const {
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
                return LaneResult::None;
            }
            if (!_policy->ShouldShip(lane, resource, normalized, entry.path().string(), reason)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("'{}' {}: {} is not shipped: {}", resource, lane, relative, reason);
                continue;
            }
            entries.push_back({normalized, entry.path().string()});
        }
        // A folder that could not be walked is not a folder with nothing in it.
        if (code) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("'{}' {}: cannot read the lane folder: {}", resource, lane, code.message());
            return LaneResult::Failed;
        }
        if (entries.empty()) {
            return LaneResult::None;
        }

        std::sort(entries.begin(), entries.end(), [](const Utils::StreamedAssets::PakSourceEntry &a, const Utils::StreamedAssets::PakSourceEntry &b) {
            return a.path < b.path;
        });
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
                return LaneResult::None;
            }
            out.info    = {lane, sha256, static_cast<std::uint64_t>(std::filesystem::file_size(path, code))};
            out.entries = std::move(listed);
            // Here rather than when it is published, which happens on the tick thread.
            if (!AssetFileServer::HasCurrentChunkList(path)) {
                AssetFileServer::WriteChunkList(path, AssetFileServer::ChunkListPath(path));
            }
            // Every other pak of the lane goes once this one is adopted, not before: until then the
            // server still announces one of them.
            const std::string prefix = resource + "." + lane + ".";
            for (const std::filesystem::directory_entry &staged : std::filesystem::directory_iterator(_stagingDir, code)) {
                const std::string name = staged.path().filename().string();
                if (name.rfind(prefix, 0) == 0 && staged.path().extension() == ".pak" && staged.path() != path) {
                    retired.push_back(staged.path());
                }
            }
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Asset pak '{}' {}: {} files, {} bytes ({})", resource, lane, out.entries.size(), out.info.size, how);
            return LaneResult::Built;
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
            return LaneResult::Failed;
        }
        const std::string sha256 = Utils::Crypto::Sha256FileHex(building.string());
        if (sha256.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("'{}' {}: cannot hash the built pak", resource, lane);
            std::filesystem::remove(building, code);
            return LaneResult::Failed;
        }

        const std::filesystem::path path = _stagingDir / Utils::StreamedAssets::PakFileName(resource, lane, sha256);
        std::filesystem::rename(building, path, code);
        if (code) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("'{}' {}: cannot move the built pak into place", resource, lane);
            return LaneResult::Failed;
        }
        std::ofstream(indexPath, std::ios::trunc) << nlohmann::json {{"fingerprint", fingerprintHash}, {"sha256", sha256}}.dump();
        return adopt(sha256, "built");
    }
} // namespace Framework::Integrations::Server
