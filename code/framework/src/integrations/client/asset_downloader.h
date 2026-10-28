/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace httplib {
    class Client;
} // namespace httplib

namespace Framework::Integrations::Client {
    // Fetches a server's resource packages and asset paks over HTTP, off the main thread. A file is
    // only written into the cache once its SHA-256 matches the one the server announced; one already
    // there and matching is not fetched again.
    class AssetDownloader final {
      public:
        struct Job {
            std::string fileName;
            std::string path;
            std::string sha256;
            uint64_t size = 0;
        };

        ~AssetDownloader();

        void Start(const std::string &origin, std::vector<Job> jobs);
        // Returns at once: a request blocked on the network is abandoned to finish on its own, and
        // nothing it does afterwards reaches the cache or this downloader.
        void Cancel();

        bool IsRunning() const {
            return _transfer != nullptr && !_reported;
        }

        // True once, on the first poll after the transfer finished, with its outcome. Its progress stays
        // readable until the next Start or Cancel.
        bool PollFinished(bool &success);

        uint64_t GetBytesDownloaded() const;
        uint64_t GetBytesTotal() const;
        uint32_t GetFilesDone() const;
        uint32_t GetFilesTotal() const;
        std::string GetCurrentFile() const;

      private:
        struct Transfer {
            std::atomic<bool> cancel {};
            std::atomic<bool> done {};
            std::atomic<bool> success {};
            std::atomic<uint64_t> bytes {};
            std::atomic<uint32_t> filesDone {};
            uint64_t total {};
            uint32_t filesTotal {};
            std::mutex mutex;
            std::string current;
            httplib::Client *client {};
        };

        static void Run(std::shared_ptr<Transfer> transfer, std::string origin, std::vector<Job> jobs);
        static bool Fetch(Transfer &transfer, const std::string &origin, const Job &job);

        std::shared_ptr<Transfer> _transfer;
        bool _reported {};
    };
} // namespace Framework::Integrations::Client
