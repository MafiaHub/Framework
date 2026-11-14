/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "integrations/server/asset_file_server.h"
#include "integrations/server/client_package.h"
#include "utils/crypto.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace ClientPackageTest {
    using Framework::Integrations::Server::ClientPackage;
    using Framework::Integrations::Server::ClientPackageRequest;

    inline void WriteText(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    }

    // A resource folder with one client script, and the request that packages it into `root/staging`.
    inline ClientPackageRequest Resource(const std::filesystem::path &root, const std::string &clientScript) {
        std::filesystem::create_directories(root / "staging");
        WriteText(root / "demo" / "package.json", "{\"name\":\"demo\"}");
        WriteText(root / "demo" / "client.js", clientScript);

        ClientPackageRequest request;
        request.name       = "demo";
        request.path       = (root / "demo").string();
        request.stagingDir = root / "staging";
        request.manifest.ParseJson({{"name", "demo"}, {"version", "1.0.0"}, {"mafiahub", {{"client", "client.js"}}}});
        request.key.fill(7);
        return request;
    }
} // namespace ClientPackageTest

MODULE(client_package, {
    using namespace ClientPackageTest;
    using Framework::Integrations::Server::AssetFileServer;
    using Framework::Integrations::Server::BuildClientPackage;
    using Framework::Integrations::Server::CommitClientPackage;
    using Framework::Integrations::Server::DiscardClientPackage;

    const auto scratch = [](const char *name) {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        return path;
    };

    IT("builds a first package and serves it once committed, with its chunk list", {
        const auto root    = scratch("fw_client_package_first");
        const auto request = Resource(root, "console.log('one')");

        ClientPackage package = BuildClientPackage(request);
        EQUALS(package.built, true);
        EQUALS(package.sha256.size(), static_cast<size_t>(64));
        EQUALS(package.served == root / "staging" / "demo.fwpak", true);
        // Not where clients fetch it from until the tick thread says so.
        EQUALS(std::filesystem::exists(package.served), false);

        EQUALS(CommitClientPackage(package), true);
        EQUALS(Framework::Utils::Crypto::Sha256FileHex(package.served.string()) == package.sha256, true);
        EQUALS(static_cast<uint64_t>(std::filesystem::file_size(package.served)), package.size);
        EQUALS(AssetFileServer::HasCurrentChunkList(package.served), true);
        std::filesystem::remove_all(root);
    });

    IT("builds a changed package on another thread beside the one clients still fetch", {
        const auto root = scratch("fw_client_package_changed");
        ClientPackage first = BuildClientPackage(Resource(root, "console.log('one')"));
        EQUALS(CommitClientPackage(first), true);

        const auto request = Resource(root, "console.log('two, and longer')");
        ClientPackage second;
        std::thread worker([&] {
            second = BuildClientPackage(request);
        });
        worker.join();
        EQUALS(second.built, true);
        EQUALS(second.sha256 != first.sha256, true);
        // What is announced still matches what is served: a client joining now gets the first one whole.
        EQUALS(Framework::Utils::Crypto::Sha256FileHex(first.served.string()) == first.sha256, true);
        EQUALS(AssetFileServer::HasCurrentChunkList(first.served), true);

        EQUALS(CommitClientPackage(second), true);
        EQUALS(Framework::Utils::Crypto::Sha256FileHex(second.served.string()) == second.sha256, true);
        EQUALS(AssetFileServer::HasCurrentChunkList(second.served), true);
        // Nothing of the build is left beside the served file.
        size_t files = 0;
        for ([[maybe_unused]] const auto &entry : std::filesystem::directory_iterator(root / "staging")) {
            ++files;
        }
        EQUALS(files, static_cast<size_t>(2));
        std::filesystem::remove_all(root);
    });

    IT("leaves the served package alone when nothing changed", {
        const auto root    = scratch("fw_client_package_same");
        const auto request = Resource(root, "console.log('one')");
        ClientPackage first = BuildClientPackage(request);
        EQUALS(CommitClientPackage(first), true);
        const auto written = std::filesystem::last_write_time(first.served);

        ClientPackage again = BuildClientPackage(request);
        EQUALS(again.built, true);
        EQUALS(again.sha256 == first.sha256, true);
        EQUALS(again.pending.empty(), true);
        EQUALS(CommitClientPackage(again), true);
        EQUALS(std::filesystem::last_write_time(first.served) == written, true);
        std::filesystem::remove_all(root);
    });

    IT("drops a build that lost to a newer one without touching the served package", {
        const auto root = scratch("fw_client_package_discard");
        ClientPackage first = BuildClientPackage(Resource(root, "console.log('one')"));
        EQUALS(CommitClientPackage(first), true);

        ClientPackage stale = BuildClientPackage(Resource(root, "console.log('stale')"));
        EQUALS(stale.pending.empty(), false);
        // Discarding clears the path: keep it to look for the files afterwards.
        const std::filesystem::path pending = stale.pending;
        EQUALS(std::filesystem::exists(pending), true);
        DiscardClientPackage(stale);
        EQUALS(std::filesystem::exists(pending), false);
        EQUALS(std::filesystem::exists(AssetFileServer::ChunkListPath(pending)), false);
        EQUALS(Framework::Utils::Crypto::Sha256FileHex(first.served.string()) == first.sha256, true);
        std::filesystem::remove_all(root);
    });

    IT("reports a resource it cannot package instead of serving a half-built one", {
        const auto root = scratch("fw_client_package_broken");
        auto request    = Resource(root, "console.log('one')");
        request.path    = (root / "missing").string();
        ClientPackage package = BuildClientPackage(request);
        EQUALS(package.built, false);
        EQUALS(package.error.empty(), false);
        EQUALS(CommitClientPackage(package), false);
        EQUALS(std::filesystem::exists(root / "staging" / "demo.fwpak"), false);
        std::filesystem::remove_all(root);
    });

    IT("keeps two builds of one resource that overlap out of each other's file", {
        const auto root = scratch("fw_client_package_overlap");
        auto first      = Resource(root, "console.log('one')");
        first.build     = 1;
        ClientPackage a = BuildClientPackage(first);
        auto second     = Resource(root, "console.log('two, and longer')");
        second.build    = 2;
        ClientPackage b = BuildClientPackage(second);
        EQUALS(a.pending.empty() || b.pending.empty(), false);
        EQUALS(a.pending == b.pending, false);
        // Each is still the bytes its own hash names, whichever was written last.
        EQUALS(Framework::Utils::Crypto::Sha256FileHex(a.pending.string()) == a.sha256, true);
        EQUALS(Framework::Utils::Crypto::Sha256FileHex(b.pending.string()) == b.sha256, true);
        // The one that lost is dropped; the other is what gets served.
        DiscardClientPackage(a);
        EQUALS(CommitClientPackage(b), true);
        EQUALS(Framework::Utils::Crypto::Sha256FileHex(b.served.string()) == b.sha256, true);
        std::filesystem::remove_all(root);
    });
});
