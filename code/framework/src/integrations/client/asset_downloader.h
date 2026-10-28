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
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
        void Cancel();

        bool IsRunning() const {
            return _worker.joinable();
        }

        // True once, on the first poll after the worker finished; reaps it and reports the outcome.
        bool PollFinished(bool &success);

        uint64_t GetBytesDownloaded() const {
            return _bytes.load(std::memory_order_relaxed);
        }
        uint64_t GetBytesTotal() const {
            return _total;
        }
        uint32_t GetFilesDone() const {
            return _filesDone.load(std::memory_order_relaxed);
        }
        uint32_t GetFilesTotal() const {
            return _filesTotal;
        }
        std::string GetCurrentFile() const;

      private:
        void Run(std::string origin, std::vector<Job> jobs);
        bool Fetch(const std::string &origin, const Job &job);

        std::thread _worker;
        std::atomic<bool> _cancel {};
        std::atomic<bool> _done {};
        std::atomic<bool> _success {};
        std::atomic<uint64_t> _bytes {};
        std::atomic<uint32_t> _filesDone {};
        uint64_t _total {};
        uint32_t _filesTotal {};
        mutable std::mutex _currentMutex;
        std::string _current;
    };
} // namespace Framework::Integrations::Client
