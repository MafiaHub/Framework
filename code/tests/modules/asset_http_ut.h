/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "core_modules.h"
#include "http/webserver.h"
#include "integrations/client/asset_downloader.h"
#include "integrations/server/asset_file_server.h"
#include "networking/rpc/server_resources.h"
#include "utils/crypto.h"
#include "utils/streamed_assets/policy.h"

#include <httplib.h>
#include <mafianet/bit_stream.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Client assets over HTTP: the server's /files/ route against the client's downloader, on loopback.
namespace AssetHttpTest {
    struct Fixture {
        Framework::HTTP::Webserver webServer;
        Framework::Integrations::Server::AssetFileServer files;
        Framework::Integrations::Client::AssetDownloader downloader;
        std::filesystem::path root;
        std::filesystem::path staging;
        std::filesystem::path cache;
        int32_t port = 0;
        uint32_t filesDone   = 0;
        uint64_t bytesDone   = 0;
        uint64_t bytesTotal  = 0;

        Fixture() {
            std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
            root    = std::filesystem::temp_directory_path() / ("fw_asset_http_" + std::to_string(random()));
            staging = root / "staging";
            cache   = root / "cache";
            std::filesystem::create_directories(staging);
            std::filesystem::create_directories(cache);
            for (int attempt = 0; attempt < 8 && port == 0; ++attempt) {
                const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
                if (webServer.Init("127.0.0.1", candidate, "") == Framework::HTTP::WebserverError::WEBSERVER_NONE) {
                    port = candidate;
                }
            }
            Framework::CoreModules::SetWebserver(&webServer);
            files.Register();
        }

        ~Fixture() {
            downloader.Cancel();
            webServer.Shutdown();
            Framework::CoreModules::SetWebserver(nullptr);
            std::error_code code;
            std::filesystem::remove_all(root, code);
        }

        std::string Origin() const {
            return "http://127.0.0.1:" + std::to_string(port);
        }

        static void Write(const std::filesystem::path &path, const std::string &content) {
            std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
        }

        static std::string Read(const std::filesystem::path &path) {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        // Stages |content| as |name| and returns the job that fetches it into the cache.
        Framework::Integrations::Client::AssetDownloader::Job Stage(const std::string &name, const std::string &content, std::unordered_map<std::string, std::filesystem::path> &published) {
            Write(staging / name, content);
            published[name] = staging / name;
            return {name, (cache / name).string(), Framework::Utils::Crypto::Sha256Hex(content), content.size()};
        }

        bool Run(std::vector<Framework::Integrations::Client::AssetDownloader::Job> jobs) {
            downloader.Start(Origin(), std::move(jobs));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            bool success        = false;
            while (!downloader.PollFinished(success)) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            filesDone  = downloader.GetFilesDone();
            bytesDone  = downloader.GetBytesDownloaded();
            bytesTotal = downloader.GetBytesTotal();
            return success;
        }

        bool NoPartialsLeft() const {
            for (const auto &entry : std::filesystem::directory_iterator(cache)) {
                if (entry.path().extension() == ".part") {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace AssetHttpTest

MODULE(asset_http, {
    IT("downloads published files into the cache and counts their bytes", {
        AssetHttpTest::Fixture fixture;
        NEQUALS(fixture.port, 0);
        std::unordered_map<std::string, std::filesystem::path> published;
        const std::string pak(300000, 'x');
        auto jobs = std::vector {fixture.Stage("map.fwpak", "package bytes", published), fixture.Stage("map.stream.0123456789abcdef.pak", pak, published)};
        fixture.files.Publish(published);

        EQUALS(fixture.Run(jobs), true);
        EQUALS(AssetHttpTest::Fixture::Read(fixture.cache / "map.fwpak") == "package bytes", true);
        EQUALS(AssetHttpTest::Fixture::Read(fixture.cache / "map.stream.0123456789abcdef.pak") == pak, true);
        EQUALS(fixture.filesDone, 2u);
        EQUALS(fixture.bytesDone, fixture.bytesTotal);
        EQUALS(fixture.NoPartialsLeft(), true);
    });

    IT("keeps a cached file whose hash matches without fetching it", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job = fixture.Stage("cached.fwpak", "already here", published);
        AssetHttpTest::Fixture::Write(job.path, "already here");

        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == "already here", true);
    });

    IT("replaces a stale cached file", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job = fixture.Stage("stale.fwpak", "new bytes", published);
        AssetHttpTest::Fixture::Write(job.path, "old bytes");
        fixture.files.Publish(published);

        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == "new bytes", true);
    });

    IT("refuses a file whose bytes do not match the announced hash", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        auto job   = fixture.Stage("tampered.fwpak", "served bytes", published);
        job.sha256 = Framework::Utils::Crypto::Sha256Hex(std::string("other bytes!"));
        fixture.files.Publish(published);

        EQUALS(fixture.Run({job}), false);
        EQUALS(std::filesystem::exists(job.path), false);
        EQUALS(fixture.NoPartialsLeft(), true);
    });

    IT("refuses a body larger than the announced size", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        auto job = fixture.Stage("oversized.pak", std::string(200000, 'y'), published);
        job.size = 100;
        fixture.files.Publish(published);

        EQUALS(fixture.Run({job}), false);
        EQUALS(std::filesystem::exists(job.path), false);
        EQUALS(fixture.NoPartialsLeft(), true);
    });

    IT("fails when the server has not published the file", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job = fixture.Stage("unpublished.fwpak", "bytes", published);

        EQUALS(fixture.Run({job}), false);
        EQUALS(std::filesystem::exists(job.path), false);
    });

    IT("serves only published names, never their neighbours", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("public.fwpak", "bytes", published);
        AssetHttpTest::Fixture::Write(fixture.staging / "package.key", "secret");
        fixture.files.Publish(published);

        httplib::Client client(fixture.Origin());
        const auto key = client.Get("/files/package.key");
        EQUALS(static_cast<bool>(key), true);
        EQUALS(key->status, 404);
        const auto traversal = client.Get("/files/..%2Fstaging%2Fpackage.key");
        EQUALS(static_cast<bool>(traversal), true);
        EQUALS(traversal->status, 404);
        const auto file = client.Get("/files/public.fwpak");
        EQUALS(file->status, 200);
        EQUALS(file->body == "bytes", true);
    });

    IT("stops serving a name once it is republished without it", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("gone.fwpak", "bytes", published);
        fixture.files.Publish(published);
        fixture.files.Publish({});

        httplib::Client client(fixture.Origin());
        EQUALS(client.Get("/files/gone.fwpak")->status, 404);
    });

    IT("reports nothing after a cancel", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job = fixture.Stage("big.pak", std::string(4000000, 'z'), published);
        fixture.files.Publish(published);

        fixture.downloader.Start(fixture.Origin(), {job});
        fixture.downloader.Cancel();
        bool success = true;
        EQUALS(fixture.downloader.PollFinished(success), false);
        EQUALS(fixture.downloader.IsRunning(), false);
    });

    IT("returns from cancel at once while a request is stalled", {
        Framework::HTTP::Webserver stalling;
        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        int32_t port = 0;
        for (int attempt = 0; attempt < 8 && port == 0; ++attempt) {
            const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
            if (stalling.Init("127.0.0.1", candidate, "") == Framework::HTTP::WebserverError::WEBSERVER_NONE) {
                port = candidate;
            }
        }
        NEQUALS(port, 0);
        stalling.RegisterRequest(R"(/files/([^/]+))", [](const httplib::Request &, httplib::Response &res) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            res.status = 404;
        });

        const auto cache = std::filesystem::temp_directory_path() / ("fw_asset_http_stall_" + std::to_string(random()));
        std::filesystem::create_directories(cache);
        Framework::Integrations::Client::AssetDownloader downloader;
        downloader.Start("http://127.0.0.1:" + std::to_string(port), {{"stall.pak", (cache / "stall.pak").string(), std::string(64, 'a'), 10}});
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        const auto started = std::chrono::steady_clock::now();
        downloader.Cancel();
        EQUALS(std::chrono::steady_clock::now() - started < std::chrono::seconds(2), true);
        EQUALS(downloader.IsRunning(), false);

        stalling.Shutdown();
        std::error_code code;
        std::filesystem::remove_all(cache, code);
    });

    IT("accepts only lanes that are one safe path and URL segment", {
        using Framework::Utils::StreamedAssets::IsValidLaneName;
        EQUALS(IsValidLaneName("stream"), true);
        EQUALS(IsValidLaneName("Stream_2-hd"), true);
        EQUALS(IsValidLaneName(""), false);
        EQUALS(IsValidLaneName(std::string(33, 'a')), false);
        EQUALS(IsValidLaneName("../../x"), false);
        EQUALS(IsValidLaneName("a/b"), false);
        EQUALS(IsValidLaneName("a\\b"), false);
        EQUALS(IsValidLaneName("a.b"), false);
        EQUALS(IsValidLaneName("a b"), false);
        EQUALS(IsValidLaneName("a?b"), false);
        EQUALS(IsValidLaneName("a#b"), false);
        EQUALS(IsValidLaneName("a%2fb"), false);
        EQUALS(IsValidLaneName("c:"), false);

        Framework::Networking::RPC::AssetPakInfo pak {"../../x", std::string(64, 'a'), 1};
        EQUALS(pak.IsSane(), false);
        pak.lane = "stream";
        EQUALS(pak.IsSane(), true);
    });

    IT("accepts any resource name that is one safe file name as a package", {
        using Framework::Utils::StreamedAssets::IsValidPackageName;
        EQUALS(IsValidPackageName("My.Resource"), true);
        EQUALS(IsValidPackageName("police_station-2"), true);
        EQUALS(IsValidPackageName(""), false);
        EQUALS(IsValidPackageName("."), false);
        EQUALS(IsValidPackageName(".."), false);
        EQUALS(IsValidPackageName("a/b"), false);
        EQUALS(IsValidPackageName("a\\b"), false);
        EQUALS(IsValidPackageName("a%2fb"), false);
        EQUALS(IsValidPackageName("a b"), false);
        EQUALS(IsValidPackageName(std::string(129, 'a')), false);
    });

    IT("carries the package size in the resource list", {
        Framework::Networking::RPC::ResourceInfo sent;
        sent.name        = "map";
        sent.version     = "1.0.0";
        sent.packageHash = std::string(64, 'a');
        sent.packageSize = 78ull * 1024 * 1024;
        MafiaNet::BitStream bs;
        sent.Serialize(&bs, true);

        Framework::Networking::RPC::ResourceInfo received;
        received.Serialize(&bs, false);
        EQUALS(received.packageSize, sent.packageSize);
        EQUALS(received.packageHash == sent.packageHash, true);
    });
})
