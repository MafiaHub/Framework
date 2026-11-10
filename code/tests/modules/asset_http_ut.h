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
#include "networking/network_server.h"
#include "networking/rpc/server_resources.h"
#include "utils/crypto.h"
#include "utils/streamed_assets/pak_archive.h"
#include "utils/streamed_assets/policy.h"
#include "utils/streamed_assets/transfer.h"

#include <httplib.h>
#include <mafianet/bit_stream.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Client assets over HTTP: the server's /files/ route against the client's downloader, on loopback.
namespace AssetHttpTest {
    constexpr uint64_t kPeer = 1;

    // Retries in milliseconds, so a test that exhausts them stays quick.
    Framework::Integrations::Client::AssetDownloader::RetryPolicy FastRetries() {
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy;
        policy.firstDelayMs = 10;
        policy.maxDelayMs   = 50;
        return policy;
    }

    struct Fixture {
        Framework::HTTP::Webserver webServer;
        std::unique_ptr<Framework::Networking::NetworkServer> network;
        Framework::Integrations::Server::AssetFileServer files;
        Framework::Integrations::Client::AssetDownloader downloader;
        std::filesystem::path root;
        std::filesystem::path staging;
        std::filesystem::path cache;
        int32_t port = 0;
        std::string token;
        uint32_t filesDone  = 0;
        uint64_t bytesDone  = 0;
        uint64_t bytesTotal = 0;

        explicit Fixture(const std::string &serveDir = "", bool shareWithNetwork = false, size_t maxTransfers = 8) {
            std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
            root    = std::filesystem::temp_directory_path() / ("fw_asset_http_" + std::to_string(random()));
            staging = root / "staging";
            cache   = root / "cache";
            std::filesystem::create_directories(staging);
            std::filesystem::create_directories(cache);
            if (!serveDir.empty()) {
                std::filesystem::create_directories(root / serveDir / "files");
            }
            for (int attempt = 0; attempt < 8 && port == 0; ++attempt) {
                const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
                if (shareWithNetwork) {
                    network = std::make_unique<Framework::Networking::NetworkServer>();
                    if (!network->Init("127.0.0.1", candidate, 2)) {
                        network.reset();
                        continue;
                    }
                }
                if (webServer.Init("127.0.0.1", candidate, serveDir.empty() ? "" : (root / serveDir).string()) == Framework::HTTP::WebserverError::WEBSERVER_NONE) {
                    port = candidate;
                }
                else if (network) {
                    network->Shutdown();
                    network.reset();
                }
            }
            Framework::CoreModules::SetWebserver(&webServer);
            files.Register(maxTransfers);
            token = files.Grant(kPeer);
            downloader.SetRetryPolicy(FastRetries());
        }

        ~Fixture() {
            downloader.Cancel();
            webServer.Shutdown();
            Framework::CoreModules::SetWebserver(nullptr);
            if (network) {
                network->Shutdown();
            }
            std::error_code code;
            std::filesystem::remove_all(root, code);
        }

        std::string Origin() const {
            return "http://127.0.0.1:" + std::to_string(port);
        }

        Framework::Integrations::Client::AssetDownloader::Source Files() const {
            return {Origin() + "/files", token};
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
            return Run({Files()}, std::move(jobs));
        }

        bool Run(std::vector<Framework::Integrations::Client::AssetDownloader::Source> sources, std::vector<Framework::Integrations::Client::AssetDownloader::Job> jobs) {
            downloader.Start(std::move(sources), std::move(jobs));
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

        httplib::Headers Authorized() const {
            return {{Framework::Integrations::Server::AssetFileServer::kTokenHeader, token}};
        }

        int Status(const std::string &path, std::string *body = nullptr) const {
            httplib::Client client(Origin());
            const auto res = client.Get(path, Authorized());
            if (!res) {
                return 0;
            }
            if (body) {
                *body = res->body;
            }
            return res->status;
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

    // Holds a download slot on the fixture's server by reading |name| with |token| and not finishing until
    // released; releases and joins on destruction, so a failed assertion cannot leave the thread joinable.
    struct HeldSlot {
        std::atomic<bool> holding {};
        std::atomic<bool> release {};
        std::thread thread;

        HeldSlot(const Fixture &fixture, const std::string &token, const std::string &name = "big.pak")
            : thread([&fixture, token, name, this] {
                httplib::Client client(fixture.Origin());
                client.Get("/files/" + name, httplib::Headers {{Framework::Integrations::Server::AssetFileServer::kTokenHeader, token}}, [this](const char *, size_t) {
                    holding.store(true);
                    while (!release.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    return true;
                });
            }) {
            while (!holding.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        void Release() {
            release.store(true);
            if (thread.joinable()) {
                thread.join();
            }
        }

        ~HeldSlot() {
            Release();
        }
    };

    // A /files/ route that answers each request with the next of |steps|, repeating the last; the downloader
    // under test fetches one file from it into a scratch cache.
    struct ScriptedServer {
        using Step = std::function<void(const httplib::Request &, httplib::Response &)>;

        Framework::HTTP::Webserver server;
        Framework::Integrations::Client::AssetDownloader downloader;
        std::filesystem::path cache;
        std::vector<Step> steps;
        std::atomic<int> requests {};
        int32_t port = 0;
        std::mutex seenMutex;
        std::vector<std::string> ranges; // the Range header of each request, empty when none
        std::vector<std::string> tokens; // the token header of each request
        std::vector<std::string> paths;  // the path of each request
        std::vector<int> ports;          // the client port of each request: one per connection

        explicit ScriptedServer(std::vector<Step> script): steps(std::move(script)) {
            std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
            for (int attempt = 0; attempt < 8 && port == 0; ++attempt) {
                const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
                if (server.Init("127.0.0.1", candidate, "") == Framework::HTTP::WebserverError::WEBSERVER_NONE) {
                    port = candidate;
                }
            }
            server.RegisterRequest(R"(/files/([^/]+))", [this](const httplib::Request &req, httplib::Response &res) {
                {
                    std::lock_guard lock(seenMutex);
                    ranges.push_back(req.get_header_value("Range"));
                    tokens.push_back(req.get_header_value(Framework::Integrations::Server::AssetFileServer::kTokenHeader));
                    paths.push_back(req.path);
                    ports.push_back(req.remote_port);
                }
                const int index = requests++;
                steps[std::min<size_t>(static_cast<size_t>(index), steps.size() - 1)](req, res);
            });
            cache = std::filesystem::temp_directory_path() / ("fw_asset_http_scripted_" + std::to_string(random()));
            std::filesystem::create_directories(cache);
            downloader.SetRetryPolicy(FastRetries());
        }

        ~ScriptedServer() {
            downloader.Cancel();
            server.Shutdown();
            std::error_code code;
            std::filesystem::remove_all(cache, code);
        }

        Framework::Integrations::Client::AssetDownloader::Job Job(const std::string &content) const {
            return {"scripted.pak", (cache / "scripted.pak").string(), Framework::Utils::Crypto::Sha256Hex(content), content.size()};
        }

        Framework::Integrations::Client::AssetDownloader::Source Files(const std::string &token = "token") const {
            return {"http://127.0.0.1:" + std::to_string(port) + "/files", token};
        }

        void Start(const Framework::Integrations::Client::AssetDownloader::Job &job) {
            downloader.Start({Files()}, {job});
        }

        // Waits for the outcome; false if none came within |limit|.
        bool Finish(bool &success, std::chrono::milliseconds limit = std::chrono::seconds(10)) {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (!downloader.PollFinished(success)) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return true;
        }

        static Step Status(int status) {
            return [status](const httplib::Request &, httplib::Response &res) {
                res.status = status;
            };
        }

        // A 206 exactly as given: httplib would otherwise apply the request's range to the body again.
        static Step RawPartial(const std::string &contentRange, const std::string &body) {
            return [contentRange, body](const httplib::Request &req, httplib::Response &res) {
                const_cast<httplib::Request &>(req).ranges.clear();
                res.status = 206;
                res.set_header("Content-Range", contentRange);
                res.set_content(body, "application/octet-stream");
            };
        }

        static Step Body(const std::string &content) {
            return [content](const httplib::Request &, httplib::Response &res) {
                res.set_content(content, "application/octet-stream");
            };
        }

        // Serves |content| by whatever range is asked, as a server would.
        static Step Ranges(const std::string &content) {
            return [content](const httplib::Request &, httplib::Response &res) {
                res.set_content_provider(content.size(), "application/octet-stream", [content](size_t offset, size_t length, httplib::DataSink &sink) {
                    return sink.write(content.data() + offset, length);
                });
            };
        }

        static bool IsChunkList(const httplib::Request &req) {
            return req.path.size() > 7 && req.path.compare(req.path.size() - 7, 7, ".chunks") == 0;
        }
    };

    // |content| cut every kChunkSize, as a file that is not a pak is.
    inline std::vector<Framework::Utils::StreamedAssets::Chunk> FixedChunks(const std::string &content) {
        std::vector<Framework::Utils::StreamedAssets::Chunk> chunks;
        const size_t size = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        for (size_t offset = 0; offset < content.size(); offset += size) {
            const size_t length = std::min(size, content.size() - offset);
            chunks.push_back({offset, length, Framework::Utils::Crypto::Sha256Hex(content.substr(offset, length))});
        }
        return chunks;
    }

    // A policy that stores every entry as is, so a pak's layout follows from its entries' sizes.
    class StorePolicy final: public Framework::Utils::StreamedAssets::Policy {
      public:
        const std::vector<std::string> &GetLanes() const override {
            static const std::vector<std::string> lanes = {"stream"};
            return lanes;
        }
        bool NormalizeEntry(std::string_view, std::string_view, std::string_view raw, std::string &out, std::string &) const override {
            out = std::string(raw);
            return true;
        }
        bool PrefersStore(std::string_view) const override {
            return true;
        }
    };

    // Writes a pak of |entries| (path to bytes) at |pakPath| and returns its bytes.
    inline std::string WritePak(const std::filesystem::path &pakPath, const std::vector<std::pair<std::string, std::string>> &entries) {
        const std::filesystem::path sources = pakPath.parent_path() / (pakPath.filename().string() + ".src");
        std::filesystem::create_directories(sources);
        std::vector<Framework::Utils::StreamedAssets::PakSourceEntry> list;
        for (const auto &[path, bytes] : entries) {
            Fixture::Write(sources / path, bytes);
            list.push_back({path, (sources / path).string()});
        }
        std::string error;
        StorePolicy policy;
        if (!Framework::Utils::StreamedAssets::PakArchive::Write(pakPath.string(), list, policy, error)) {
            return {};
        }
        return Fixture::Read(pakPath);
    }
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
        const auto key = client.Get("/files/package.key", fixture.Authorized());
        EQUALS(static_cast<bool>(key), true);
        EQUALS(key->status, 404);
        const auto traversal = client.Get("/files/..%2Fstaging%2Fpackage.key", fixture.Authorized());
        EQUALS(static_cast<bool>(traversal), true);
        EQUALS(traversal->status, 404);
        const auto file = client.Get("/files/public.fwpak", fixture.Authorized());
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
        EQUALS(client.Get("/files/gone.fwpak", fixture.Authorized())->status, 404);
    });

    IT("reports nothing after a cancel", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job = fixture.Stage("big.pak", std::string(4000000, 'z'), published);
        fixture.files.Publish(published);

        fixture.downloader.Start({fixture.Files()}, {job});
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
        downloader.Start({{"http://127.0.0.1:" + std::to_string(port) + "/files", "token"}}, {{"stall.pak", (cache / "stall.pak").string(), std::string(64, 'a'), 10}});
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
        pak.size = Framework::Utils::StreamedAssets::kMaxPakSize + 1;
        EQUALS(pak.IsSane(), false);
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

    IT("does not declare its route without a webserver component", {
        Framework::CoreModules::SetWebserver(nullptr);
        Framework::Integrations::Server::AssetFileServer files;
        EQUALS(files.Register(8), false);
    });

    IT("declares its route on the webserver registered in core modules", {
        AssetHttpTest::Fixture fixture;
        EQUALS(Framework::CoreModules::GetWebserver() == &fixture.webServer, true);
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("map.fwpak", "package bytes", published);
        fixture.files.Publish(published);
        std::string body;
        EQUALS(fixture.Status("/files/map.fwpak", &body), 200);
        EQUALS(body == "package bytes", true);
    });

    IT("serves the static directory beside the published files without either leaking into the other", {
        AssetHttpTest::Fixture fixture("public");
        NEQUALS(fixture.port, 0);
        AssetHttpTest::Fixture::Write(fixture.root / "public" / "index.txt", "hello");
        AssetHttpTest::Fixture::Write(fixture.root / "public" / "files" / "map.fwpak", "shadow");
        AssetHttpTest::Fixture::Write(fixture.root / "public" / "files" / "loose.pak", "loose");
        AssetHttpTest::Fixture::Write(fixture.staging / "package.key", "secret");
        std::unordered_map<std::string, std::filesystem::path> published;
        auto job = fixture.Stage("map.fwpak", "package bytes", published);
        fixture.files.Publish(published);

        std::string body;
        EQUALS(fixture.Status("/index.txt", &body), 200);
        EQUALS(body == "hello", true);
        EQUALS(fixture.Status("/files/map.fwpak", &body), 200);
        EQUALS(body == "package bytes", true);
        EQUALS(fixture.Status("/files/loose.pak"), 404);
        EQUALS(fixture.Status("/files/package.key"), 404);
        EQUALS(fixture.Status("/package.key"), 404);
        EQUALS(fixture.Run({job}), true);
    });

    IT("keeps every path under /files/ away from the static directory", {
        AssetHttpTest::Fixture fixture("public");
        NEQUALS(fixture.port, 0);
        std::filesystem::create_directories(fixture.root / "public" / "files" / "private");
        AssetHttpTest::Fixture::Write(fixture.root / "public" / "files" / "private" / "key.txt", "secret");
        AssetHttpTest::Fixture::Write(fixture.root / "public" / "files" / "index.html", "listing");
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("map.fwpak", "package bytes", published);
        fixture.files.Publish(published);

        httplib::Client anonymous(fixture.Origin());
        EQUALS(anonymous.Get("/files/private/key.txt")->status, 404);
        EQUALS(fixture.Status("/files/private/key.txt"), 404);
        EQUALS(fixture.Status("/files/map.fwpak/extra"), 404);
        EQUALS(fixture.Status("/files/"), 404);
        EQUALS(fixture.Status("/files"), 404);
        std::string body;
        EQUALS(fixture.Status("/files/map.fwpak", &body), 200);
        EQUALS(body == "package bytes", true);
    });

    IT("serves files over TCP on the port the game binds for UDP", {
        AssetHttpTest::Fixture fixture("", true);
        NEQUALS(fixture.port, 0);
        EQUALS(fixture.network != nullptr, true);
        std::unordered_map<std::string, std::filesystem::path> published;
        auto job = fixture.Stage("map.fwpak", std::string(100000, 'm'), published);
        fixture.files.Publish(published);
        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == std::string(100000, 'm'), true);
    });

    IT("refuses a request without a granted token", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("map.fwpak", "package bytes", published);
        fixture.files.Publish(published);

        httplib::Client client(fixture.Origin());
        EQUALS(client.Get("/files/map.fwpak")->status, 403);
        EQUALS(client.Get("/files/map.fwpak", httplib::Headers {{Framework::Integrations::Server::AssetFileServer::kTokenHeader, std::string(64, '0')}})->status, 403);
        EQUALS(fixture.Status("/files/map.fwpak"), 200);
    });

    IT("stops honouring a token once it is revoked", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job = fixture.Stage("map.fwpak", "package bytes", published);
        fixture.files.Publish(published);
        fixture.files.Revoke(AssetHttpTest::kPeer);

        EQUALS(fixture.Status("/files/map.fwpak"), 403);
        EQUALS(fixture.Run({job}), false);
    });

    IT("gives a peer a new token each grant and drops the old one", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("map.fwpak", "package bytes", published);
        fixture.files.Publish(published);
        const std::string first = fixture.token;
        fixture.token           = fixture.files.Grant(AssetHttpTest::kPeer);

        EQUALS(fixture.token.size(), 64u);
        EQUALS(fixture.token != first, true);
        EQUALS(fixture.Status("/files/map.fwpak"), 200);
        httplib::Client client(fixture.Origin());
        EQUALS(client.Get("/files/map.fwpak", httplib::Headers {{Framework::Integrations::Server::AssetFileServer::kTokenHeader, first}})->status, 403);
    });

    IT("tells a request past the transfer cap to retry, and frees the slot when the transfer ends", {
        AssetHttpTest::Fixture fixture("", false, 1);
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("big.pak", std::string(8000000, 'b'), published);
        fixture.Stage("small.fwpak", "small", published);
        fixture.files.Publish(published);
        const std::string other = fixture.files.Grant(AssetHttpTest::kPeer + 1);

        // Another peer holds the only slot by reading the big file slowly.
        AssetHttpTest::HeldSlot held(fixture, other);
        httplib::Client client(fixture.Origin());
        const auto busy = client.Get("/files/small.fwpak", fixture.Authorized());
        EQUALS(busy->status, 503);
        EQUALS(busy->get_header_value("Retry-After") == "2", true);
        EQUALS(busy->get_header_value(Framework::Integrations::Server::AssetFileServer::kQueueHeader) == "0", true);
        held.Release();
        // The holder's slot lingers for its next file, but as spare capacity: the one in line is served.
        EQUALS(fixture.Status("/files/small.fwpak"), 200);
    });

    IT("retries a failed request and succeeds when the server recovers", {
        Framework::HTTP::Webserver flaky;
        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        int32_t port = 0;
        for (int attempt = 0; attempt < 8 && port == 0; ++attempt) {
            const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
            if (flaky.Init("127.0.0.1", candidate, "") == Framework::HTTP::WebserverError::WEBSERVER_NONE) {
                port = candidate;
            }
        }
        NEQUALS(port, 0);
        std::atomic<int> requests {};
        const std::string content = "recovered";
        flaky.RegisterRequest(R"(/files/([^/]+))", [&requests, &content](const httplib::Request &, httplib::Response &res) {
            if (++requests < 3) {
                res.status = 500;
                return;
            }
            res.set_content(content, "application/octet-stream");
        });

        const auto cache = std::filesystem::temp_directory_path() / ("fw_asset_http_flaky_" + std::to_string(random()));
        std::filesystem::create_directories(cache);
        Framework::Integrations::Client::AssetDownloader downloader;
        downloader.SetRetryPolicy(AssetHttpTest::FastRetries());
        downloader.Start({{"http://127.0.0.1:" + std::to_string(port) + "/files", "token"}}, {{"flaky.pak", (cache / "flaky.pak").string(), Framework::Utils::Crypto::Sha256Hex(content), content.size()}});
        bool success        = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!downloader.PollFinished(success) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EQUALS(success, true);
        EQUALS(requests.load(), 3);
        EQUALS(downloader.GetBytesDownloaded(), static_cast<uint64_t>(content.size()));

        flaky.Shutdown();
        std::error_code code;
        std::filesystem::remove_all(cache, code);
    });

    IT("waits as long as a busy server asks before trying again", {
        AssetHttpTest::ScriptedServer scripted({
            [](const httplib::Request &, httplib::Response &res) {
                res.status = 503;
                res.set_header("Retry-After", "1");
            },
            AssetHttpTest::ScriptedServer::Body("after the wait"),
        });
        NEQUALS(scripted.port, 0);
        const auto started = std::chrono::steady_clock::now();
        scripted.Start(scripted.Job("after the wait"));
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.requests.load(), 2);
        // FastRetries would have waited 10 ms; Retry-After said a second.
        EQUALS(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(900), true);
    });

    IT("fetches a file again when a transfer arrives corrupted", {
        const std::string content = "the real bytes";
        AssetHttpTest::ScriptedServer scripted({AssetHttpTest::ScriptedServer::Body("the fake bytes"), AssetHttpTest::ScriptedServer::Body(content)});
        NEQUALS(scripted.port, 0);
        const auto job = scripted.Job(content);
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.requests.load(), 2);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
        EQUALS(scripted.downloader.GetBytesDownloaded(), static_cast<uint64_t>(content.size()));
    });

    IT("gives up after the policy's attempts and keeps nothing", {
        AssetHttpTest::ScriptedServer scripted({AssetHttpTest::ScriptedServer::Status(500)});
        NEQUALS(scripted.port, 0);
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy = AssetHttpTest::FastRetries();
        policy.attempts                                                      = 3;
        scripted.downloader.SetRetryPolicy(policy);
        const auto job = scripted.Job("never served");
        scripted.Start(job);
        bool success = true;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, false);
        EQUALS(scripted.requests.load(), 3);
        EQUALS(std::filesystem::exists(job.path), false);
        EQUALS(scripted.downloader.GetBytesDownloaded(), 0ull);
    });

    IT("retries when the server cannot be reached", {
        Framework::Integrations::Client::AssetDownloader downloader;
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy = AssetHttpTest::FastRetries();
        policy.attempts                                                      = 2;
        downloader.SetRetryPolicy(policy);
        const auto cache = std::filesystem::temp_directory_path() / "fw_asset_http_unreachable";
        // Port 1 on loopback refuses at once.
        downloader.Start({{"http://127.0.0.1:1/files", "token"}}, {{"x.pak", (cache / "x.pak").string(), std::string(64, 'a'), 10}});
        bool success        = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!downloader.PollFinished(success) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EQUALS(success, false);
        EQUALS(downloader.IsRunning(), false);
    });

    IT("returns from cancel at once while waiting to retry", {
        AssetHttpTest::ScriptedServer scripted({AssetHttpTest::ScriptedServer::Status(500)});
        NEQUALS(scripted.port, 0);
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy;
        policy.firstDelayMs = 10000;
        policy.maxDelayMs   = 10000;
        scripted.downloader.SetRetryPolicy(policy);
        scripted.Start(scripted.Job("never served"));
        while (scripted.requests.load() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const auto started = std::chrono::steady_clock::now();
        scripted.downloader.Cancel();
        EQUALS(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500), true);
        bool success = true;
        EQUALS(scripted.downloader.PollFinished(success), false);
        // The abandoned worker stops at its next check instead of asking again after the pause.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        EQUALS(scripted.requests.load(), 1);
    });

    IT("keeps every connected peer's token valid on its own", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("map.fwpak", "package bytes", published);
        fixture.files.Publish(published);
        const std::string second = fixture.files.Grant(AssetHttpTest::kPeer + 1);
        const auto statusWith    = [&fixture](const std::string &token) {
            httplib::Client client(fixture.Origin());
            return client.Get("/files/map.fwpak", httplib::Headers {{Framework::Integrations::Server::AssetFileServer::kTokenHeader, token}})->status;
        };

        EQUALS(second != fixture.token, true);
        EQUALS(statusWith(fixture.token), 200);
        EQUALS(statusWith(second), 200);
        fixture.files.Revoke(AssetHttpTest::kPeer + 1);
        EQUALS(statusWith(second), 403);
        EQUALS(statusWith(fixture.token), 200);
        // Revoking a peer that holds nothing is harmless.
        fixture.files.Revoke(AssetHttpTest::kPeer + 99);
        EQUALS(statusWith(fixture.token), 200);
    });

    IT("serves the requested range of a published file", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("range.pak", "0123456789", published);
        fixture.files.Publish(published);

        httplib::Client client(fixture.Origin());
        auto headers = fixture.Authorized();
        headers.emplace("Range", "bytes=4-");
        const auto res = client.Get("/files/range.pak", headers);
        EQUALS(res->status, 206);
        EQUALS(res->body == "456789", true);
        // httplib 0.14 leaves an open range's end out ("bytes 4-/10"); the client reads only the start.
        EQUALS(res->get_header_value("Content-Range").rfind("bytes 4-", 0) == 0, true);
    });

    IT("resumes a partial download from where it stopped", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const std::string content(500000, 'r');
        const auto job = fixture.Stage("resume.pak", content, published);
        fixture.files.Publish(published);
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::PartialPath(job), content.substr(0, 200000));

        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
        EQUALS(fixture.bytesDone, fixture.bytesTotal);
        EQUALS(fixture.NoPartialsLeft(), true);
    });

    IT("asks only for the missing bytes and appends them", {
        const std::string content = "head|tail";
        AssetHttpTest::ScriptedServer scripted({AssetHttpTest::ScriptedServer::RawPartial("bytes 5-8/9", content.substr(5))});
        NEQUALS(scripted.port, 0);
        const auto job = scripted.Job(content);
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::PartialPath(job), content.substr(0, 5));
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.requests.load(), 1);
        EQUALS(scripted.ranges.front() == "bytes=5-", true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
        EQUALS(scripted.downloader.GetBytesDownloaded(), static_cast<uint64_t>(content.size()));
    });

    IT("starts over when the server ignores the range", {
        const std::string content = "whole file";
        AssetHttpTest::ScriptedServer scripted({AssetHttpTest::ScriptedServer::Body(content)});
        NEQUALS(scripted.port, 0);
        const auto job = scripted.Job(content);
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::PartialPath(job), "whole");
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.ranges.front() == "bytes=5-", true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
    });

    IT("starts over when a range answers from the wrong offset", {
        const std::string content = "0123456789";
        AssetHttpTest::ScriptedServer scripted({AssetHttpTest::ScriptedServer::RawPartial("bytes 2-9/10", "23456789"), AssetHttpTest::ScriptedServer::Body(content)});
        NEQUALS(scripted.port, 0);
        const auto job = scripted.Job(content);
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::PartialPath(job), "01234");
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.requests.load(), 2);
        EQUALS(scripted.ranges[1].empty(), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
    });

    IT("fetches the whole file again when a resumed one does not verify", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const std::string content(300000, 'v');
        const auto job = fixture.Stage("poisoned.pak", content, published);
        fixture.files.Publish(published);
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::PartialPath(job), std::string(100000, 'X'));

        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
        EQUALS(fixture.NoPartialsLeft(), true);
    });

    IT("discards a partial as long as the file instead of asking past its end", {
        const std::string content = "exact";
        AssetHttpTest::ScriptedServer scripted({AssetHttpTest::ScriptedServer::Body(content)});
        NEQUALS(scripted.port, 0);
        const auto job = scripted.Job(content);
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::PartialPath(job), "wrong");
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.ranges.front().empty(), true);
    });

    IT("keeps the bytes of a failed attempt for the next to resume", {
        const std::string content = "first half|second half";
        AssetHttpTest::ScriptedServer scripted({AssetHttpTest::ScriptedServer::Status(500), AssetHttpTest::ScriptedServer::RawPartial("bytes 11-21/22", content.substr(11))});
        NEQUALS(scripted.port, 0);
        const auto job = scripted.Job(content);
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::PartialPath(job), content.substr(0, 11));
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.ranges[0] == "bytes=11-", true);
        EQUALS(scripted.ranges[1] == "bytes=11-", true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
    });

    IT("names a partial by the file it expects", {
        Framework::Integrations::Client::AssetDownloader::Job job {"map.pak", "/cache/map.pak", std::string(16, 'a') + std::string(48, 'b'), 1};
        EQUALS(Framework::Integrations::Client::AssetDownloader::PartialPath(job) == "/cache/map.pak.aaaaaaaaaaaaaaaa.part", true);
        job.sha256 = std::string(64, 'c');
        EQUALS(Framework::Integrations::Client::AssetDownloader::PartialPath(job) == "/cache/map.pak.cccccccccccccccc.part", true);
    });

    IT("fetches from the download URL first and sends it no token", {
        const std::string content = "from the mirror";
        AssetHttpTest::ScriptedServer mirror({AssetHttpTest::ScriptedServer::Body(content)});
        NEQUALS(mirror.port, 0);
        const auto job = mirror.Job(content);
        mirror.downloader.Start({mirror.Files(""), {"http://127.0.0.1:1/files", "secret"}}, {job});
        bool success = false;
        EQUALS(mirror.Finish(success), true);
        EQUALS(success, true);
        EQUALS(mirror.requests.load(), 1);
        EQUALS(mirror.tokens.front().empty(), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
    });

    IT("falls back to the game port when the download URL fails", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job = fixture.Stage("fallback.pak", std::string(100000, 'f'), published);
        fixture.files.Publish(published);
        AssetHttpTest::ScriptedServer mirror({AssetHttpTest::ScriptedServer::Status(404)});
        NEQUALS(mirror.port, 0);

        EQUALS(fixture.Run({mirror.Files(""), fixture.Files()}, {job}), true);
        EQUALS(mirror.requests.load(), 1);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == std::string(100000, 'f'), true);
    });

    IT("passes over a download URL that is not http(s)", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job = fixture.Stage("plain.fwpak", "bytes", published);
        fixture.files.Publish(published);

        EQUALS(fixture.Run({{"ftp://mirror.example/files", ""}, {"no scheme", ""}, fixture.Files()}, {job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == "bytes", true);
    });

    IT("fetches from a download URL with a path and a trailing slash", {
        const std::string content = "nested";
        Framework::HTTP::Webserver cdn;
        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        int32_t port = 0;
        for (int attempt = 0; attempt < 8 && port == 0; ++attempt) {
            const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
            if (cdn.Init("127.0.0.1", candidate, "") == Framework::HTTP::WebserverError::WEBSERVER_NONE) {
                port = candidate;
            }
        }
        NEQUALS(port, 0);
        std::atomic<int> hits {};
        cdn.RegisterRequest(R"(/m2o/assets/nested\.pak)", [&content, &hits](const httplib::Request &, httplib::Response &res) {
            ++hits;
            res.set_content(content, "application/octet-stream");
        });
        const auto cache = std::filesystem::temp_directory_path() / ("fw_asset_http_cdn_" + std::to_string(random()));
        std::filesystem::create_directories(cache);
        Framework::Integrations::Client::AssetDownloader downloader;
        downloader.SetRetryPolicy(AssetHttpTest::FastRetries());
        downloader.Start({{"http://127.0.0.1:" + std::to_string(port) + "/m2o/assets/", ""}}, {{"nested.pak", (cache / "nested.pak").string(), Framework::Utils::Crypto::Sha256Hex(content), content.size()}});
        bool success        = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!downloader.PollFinished(success) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EQUALS(success, true);
        EQUALS(hits.load(), 1);
        cdn.Shutdown();
        std::error_code code;
        std::filesystem::remove_all(cache, code);
    });

    IT("carries the download URL in the resource list", {
        Framework::Networking::RPC::ServerResources sent;
        sent.packageKey    = std::string(64, 'k');
        sent.downloadToken = std::string(64, 't');
        sent.downloadUrl   = "https://cdn.example/m2o";
        MafiaNet::BitStream bs;
        sent.Serialize(&bs, true);

        Framework::Networking::RPC::ServerResources received;
        received.Serialize(&bs, false);
        EQUALS(received.downloadUrl == sent.downloadUrl, true);
        EQUALS(received.downloadToken == sent.downloadToken, true);
    });

    IT("does not retry a refusal", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job     = fixture.Stage("unpublished.fwpak", "bytes", published);
        const auto started = std::chrono::steady_clock::now();
        EQUALS(fixture.Run({job}), false);
        EQUALS(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500), true);
    });

    IT("carries the download token in the resource list", {
        Framework::Networking::RPC::ServerResources sent;
        sent.packageKey    = std::string(64, 'k');
        sent.downloadToken = std::string(64, 't');
        sent.resources.push_back({"map", "1.0.0", std::string(64, 'a'), 10, {}});
        MafiaNet::BitStream bs;
        sent.Serialize(&bs, true);

        Framework::Networking::RPC::ServerResources received;
        received.Serialize(&bs, false);
        EQUALS(received.downloadToken == sent.downloadToken, true);
        EQUALS(received.resources.size(), 1u);
        EQUALS(received.resources[0].name == "map", true);
    });

    IT("binds again after a failed bind", {
        Framework::HTTP::Webserver server;
        // A documentation address (RFC 5737) no interface holds, so the bind fails on every platform; a port
        // held by another socket would not, since httplib listens with SO_REUSEPORT where it exists.
        EQUALS(server.Init("203.0.113.1", 42000, "") == Framework::HTTP::WebserverError::WEBSERVER_BIND_FAILED, true);
        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        int32_t port = 0;
        for (int attempt = 0; attempt < 8 && port == 0; ++attempt) {
            const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
            if (server.Init("127.0.0.1", candidate, "") == Framework::HTTP::WebserverError::WEBSERVER_NONE) {
                port = candidate;
            }
        }
        NEQUALS(port, 0);
        server.Shutdown();
    });

    IT("answers as many concurrent requests as it has workers without queueing them", {
        Framework::HTTP::Webserver server;
        server.SetWorkerCount(16);
        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        int32_t port = 0;
        for (int attempt = 0; attempt < 8 && port == 0; ++attempt) {
            const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
            if (server.Init("127.0.0.1", candidate, "") == Framework::HTTP::WebserverError::WEBSERVER_NONE) {
                port = candidate;
            }
        }
        NEQUALS(port, 0);
        server.RegisterRequest("/slow", [](const httplib::Request &, httplib::Response &res) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            res.set_content("ok", "text/plain");
        });

        std::atomic<int> answered {};
        std::vector<std::thread> clients;
        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < 12; ++i) {
            clients.emplace_back([port, &answered] {
                httplib::Client client("127.0.0.1", port);
                if (const auto res = client.Get("/slow"); res && res->status == 200) {
                    ++answered;
                }
            });
        }
        for (auto &client : clients) {
            client.join();
        }
        EQUALS(answered.load(), 12);
        EQUALS(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(950), true);
        server.Shutdown();
    });

    IT("keeps asking a busy server past the attempt budget", {
        std::vector<AssetHttpTest::ScriptedServer::Step> steps;
        for (int i = 0; i < 7; ++i) {
            steps.push_back([](const httplib::Request &, httplib::Response &res) {
                res.status = 503;
                res.set_header("Retry-After", "0");
            });
        }
        steps.push_back(AssetHttpTest::ScriptedServer::Body("served at last"));
        AssetHttpTest::ScriptedServer scripted(std::move(steps));
        NEQUALS(scripted.port, 0);
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy = AssetHttpTest::FastRetries();
        policy.attempts                                                      = 3;
        scripted.downloader.SetRetryPolicy(policy);
        const auto job = scripted.Job("served at last");
        scripted.Start(job);
        bool success = false;
        // Seven refusals two seconds apart, as a 503 without a usable Retry-After is paced.
        EQUALS(scripted.Finish(success, std::chrono::seconds(30)), true);
        EQUALS(success, true);
        EQUALS(scripted.requests.load(), 8);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == "served at last", true);
        EQUALS(scripted.downloader.IsWaitingForSlot(), false);
    });

    IT("reads its place in the server's queue while it waits", {
        AssetHttpTest::ScriptedServer scripted({
            [](const httplib::Request &, httplib::Response &res) {
                res.status = 503;
                res.set_header("Retry-After", "1");
                res.set_header(Framework::Integrations::Server::AssetFileServer::kQueueHeader, "3");
            },
            AssetHttpTest::ScriptedServer::Body("after the queue"),
        });
        NEQUALS(scripted.port, 0);
        EQUALS(scripted.downloader.GetQueuePosition(), -1);
        scripted.Start(scripted.Job("after the queue"));
        while (scripted.requests.load() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EQUALS(scripted.downloader.IsWaitingForSlot(), true);
        EQUALS(scripted.downloader.GetQueuePosition(), 3);
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.downloader.IsWaitingForSlot(), false);
        EQUALS(scripted.downloader.GetQueuePosition(), -1);
    });

    IT("leaves a queue only once the policy's wait is up", {
        AssetHttpTest::ScriptedServer scripted({[](const httplib::Request &, httplib::Response &res) {
            res.status = 503;
            res.set_header("Retry-After", "1");
        }});
        NEQUALS(scripted.port, 0);
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy = AssetHttpTest::FastRetries();
        policy.attempts                                                      = 1;
        policy.queueWaitMs                                                   = 1500;
        scripted.downloader.SetRetryPolicy(policy);
        scripted.Start(scripted.Job("never served"));
        bool success = true;
        EQUALS(scripted.Finish(success, std::chrono::seconds(10)), true);
        EQUALS(success, false);
        // One attempt would have been one request; the queue was waited out across more than that.
        EQUALS(scripted.requests.load() >= 2, true);
    });

    IT("resumes a cut transfer without spending an attempt", {
        const std::string content = "0123456789";
        // Each request is answered with one byte from where it asks, then dropped.
        const AssetHttpTest::ScriptedServer::Step cut = [content](const httplib::Request &, httplib::Response &res) {
            res.set_content_provider(content.size(), "application/octet-stream", [content](size_t offset, size_t, httplib::DataSink &sink) {
                sink.write(content.data() + offset, 1);
                return false;
            });
        };
        AssetHttpTest::ScriptedServer scripted({cut, cut, cut, cut, AssetHttpTest::ScriptedServer::Body(content)});
        NEQUALS(scripted.port, 0);
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy = AssetHttpTest::FastRetries();
        policy.attempts                                                      = 2;
        scripted.downloader.SetRetryPolicy(policy);
        const auto job = scripted.Job(content);
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.requests.load(), 5);
        EQUALS(scripted.ranges[1] == "bytes=1-", true);
        EQUALS(scripted.ranges[4] == "bytes=4-", true);
        EQUALS(scripted.downloader.GetCuts(), 4u);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
    });

    IT("records when it last did anything", {
        AssetHttpTest::ScriptedServer scripted({AssetHttpTest::ScriptedServer::Body("bytes")});
        NEQUALS(scripted.port, 0);
        EQUALS(scripted.downloader.GetLastActivityMs(), 0ll);
        const int64_t before = Framework::Integrations::Client::AssetDownloader::NowMs();
        scripted.Start(scripted.Job("bytes"));
        bool success = false;
        EQUALS(scripted.Finish(success), true);
        EQUALS(success, true);
        EQUALS(scripted.downloader.GetLastActivityMs() >= before, true);
    });

    IT("hands slots out in the order peers asked", {
        AssetHttpTest::Fixture fixture("", false, 1);
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("big.pak", std::string(8000000, 'b'), published);
        fixture.Stage("small.fwpak", "small", published);
        fixture.files.Publish(published);
        const std::string second = fixture.files.Grant(AssetHttpTest::kPeer + 1);
        const std::string third  = fixture.files.Grant(AssetHttpTest::kPeer + 2);
        const auto ask           = [&fixture](const std::string &token) {
            httplib::Client client(fixture.Origin());
            return client.Get("/files/small.fwpak", httplib::Headers {{Framework::Integrations::Server::AssetFileServer::kTokenHeader, token}});
        };

        AssetHttpTest::HeldSlot held(fixture, fixture.token);
        auto res = ask(second);
        EQUALS(res->status, 503);
        EQUALS(res->get_header_value(Framework::Integrations::Server::AssetFileServer::kQueueHeader) == "0", true);
        res = ask(third);
        EQUALS(res->status, 503);
        EQUALS(res->get_header_value(Framework::Integrations::Server::AssetFileServer::kQueueHeader) == "1", true);
        // Asking again keeps the place; it does not move anyone back.
        res = ask(second);
        EQUALS(res->get_header_value(Framework::Integrations::Server::AssetFileServer::kQueueHeader) == "0", true);
        EQUALS(fixture.files.GetQueuedClients(), 2u);
        EQUALS(fixture.files.GetActiveClients(), 1u);

        held.Release();
        // The idle slot goes to the head of the line, which is still the second peer; the third keeps
        // waiting behind it, and the holder's own next file rides its slot while it has it.
        res = ask(third);
        EQUALS(res->status, 503);
        EQUALS(res->get_header_value(Framework::Integrations::Server::AssetFileServer::kQueueHeader) == "1", true);
        EQUALS(fixture.Status("/files/small.fwpak"), 200);
    });

    IT("lets a peer open more connections on the slot it holds", {
        AssetHttpTest::Fixture fixture("", false, 2);
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("big.pak", std::string(8000000, 'b'), published);
        fixture.Stage("small.fwpak", "small", published);
        fixture.files.Publish(published);
        const std::string other = fixture.files.Grant(AssetHttpTest::kPeer + 1);

        // A budget of two is one slot of two connections: the holder's second connection rides its
        // slot, and the slot is the holder's while either runs, not the other peer's for the asking.
        AssetHttpTest::HeldSlot held(fixture, fixture.token);
        AssetHttpTest::HeldSlot again(fixture, fixture.token);
        EQUALS(fixture.files.GetActiveClients(), 1u);
        const auto ask = [&fixture, &other] {
            httplib::Client client(fixture.Origin());
            return client.Get("/files/small.fwpak", httplib::Headers {{Framework::Integrations::Server::AssetFileServer::kTokenHeader, other}})->status;
        };
        EQUALS(ask(), 503);
        again.Release();
        EQUALS(ask(), 503);
        // Once the holder is idle its slot is spare capacity, and the peer in line takes it.
        held.Release();
        EQUALS(ask(), 200);
    });

    IT("drops a revoked peer from the queue", {
        AssetHttpTest::Fixture fixture("", false, 1);
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("big.pak", std::string(8000000, 'b'), published);
        fixture.Stage("small.fwpak", "small", published);
        fixture.files.Publish(published);
        const std::string second = fixture.files.Grant(AssetHttpTest::kPeer + 1);
        const std::string third  = fixture.files.Grant(AssetHttpTest::kPeer + 2);
        const auto ask           = [&fixture](const std::string &token) {
            httplib::Client client(fixture.Origin());
            return client.Get("/files/small.fwpak", httplib::Headers {{Framework::Integrations::Server::AssetFileServer::kTokenHeader, token}});
        };

        AssetHttpTest::HeldSlot held(fixture, fixture.token);
        EQUALS(ask(second)->status, 503);
        EQUALS(ask(third)->get_header_value(Framework::Integrations::Server::AssetFileServer::kQueueHeader) == "1", true);
        fixture.files.Revoke(AssetHttpTest::kPeer + 1);
        EQUALS(ask(third)->get_header_value(Framework::Integrations::Server::AssetFileServer::kQueueHeader) == "0", true);
        held.Release();
    });

    IT("writes a chunk list beside each published file and serves it outside the slots", {
        AssetHttpTest::Fixture fixture("", false, 1);
        std::unordered_map<std::string, std::filesystem::path> published;
        const std::string content(static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize * 2 + 10), 'c');
        fixture.Stage("big.pak", content, published);
        fixture.files.Publish(published);
        const std::string other = fixture.files.Grant(AssetHttpTest::kPeer + 1);

        EQUALS(std::filesystem::exists(fixture.staging / "big.pak.chunks"), true);
        std::string body;
        EQUALS(fixture.Status("/files/big.pak.chunks", &body), 200);
        std::vector<Framework::Utils::StreamedAssets::Chunk> chunks;
        EQUALS(Framework::Utils::StreamedAssets::ParseChunkList(body, content.size(), chunks), true);
        EQUALS(chunks.size(), 3u);
        EQUALS(chunks[0].sha256 == Framework::Utils::Crypto::Sha256Hex(content.substr(0, static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize))), true);
        EQUALS(chunks[2].offset, Framework::Utils::StreamedAssets::kChunkSize * 2);
        EQUALS(chunks[2].length, 10ull);
        EQUALS(chunks[2].sha256 == Framework::Utils::Crypto::Sha256Hex(content.substr(static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize * 2))), true);
        // A list of another shape beside the file is made again, not served.
        AssetHttpTest::Fixture::Write(fixture.staging / "big.pak.chunks", "not a list\n");
        fixture.files.Publish(published);
        EQUALS(fixture.Status("/files/big.pak.chunks", &body), 200);
        EQUALS(Framework::Utils::StreamedAssets::ParseChunkList(body, content.size(), chunks), true);
        // A queued peer still reads the list.
        AssetHttpTest::HeldSlot held(fixture, other);
        EQUALS(fixture.Status("/files/big.pak.chunks"), 200);
        EQUALS(fixture.Status("/files/big.pak"), 503);
        held.Release();
        EQUALS(fixture.Status("/files/nothing.pak.chunks"), 404);
    });

    IT("assembles a chunked file over several ranges and verifies it", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        std::string content;
        for (size_t i = 0; i < static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize * 3 + 123); ++i) {
            content.push_back(static_cast<char>('a' + i % 26));
        }
        const auto job = fixture.Stage("chunked.pak", content, published);
        fixture.files.Publish(published);

        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
        EQUALS(fixture.bytesDone, fixture.bytesTotal);
        EQUALS(fixture.NoPartialsLeft(), true);
        EQUALS(std::filesystem::exists(Framework::Integrations::Client::AssetDownloader::AssemblyNotePath(job)), false);
        EQUALS(Framework::Utils::StreamedAssets::IsVerified(job.path, job.sha256), true);
    });

    IT("takes the chunks an older copy already holds from disk", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const size_t chunk = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        std::string older(chunk * 3, 'o');
        std::string newer = older;
        for (size_t i = chunk; i < chunk * 2; ++i) {
            newer[i] = 'n';
        }
        auto job = fixture.Stage("map.stream.1111111111111111111111111111111111111111111111111111111111111111.pak", newer, published);
        fixture.files.Publish(published);
        const std::string previous = (fixture.cache / "map.stream.0000000000000000000000000000000000000000000000000000000000000000.pak").string();
        AssetHttpTest::Fixture::Write(previous, older);
        job.reuse = {previous};

        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == newer, true);
        EQUALS(fixture.downloader.GetBytesReused(), static_cast<uint64_t>(chunk * 2));
        EQUALS(fixture.bytesDone, fixture.bytesTotal);
    });

    IT("resumes an assembly from the chunks already in place", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const size_t chunk = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        const std::string content(chunk * 2 + 5, 'r');
        const auto job = fixture.Stage("resume.pak", content, published);
        fixture.files.Publish(published);
        // Chunk 1 is in place and noted; chunk 0 and 2 are not, nor is a chunk noted under another list's shape.
        std::string partial(content.size(), '\0');
        for (size_t i = chunk; i < chunk * 2; ++i) {
            partial[i] = 'r';
        }
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::PartialPath(job), partial);
        const std::string middle = Framework::Utils::Crypto::Sha256Hex(content.substr(chunk, chunk));
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::AssemblyNotePath(job), std::to_string(chunk) + " " + std::to_string(chunk) + " " + middle + "\n0 " + std::to_string(chunk) + " " + std::string(64, 'f') + "\n");

        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
        EQUALS(fixture.NoPartialsLeft(), true);
        EQUALS(fixture.bytesDone, fixture.bytesTotal);
    });

    IT("falls back to reading a file whole when the source ignores ranges", {
        const size_t chunk = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        const std::string content(chunk * 2 + 1, 'w');
        const auto chunks = AssetHttpTest::FixedChunks(content);
        AssetHttpTest::ScriptedServer scripted({[chunks, content](const httplib::Request &req, httplib::Response &res) {
            if (AssetHttpTest::ScriptedServer::IsChunkList(req)) {
                res.set_content(Framework::Utils::StreamedAssets::FormatChunkList(chunks), "text/plain");
                return;
            }
            // Whole body, whatever the range asked for.
            const_cast<httplib::Request &>(req).ranges.clear();
            res.set_content(content, "application/octet-stream");
        }});
        NEQUALS(scripted.port, 0);
        const auto job = scripted.Job(content);
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success, std::chrono::seconds(20)), true);
        EQUALS(success, true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
    });

    IT("parses only a chunk list that fits the file", {
        using Framework::Utils::StreamedAssets::kChunkSize;
        using Framework::Utils::StreamedAssets::ParseChunkList;
        const std::string a(64, 'a');
        const std::string b(64, 'b');
        const std::string whole = std::to_string(kChunkSize);
        std::vector<Framework::Utils::StreamedAssets::Chunk> chunks;
        EQUALS(ParseChunkList(whole + " " + a + "\n1 " + b + "\n", kChunkSize + 1, chunks), true);
        EQUALS(chunks.size(), 2u);
        EQUALS(chunks[1].offset, kChunkSize);
        EQUALS(chunks[1].length, 1ull);
        EQUALS(chunks[1].sha256 == b, true);
        // Pieces of any length up to a chunk, in the order they lie; a blank line and a CR are nothing.
        EQUALS(ParseChunkList("3 " + a + "\r\n\r\n7 " + b + "\r\n", 10, chunks), true);
        EQUALS(chunks[1].offset, 3ull);
        // Short of the file, past it, a piece longer than a chunk, a piece of nothing, a bad digest.
        EQUALS(ParseChunkList(whole + " " + a + "\n", kChunkSize + 1, chunks), false);
        EQUALS(ParseChunkList("6 " + a + "\n6 " + b + "\n", 10, chunks), false);
        EQUALS(ParseChunkList(std::to_string(kChunkSize + 1) + " " + a + "\n", kChunkSize + 1, chunks), false);
        EQUALS(ParseChunkList("0 " + a + "\n10 " + b + "\n", 10, chunks), false);
        EQUALS(ParseChunkList("10 not a hash\n", 10, chunks), false);
        EQUALS(ParseChunkList(a + "\n", 10, chunks), false);
        EQUALS(ParseChunkList("", 0, chunks), true);
        EQUALS(chunks.empty(), true);
        EQUALS(Framework::Utils::StreamedAssets::FormatChunkList({{0, 3, a}, {3, 7, b}}) == "3 " + a + "\n7 " + b + "\n", true);
    });

    IT("splits a download URL list on spaces and commas", {
        const auto urls = Framework::Utils::StreamedAssets::SplitUrlList(" https://a.example/files/, http://b.example:8080/x  ,,\thttps://c.example ");
        EQUALS(urls.size(), 3u);
        EQUALS(urls[0] == "https://a.example/files", true);
        EQUALS(urls[1] == "http://b.example:8080/x", true);
        EQUALS(urls[2] == "https://c.example", true);
        EQUALS(Framework::Utils::StreamedAssets::SplitUrlList("").empty(), true);
    });

    IT("accepts a download mirror only on a public host or the server's own", {
        using Framework::Utils::StreamedAssets::IsAllowedMirrorUrl;
        const std::string server = "203.0.113.7";
        for (const char *url : {"https://cdn.example/files", "http://cdn.example:8080/files/", "https://CDN.Example/files", "http://203.0.113.7:9000/files", "http://[2001:db8::1]/files", "http://[2001:db8::1]:8080/files", "https://cdn.example:443/files", "http://172.15.0.1/x", "http://172.32.0.1/x", "http://11.0.0.1/x", "http://100.128.0.1/x"}) {
            EQUALS(IsAllowedMirrorUrl(url, server), true);
        }
        EQUALS(IsAllowedMirrorUrl("http://192.168.1.20/files", "192.168.1.20"), true);
        for (const char *url : {"ftp://cdn.example/files", "cdn.example/files", "http://", "https:///files", "http://user:pw@cdn.example/files", "https://cdn.example/files?x=1", "https://cdn.example/files#frag", "http://localhost/x", "http://LOCALHOST:8080/x", "http://a.localhost/x", "http://127.0.0.1/x", "http://127.1/x", "http://2130706433/x", "http://10.1.2.3/x",
                                "http://172.16.0.1/x", "http://172.31.255.255/x", "http://192.168.0.1/x", "http://169.254.169.254/x", "http://100.64.0.1/x", "http://0.0.0.0/x", "http://192.168.1.20/files", "http://[::1]/x", "http://[fd00::1]/x", "http://[fe80::1]/x", "http://[::ffff:127.0.0.1]/x", "http://127.0.0.1:abc/x", "http://127.0.0.1:/x", "http://cdn.example:/x", "http://cdn.example:80a/x", "http://fe80::1/x", "http://[::1]x/y", "http://[2001:db8::1]:abc/y"}) {
            EQUALS(IsAllowedMirrorUrl(url, server), false);
        }
    });

    IT("trusts a verified file by its note until it is touched", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const auto job = fixture.Stage("noted.fwpak", "noted bytes", published);
        AssetHttpTest::Fixture::Write(job.path, "noted bytes");
        EQUALS(Framework::Utils::StreamedAssets::IsVerified(job.path, job.sha256), false);
        Framework::Utils::StreamedAssets::WriteVerifiedNote(job.path, job.sha256);
        EQUALS(Framework::Utils::StreamedAssets::IsVerified(job.path, job.sha256), true);
        EQUALS(Framework::Utils::StreamedAssets::IsVerified(job.path, std::string(64, 'x')), false);
        // A rewrite of a different length is not what was verified.
        AssetHttpTest::Fixture::Write(job.path, "other bytes!");
        EQUALS(Framework::Utils::StreamedAssets::IsVerified(job.path, job.sha256), false);
        Framework::Utils::StreamedAssets::RemoveVerifiedNote(job.path);
        EQUALS(std::filesystem::exists(Framework::Utils::StreamedAssets::VerifiedNotePath(job.path)), false);
    });

    IT("carries the deferred flag of an asset pak", {
        Framework::Networking::RPC::AssetPakInfo sent {"sounds", std::string(64, 'd'), 42, true};
        MafiaNet::BitStream bs;
        sent.Serialize(&bs, true);
        Framework::Networking::RPC::AssetPakInfo received;
        received.Serialize(&bs, false);
        EQUALS(received.deferred, true);
        EQUALS(received.lane == "sounds", true);
        EQUALS(received.IsSane(), true);
    });

    IT("keeps open transfers within the worker budget whoever holds the slots", {
        AssetHttpTest::Fixture fixture("", false, 2);
        std::unordered_map<std::string, std::filesystem::path> published;
        fixture.Stage("big.pak", std::string(8000000, 'b'), published);
        fixture.Stage("small.fwpak", "small", published);
        fixture.files.Publish(published);
        const std::string other = fixture.files.Grant(AssetHttpTest::kPeer + 1);

        // One peer's two connections fill a budget of two; its own third and another peer both wait.
        AssetHttpTest::HeldSlot first(fixture, fixture.token);
        AssetHttpTest::HeldSlot second(fixture, fixture.token);
        EQUALS(fixture.Status("/files/small.fwpak"), 503);
        httplib::Client client(fixture.Origin());
        EQUALS(client.Get("/files/small.fwpak", httplib::Headers {{Framework::Integrations::Server::AssetFileServer::kTokenHeader, other}})->status, 503);
        first.Release();
        EQUALS(fixture.Status("/files/small.fwpak"), 200);
        second.Release();
    });

    IT("fails a chunk that is cut every time instead of retrying for ever", {
        const size_t chunk = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        const std::string content(chunk * 2 + 1, 'k');
        const auto chunks = AssetHttpTest::FixedChunks(content);
        AssetHttpTest::ScriptedServer scripted({[chunks, content](const httplib::Request &req, httplib::Response &res) {
            if (AssetHttpTest::ScriptedServer::IsChunkList(req)) {
                res.set_content(Framework::Utils::StreamedAssets::FormatChunkList(chunks), "text/plain");
                return;
            }
            // One byte of every range, then the line drops.
            res.set_content_provider(content.size(), "application/octet-stream", [content](size_t offset, size_t, httplib::DataSink &sink) {
                sink.write(content.data() + offset, 1);
                return false;
            });
        }});
        NEQUALS(scripted.port, 0);
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy = AssetHttpTest::FastRetries();
        policy.attempts                                                      = 2;
        policy.connections                                                   = 1;
        scripted.downloader.SetRetryPolicy(policy);
        const auto job = scripted.Job(content);
        scripted.Start(job);
        bool success = true;
        EQUALS(scripted.Finish(success, std::chrono::seconds(20)), true);
        EQUALS(success, false);
        EQUALS(std::filesystem::exists(job.path), false);
    });

    IT("keeps its connection across the chunks of a file", {
        const size_t chunk = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        const std::string content(chunk * 2 + 1, 'k');
        const auto chunks = AssetHttpTest::FixedChunks(content);
        AssetHttpTest::ScriptedServer scripted({[chunks, content](const httplib::Request &req, httplib::Response &res) {
            if (AssetHttpTest::ScriptedServer::IsChunkList(req)) {
                res.set_content(Framework::Utils::StreamedAssets::FormatChunkList(chunks), "text/plain");
                return;
            }
            res.set_content_provider(content.size(), "application/octet-stream", [content](size_t offset, size_t length, httplib::DataSink &sink) {
                return sink.write(content.data() + offset, length);
            });
        }});
        NEQUALS(scripted.port, 0);
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy = AssetHttpTest::FastRetries();
        policy.connections                                                   = 1;
        scripted.downloader.SetRetryPolicy(policy);
        const auto job = scripted.Job(content);
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success, std::chrono::seconds(20)), true);
        EQUALS(success, true);
        // The list came on a connection of its own; the three chunks shared one.
        std::set<int> chunkPorts;
        for (size_t i = 0; i < scripted.paths.size(); ++i) {
            if (scripted.paths[i].find(".chunks") == std::string::npos) {
                chunkPorts.insert(scripted.ports[i]);
            }
        }
        EQUALS(scripted.requests.load(), 4);
        EQUALS(chunkPorts.size(), 1u);
    });

    IT("reuses the entries of an older pak after one of them grew", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        std::mt19937 random(7);
        const auto bytes = [&random](size_t size) {
            std::string out(size, '\0');
            for (char &c : out) {
                c = static_cast<char>(random());
            }
            return out;
        };
        // Three stored entries; the middle one grows by a byte, which moves the third and the central
        // directory. Cut at entries, the first and the third are the same pieces at other offsets.
        const std::string first  = bytes(1536 * 1024);
        const std::string second = bytes(2048 * 1024);
        const std::string third  = bytes(1536 * 1024);
        const std::string older  = AssetHttpTest::WritePak(fixture.cache / "map.stream.0000000000000000.pak", {{"a.bin", first}, {"b.bin", second}, {"c.bin", third}});
        const std::string newer  = AssetHttpTest::WritePak(fixture.staging / "map.stream.1111111111111111.pak", {{"a.bin", first}, {"b.bin", second + "!"}, {"c.bin", third}});
        EQUALS(older.empty(), false);
        EQUALS(newer.empty(), false);
        EQUALS(newer.size() > Framework::Utils::StreamedAssets::kChunkSize, true);
        published["map.stream.1111111111111111.pak"] = fixture.staging / "map.stream.1111111111111111.pak";
        fixture.files.Publish(published);
        Framework::Integrations::Client::AssetDownloader::Job job {"map.stream.1111111111111111.pak", (fixture.cache / "map.stream.1111111111111111.pak").string(), Framework::Utils::Crypto::Sha256Hex(newer), newer.size(),
            {(fixture.cache / "map.stream.0000000000000000.pak").string()}};

        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == newer, true);
        // Everything but the grown entry and the central directory came from the older pak: the
        // two unchanged entries with their local headers.
        const auto chunks = Framework::Utils::StreamedAssets::HashFileChunks(job.path);
        EQUALS(chunks.size(), 4u);
        EQUALS(fixture.downloader.GetBytesReused(), chunks[0].length + chunks[2].length);
        EQUALS(fixture.downloader.GetBytesReused() > first.size() + third.size(), true);
        EQUALS(fixture.bytesDone, fixture.bytesTotal);
    });

    IT("keeps the whole chunks of a cut run and goes on from the cut", {
        const size_t chunk = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        std::string content(chunk + 3000, 'c');
        for (size_t i = 0; i < content.size(); ++i) {
            content[i] = static_cast<char>('a' + i % 26);
        }
        // Three small chunks, one request between them, then one of a chunk's full length.
        std::vector<Framework::Utils::StreamedAssets::Chunk> chunks;
        for (size_t offset = 0; offset < content.size();) {
            const size_t length = offset < 3000 ? 1000 : chunk;
            chunks.push_back({offset, length, Framework::Utils::Crypto::Sha256Hex(content.substr(offset, length))});
            offset += length;
        }
        const auto list = [chunks](const httplib::Request &, httplib::Response &res) {
            res.set_content(Framework::Utils::StreamedAssets::FormatChunkList(chunks), "text/plain");
        };
        // The first run of three chunks is cut in its third: 2500 bytes, then the line drops.
        const auto cut = [content](const httplib::Request &, httplib::Response &res) {
            res.set_content_provider(content.size(), "application/octet-stream", [content](size_t offset, size_t, httplib::DataSink &sink) {
                sink.write(content.data() + offset, 2500);
                return false;
            });
        };
        AssetHttpTest::ScriptedServer scripted({list, cut, AssetHttpTest::ScriptedServer::Ranges(content)});
        NEQUALS(scripted.port, 0);
        Framework::Integrations::Client::AssetDownloader::RetryPolicy policy = AssetHttpTest::FastRetries();
        policy.attempts                                                      = 1;
        policy.connections                                                   = 1;
        scripted.downloader.SetRetryPolicy(policy);
        const auto job = scripted.Job(content);
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success, std::chrono::seconds(20)), true);
        EQUALS(success, true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
        // The two whole chunks stayed, so the cut spent no attempt of a budget of one and the next
        // request began at the third chunk.
        EQUALS(scripted.downloader.GetCuts(), 1u);
        EQUALS(scripted.requests.load(), 4);
        EQUALS(scripted.ranges[1] == "bytes=0-2999", true);
        EQUALS(scripted.ranges[2] == "bytes=2000-2999", true);
        EQUALS(scripted.ranges[3] == "bytes=3000-" + std::to_string(content.size() - 1), true);
    });

    IT("fetches again the chunks of an assembly that are not as written", {
        const size_t chunk = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        std::string content(chunk * 2 + 5, 'm');
        for (size_t i = 0; i < content.size(); ++i) {
            content[i] = static_cast<char>('a' + i % 26);
        }
        const auto chunks = AssetHttpTest::FixedChunks(content);
        AssetHttpTest::ScriptedServer scripted({[chunks, content](const httplib::Request &req, httplib::Response &res) {
            if (AssetHttpTest::ScriptedServer::IsChunkList(req)) {
                res.set_content(Framework::Utils::StreamedAssets::FormatChunkList(chunks), "text/plain");
                return;
            }
            AssetHttpTest::ScriptedServer::Ranges(content)(req, res);
        }});
        NEQUALS(scripted.port, 0);
        const auto job = scripted.Job(content);
        // The note says every chunk is in place; the middle one is not what was written.
        std::string damaged = content;
        damaged[chunk + 1]  = '#';
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::PartialPath(job), damaged);
        std::string note;
        for (const auto &piece : chunks) {
            note += std::to_string(piece.offset) + " " + std::to_string(piece.length) + " " + piece.sha256 + "\n";
        }
        AssetHttpTest::Fixture::Write(Framework::Integrations::Client::AssetDownloader::AssemblyNotePath(job), note);

        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success, std::chrono::seconds(20)), true);
        EQUALS(success, true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
        // Only the damaged chunk came over the network.
        EQUALS(scripted.requests.load(), 2);
        EQUALS(scripted.ranges[1] == "bytes=" + std::to_string(chunk) + "-" + std::to_string(chunk * 2 - 1), true);
        EQUALS(std::filesystem::exists(Framework::Integrations::Client::AssetDownloader::AssemblyNotePath(job)), false);
    });

    IT("reads a file whole when its chunk list is not the file's", {
        const size_t chunk = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        const std::string content(chunk + 1, 'f');
        const std::string other(chunk + 1, 'o');
        // The list and the ranges describe another file of the same size; the whole read is the file.
        const auto chunks = AssetHttpTest::FixedChunks(other);
        AssetHttpTest::ScriptedServer scripted({[chunks, content, other](const httplib::Request &req, httplib::Response &res) {
            if (AssetHttpTest::ScriptedServer::IsChunkList(req)) {
                res.set_content(Framework::Utils::StreamedAssets::FormatChunkList(chunks), "text/plain");
                return;
            }
            AssetHttpTest::ScriptedServer::Ranges(req.has_header("Range") ? other : content)(req, res);
        }});
        NEQUALS(scripted.port, 0);
        const auto job = scripted.Job(content);
        scripted.Start(job);
        bool success = false;
        EQUALS(scripted.Finish(success, std::chrono::seconds(20)), true);
        EQUALS(success, true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == content, true);
        EQUALS(scripted.ranges.back().empty(), true);
    });

    IT("reuses the chunks of the stale copy it replaces", {
        AssetHttpTest::Fixture fixture;
        std::unordered_map<std::string, std::filesystem::path> published;
        const size_t chunk = static_cast<size_t>(Framework::Utils::StreamedAssets::kChunkSize);
        std::string older(chunk * 2 + 7, 's');
        std::string newer = older;
        newer[chunk + 1]  = 'x';
        auto job          = fixture.Stage("pkg.fwpak", newer, published);
        fixture.files.Publish(published);
        AssetHttpTest::Fixture::Write(job.path, older);
        job.reuse = {job.path};

        EQUALS(fixture.Run({job}), true);
        EQUALS(AssetHttpTest::Fixture::Read(job.path) == newer, true);
        EQUALS(fixture.downloader.GetBytesReused(), static_cast<uint64_t>(chunk + 7));
    });
})
