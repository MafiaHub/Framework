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
    // there and matching is not fetched again. A failed request is retried with growing pauses before
    // the transfer gives up, and the bytes it got are kept: the next request, in this session or a
    // later one, asks only for the rest (HTTP Range). Each file is asked of the sources in order.
    class AssetDownloader final {
      public:
        struct Job {
            std::string fileName;
            std::string path;
            std::string sha256;
            uint64_t size = 0;
        };

        // Where files are fetched: <url>/<fileName>. A non-empty token is sent with every request.
        struct Source {
            std::string url;
            std::string token;
        };

        // How often a file is asked for before the transfer fails, and the first pause between tries;
        // each later pause doubles up to maxDelayMs. A pause the server names (Retry-After) is used instead.
        struct RetryPolicy {
            int attempts     = 5;
            int firstDelayMs = 1000;
            int maxDelayMs   = 30000;
        };

        ~AssetDownloader();

        void Start(std::vector<Source> sources, std::vector<Job> jobs);

        // The partial a job's bytes collect in until they are verified; named by the expected hash so a
        // newer version of the file never resumes from an older one's bytes.
        static std::string PartialPath(const Job &job);

        // Applies from the next Start.
        void SetRetryPolicy(const RetryPolicy &policy) {
            _retryPolicy = policy;
        }
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
            RetryPolicy retry;
            std::mutex mutex;
            std::string current;
            httplib::Client *client {};
        };

        // Whether a request succeeded, and if not, whether asking again could help and after how long.
        struct Attempt {
            bool done {};
            bool retry {};
            int retryAfterMs {};
        };

        static void Run(std::shared_ptr<Transfer> transfer, std::vector<Source> sources, std::vector<Job> jobs);
        static bool FetchWithRetries(Transfer &transfer, const Source &source, const Job &job);
        static Attempt Fetch(Transfer &transfer, const Source &source, const Job &job);

        std::shared_ptr<Transfer> _transfer;
        RetryPolicy _retryPolicy;
        bool _reported {};
    };
} // namespace Framework::Integrations::Client
