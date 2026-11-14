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
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Integrations::Server {
    /**
     * Builds the asset paks a resource ships for the game engine to mount -- one per lane folder the
     * mod's policy names (`<resource>/<lane>/**`) -- into the resource package staging directory,
     * where the asset file server serves them beside the `.fwpak`s. They travel with the resource:
     * announced in its ResourceInfo, served over the same HTTP file server, refreshed by the same
     * hot reload.
     *
     * A pak is a plain ZIP the engine reads in place, so it is never encrypted, and its file name
     * carries its hash: a changed pak is a new file, never one rewritten under a mounted archive.
     * An unchanged lane folder keeps its pak without being rebuilt or rehashed.
     *
     * Tick thread only, except Prepare.
     */
    class AssetPakBuilder final {
      private:
        struct BuiltLane {
            Networking::RPC::AssetPakInfo info;
            std::vector<Utils::StreamedAssets::PakEntry> entries;
        };

      public:
        /** What Prepare built, for Adopt: the lanes' paks, and the staged files they make obsolete. */
        class Prepared final {
          public:
            /**
             * A lane could not be built for a reason that is not the resource's doing: a file that could not be
             * read or written. Not set for a lane the policy refuses or a folder with nothing to ship, which are
             * a resource shipping nothing. A failed build should not replace what was published before it.
             */
            bool Failed() const {
                return _failed;
            }

          private:
            friend class AssetPakBuilder;
            std::vector<BuiltLane> _lanes;
            std::vector<std::filesystem::path> _retired;
            bool _failed = false;
        };

        void Init(std::shared_ptr<const Utils::StreamedAssets::Policy> policy, std::filesystem::path stagingDir);

        bool IsActive() const {
            return _policy != nullptr;
        }

        /** Builds (or reuses) every lane of one resource. Empty when it ships none, or a lane breaks the policy. */
        std::vector<Networking::RPC::AssetPakInfo> Build(const std::string &resource, const std::filesystem::path &folder);

        /**
         * The slow half of Build, for a worker thread: writes each changed lane's pak under its new
         * name with its chunk list, and changes nothing the server has announced. The paks being
         * replaced stay on disk and HasEntry keeps answering for them until Adopt. Calls from several
         * threads take turns: the policy's methods are called from the thread whose turn it is.
         */
        Prepared Prepare(const std::string &resource, const std::filesystem::path &folder) const;

        /** The fast half, on the tick thread: makes what Prepare built the resource's paks and removes the ones they replace. */
        std::vector<Networking::RPC::AssetPakInfo> Adopt(const std::string &resource, Prepared prepared);

        /** The staged file of a pak Build returned. */
        std::filesystem::path PathOf(const std::string &resource, const Networking::RPC::AssetPakInfo &pak) const;

        void Forget(const std::string &resource);

        /** Whether a pak of `lane` the server currently ships carries `path` -- a mod's check before it accepts a path from a script. */
        bool HasEntry(std::string_view lane, std::string_view path) const;

      private:
        enum class LaneResult {
            Built,  // a pak, new or kept
            None,   // nothing to ship: an empty folder, or content the policy refuses
            Failed, // the pak could not be written
        };
        LaneResult BuildLane(const std::filesystem::path &laneRoot, const std::string &resource, const std::string &lane, BuiltLane &out, std::vector<std::filesystem::path> &retired) const;

        // Held for a whole Prepare, so the policy is only ever asked by one thread at a time.
        mutable std::mutex _policyMutex;
        std::shared_ptr<const Utils::StreamedAssets::Policy> _policy;
        std::filesystem::path _stagingDir;
        std::map<std::string, std::vector<BuiltLane>> _resources;
    };
} // namespace Framework::Integrations::Server
