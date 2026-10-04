/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "integrations/server/asset_paks/asset_pak_builder.h"
#include "networking/rpc/server_resources.h"
#include "utils/crypto.h"
#include "utils/streamed_assets/pak_archive.h"
#include "utils/streamed_assets/policy.h"

#include <mafianet/BitStream.h>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

// Asset paks: a resource's lane folders built into plain ZIPs an engine mounts, staged beside its
// .fwpak and announced in its ResourceInfo. A test policy stands in for a mod's. The transfer is the
// resource packages' own (DirectoryDeltaTransfer), tested in MafiaNet.
namespace StreamedAssetsTest {
    class TestPolicy final: public Framework::Utils::StreamedAssets::Policy {
      public:
        const std::vector<std::string> &GetLanes() const override {
            static const std::vector<std::string> lanes = {"stream"};
            return lanes;
        }

        bool NormalizeEntry(std::string_view lane, std::string_view, std::string_view raw, std::string &out, std::string &reason) const override {
            if (lane != "stream" || raw.empty() || raw.find("..") != std::string_view::npos || raw.find("forbidden") != std::string_view::npos) {
                reason = "refused";
                return false;
            }
            out.clear();
            for (const char c : raw) {
                out.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return true;
        }

        bool PrefersStore(std::string_view normalized) const override {
            return normalized.size() > 4 && normalized.substr(normalized.size() - 4) == ".bin";
        }
    };

    inline std::filesystem::path Scratch(const char *name) {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        return path;
    }

    inline void WriteBytes(const std::filesystem::path &path, std::size_t size, unsigned seed) {
        std::filesystem::create_directories(path.parent_path());
        std::mt19937 random(seed);
        std::string bytes(size, '\0');
        for (char &c : bytes) {
            c = static_cast<char>(random());
        }
        std::ofstream(path, std::ios::binary) << bytes;
    }
} // namespace StreamedAssetsTest

MODULE(streamed_assets, {
    using StreamedAssetsTest::Scratch;
    using StreamedAssetsTest::TestPolicy;
    using StreamedAssetsTest::WriteBytes;

    IT("writes deterministic paks the validator accepts and lists with CRCs", {
        TestPolicy policy;
        const auto root = Scratch("fw_streamed_pak");
        WriteBytes(root / "a.txt", 5000, 1);
        WriteBytes(root / "b.bin", 3000, 2);
        std::string error;
        const std::vector<Framework::Utils::StreamedAssets::PakSourceEntry> entries = {{"b.bin", (root / "b.bin").string()}, {"a.txt", (root / "a.txt").string()}};
        EQUALS(Framework::Utils::StreamedAssets::PakArchive::Write((root / "one.pak").string(), entries, policy, error), true);
        EQUALS(Framework::Utils::StreamedAssets::PakArchive::Write((root / "two.pak").string(), {entries[1], entries[0]}, policy, error), true);
        EQUALS(Framework::Utils::Crypto::Sha256FileHex((root / "one.pak").string()) == Framework::Utils::Crypto::Sha256FileHex((root / "two.pak").string()), true);

        std::vector<Framework::Utils::StreamedAssets::PakEntry> listed;
        EQUALS(Framework::Utils::StreamedAssets::PakArchive::Validate((root / "one.pak").string(), policy, "stream", "demo", listed, error), true);
        UEQUALS(listed.size(), 2u);
        EQUALS(listed[0].path == "a.txt" && listed[0].size == 5000 && listed[0].crc32 != 0, true);
        std::filesystem::remove_all(root);
    });

    IT("stages a resource's lane as a pak named by its hash, and answers its entries", {
        const auto root    = Scratch("fw_asset_pak_build");
        const auto staging = root / "staging";
        std::filesystem::create_directories(staging);
        WriteBytes(root / "demo" / "stream" / "models" / "chair.bin", 4000, 3);
        WriteBytes(root / "demo" / "stream" / "notes.txt", 600, 4);

        Framework::Integrations::Server::AssetPakBuilder builder;
        builder.Init(std::make_shared<TestPolicy>(), staging);
        const auto paks = builder.Build("demo", root / "demo");
        UEQUALS(paks.size(), 1u);
        EQUALS(paks[0].lane == "stream" && paks[0].IsSane(), true);
        const auto path = builder.PathOf("demo", paks[0]);
        EQUALS(path.filename().string() == Framework::Utils::StreamedAssets::PakFileName("demo", "stream", paks[0].sha256), true);
        EQUALS(Framework::Utils::Crypto::Sha256FileHex(path.string()) == paks[0].sha256, true);
        UEQUALS(paks[0].size, static_cast<uint64_t>(std::filesystem::file_size(path)));
        EQUALS(builder.HasEntry("stream", "models/chair.bin"), true);
        EQUALS(builder.HasEntry("stream", "models/table.bin"), false);
        EQUALS(builder.HasEntry("other", "models/chair.bin"), false);

        builder.Forget("demo");
        EQUALS(builder.HasEntry("stream", "models/chair.bin"), false);
        std::filesystem::remove_all(root);
    });

    IT("reuses an unchanged lane, and replaces the pak of a changed one", {
        const auto root    = Scratch("fw_asset_pak_reuse");
        const auto staging = root / "staging";
        std::filesystem::create_directories(staging);
        WriteBytes(root / "demo" / "stream" / "a.bin", 4000, 5);

        Framework::Integrations::Server::AssetPakBuilder builder;
        builder.Init(std::make_shared<TestPolicy>(), staging);
        const auto first = builder.Build("demo", root / "demo");
        UEQUALS(first.size(), 1u);
        const auto firstPath    = builder.PathOf("demo", first[0]);
        const auto firstWritten = std::filesystem::last_write_time(firstPath);

        const auto again = builder.Build("demo", root / "demo");
        UEQUALS(again.size(), 1u);
        EQUALS(again[0].sha256 == first[0].sha256, true);
        EQUALS(std::filesystem::last_write_time(firstPath) == firstWritten, true);

        // A same-size edit must change the timestamp fingerprint and replace the pak.
        const auto modified = std::filesystem::last_write_time(root / "demo" / "stream" / "a.bin");
        WriteBytes(root / "demo" / "stream" / "a.bin", 4000, 6);
        std::filesystem::last_write_time(root / "demo" / "stream" / "a.bin", modified + std::chrono::seconds(2));
        const auto changed = builder.Build("demo", root / "demo");
        UEQUALS(changed.size(), 1u);
        EQUALS(changed[0].sha256 != first[0].sha256, true);
        EQUALS(std::filesystem::exists(builder.PathOf("demo", changed[0])), true);
        EQUALS(std::filesystem::exists(firstPath), false);
        std::filesystem::remove_all(root);
    });

    IT("ships nothing for a lane that breaks the policy", {
        const auto root    = Scratch("fw_asset_pak_refused");
        const auto staging = root / "staging";
        std::filesystem::create_directories(staging);
        WriteBytes(root / "demo" / "stream" / "fine.bin", 100, 7);
        WriteBytes(root / "demo" / "stream" / "forbidden.bin", 100, 8);

        Framework::Integrations::Server::AssetPakBuilder builder;
        builder.Init(std::make_shared<TestPolicy>(), staging);
        UEQUALS(builder.Build("demo", root / "demo").size(), 0u);
        EQUALS(builder.HasEntry("stream", "fine.bin"), false);
        // An invalid resource name ships nothing either.
        WriteBytes(root / "Bad Name" / "stream" / "fine.bin", 100, 9);
        UEQUALS(builder.Build("Bad Name", root / "Bad Name").size(), 0u);
        std::filesystem::remove_all(root);
    });

    IT("carries asset paks in the resource list", {
        Framework::Networking::RPC::ResourceInfo info;
        info.name        = "demo";
        info.version     = "1.0.0";
        info.packageHash = std::string(64, 'b');
        info.assetPaks.push_back({"stream", std::string(64, 'a'), 1234});
        info.assetPaks.push_back({"replace", std::string(64, 'c'), 99});

        MafiaNet::BitStream stream;
        info.Serialize(&stream, true);
        Framework::Networking::RPC::ResourceInfo read;
        read.Serialize(&stream, false);
        EQUALS(read.name == "demo" && read.packageHash == info.packageHash, true);
        UEQUALS(read.assetPaks.size(), 2u);
        EQUALS(read.assetPaks[0].lane == "stream" && read.assetPaks[0].sha256 == info.assetPaks[0].sha256 && read.assetPaks[0].size == 1234, true);
        EQUALS(read.assetPaks[1].IsSane(), true);

        Framework::Networking::RPC::AssetPakInfo bad {"stream", "short", 10};
        EQUALS(bad.IsSane(), false);
    });
})
