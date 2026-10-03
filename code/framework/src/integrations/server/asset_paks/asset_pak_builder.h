/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/rpc/server_resources.h>
#include <utils/streamed_assets/pak_archive.h>
#include <utils/streamed_assets/policy.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Integrations::Server {
    /**
     * Builds the asset paks a resource ships for the game engine to mount -- one per lane folder the
     * mod's policy names (`<resource>/<lane>/**`) -- into the resource package staging directory,
     * where the asset streamer serves them beside the `.fwpak`s. They travel with the resource:
     * announced in its ResourceInfo, downloaded by the same delta transfer, refreshed by the same
     * hot reload.
     *
     * A pak is a plain ZIP the engine reads in place, so it is never encrypted, and its file name
     * carries its hash: a changed pak is a new file, never one rewritten under a mounted archive.
     * An unchanged lane folder keeps its pak without being rebuilt or rehashed.
     *
     * Main thread only.
     */
    class AssetPakBuilder final {
      public:
        void Init(std::shared_ptr<const Utils::StreamedAssets::Policy> policy, std::filesystem::path stagingDir);

        bool IsActive() const {
            return _policy != nullptr;
        }

        /** Builds (or reuses) every lane of one resource. Empty when it ships none, or a lane breaks the policy. */
        std::vector<Networking::RPC::AssetPakInfo> Build(const std::string &resource, const std::filesystem::path &folder);

        /** The staged file of a pak Build returned. */
        std::filesystem::path PathOf(const std::string &resource, const Networking::RPC::AssetPakInfo &pak) const;

        void Forget(const std::string &resource);

        /** Whether a pak of `lane` the server currently ships carries `path` -- a mod's check before it accepts a path from a script. */
        bool HasEntry(std::string_view lane, std::string_view path) const;

      private:
        struct BuiltLane {
            Networking::RPC::AssetPakInfo info;
            std::vector<Utils::StreamedAssets::PakEntry> entries;
        };

        bool BuildLane(const std::filesystem::path &laneRoot, const std::string &resource, const std::string &lane, BuiltLane &out);

        std::shared_ptr<const Utils::StreamedAssets::Policy> _policy;
        std::filesystem::path _stagingDir;
        std::map<std::string, std::vector<BuiltLane>> _resources;
    };
} // namespace Framework::Integrations::Server
