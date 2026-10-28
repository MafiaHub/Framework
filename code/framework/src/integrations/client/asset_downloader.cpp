/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "asset_downloader.h"

#include <logging/logger.h>
#include <utils/crypto.h>

#include <httplib.h>

#include <filesystem>
#include <fstream>

namespace Framework::Integrations::Client {
    namespace {
        constexpr time_t kConnectTimeoutSeconds = 10;
        constexpr time_t kReadTimeoutSeconds    = 30;

        bool IsCached(const AssetDownloader::Job &job) {
            std::error_code code;
            const auto size = std::filesystem::file_size(job.path, code);
            return !code && size == job.size && Utils::Crypto::ConstantTimeEquals(Utils::Crypto::Sha256FileHex(job.path), job.sha256);
        }
    } // namespace

    AssetDownloader::~AssetDownloader() {
        Cancel();
    }

    void AssetDownloader::Start(const std::string &origin, std::vector<Job> jobs) {
        Cancel();
        _cancel.store(false);
        _done.store(false);
        _success.store(false);
        _bytes.store(0);
        _filesDone.store(0);
        _total      = 0;
        _filesTotal = static_cast<uint32_t>(jobs.size());
        for (const auto &job : jobs) {
            _total += job.size;
        }
        _worker = std::thread(&AssetDownloader::Run, this, origin, std::move(jobs));
    }

    void AssetDownloader::Cancel() {
        _cancel.store(true);
        if (_worker.joinable()) {
            _worker.join();
        }
        _done.store(false);
    }

    bool AssetDownloader::PollFinished(bool &success) {
        if (!_done.exchange(false)) {
            return false;
        }
        _worker.join();
        success = _success.load();
        return true;
    }

    std::string AssetDownloader::GetCurrentFile() const {
        std::lock_guard lock(_currentMutex);
        return _current;
    }

    void AssetDownloader::Run(std::string origin, std::vector<Job> jobs) {
        bool success = true;
        for (const auto &job : jobs) {
            if (_cancel.load()) {
                return;
            }
            {
                std::lock_guard lock(_currentMutex);
                _current = job.fileName;
            }
            if (IsCached(job)) {
                _bytes.fetch_add(job.size, std::memory_order_relaxed);
            }
            else if (!Fetch(origin, job)) {
                success = false;
                break;
            }
            _filesDone.fetch_add(1, std::memory_order_relaxed);
        }
        if (_cancel.load()) {
            return;
        }
        _success.store(success);
        _done.store(true);
    }

    bool AssetDownloader::Fetch(const std::string &origin, const Job &job) {
        const std::filesystem::path partial = job.path + ".part";
        std::error_code code;
        uint64_t received = 0;
        {
            std::ofstream output(partial, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not open '{}' for writing", partial.string());
                return false;
            }

            httplib::Client client(origin);
            client.set_connection_timeout(kConnectTimeoutSeconds, 0);
            client.set_read_timeout(kReadTimeoutSeconds, 0);
            const auto res = client.Get("/files/" + job.fileName, [&](const char *data, size_t length) {
                received += length;
                if (_cancel.load() || received > job.size) {
                    return false;
                }
                output.write(data, static_cast<std::streamsize>(length));
                _bytes.fetch_add(length, std::memory_order_relaxed);
                return output.good();
            });
            if (!res || res->status != 200) {
                if (!_cancel.load()) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Downloading '{}' from {} failed: {}", job.fileName, origin, res ? "HTTP " + std::to_string(res->status) : httplib::to_string(res.error()));
                }
                output.close();
                std::filesystem::remove(partial, code);
                return false;
            }
        }

        if (received != job.size || !Utils::Crypto::ConstantTimeEquals(Utils::Crypto::Sha256FileHex(partial.string()), job.sha256)) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("'{}' does not match the hash the server announced", job.fileName);
            std::filesystem::remove(partial, code);
            return false;
        }
        std::filesystem::rename(partial, job.path, code);
        if (code) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not move '{}' into the cache: {}", job.fileName, code.message());
            std::filesystem::remove(partial, code);
            return false;
        }
        return true;
    }
} // namespace Framework::Integrations::Client
