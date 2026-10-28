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

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace Framework::Integrations::Client {
    namespace {
        constexpr time_t kConnectTimeoutSeconds = 10;
        constexpr time_t kReadTimeoutSeconds    = 30;
        constexpr const char *kTokenHeader      = "X-Download-Token";
        constexpr int kMaxServerDelayMs         = 30000;

        // Each transfer writes its own partial files, so one abandoned by Cancel never collides with its successor.
        std::atomic<uint64_t> gNextTransferId {};

        bool IsCached(const AssetDownloader::Job &job) {
            std::error_code code;
            if (!std::filesystem::exists(job.path, code)) {
                return false;
            }
            const auto size = std::filesystem::file_size(job.path, code);
            if (!code && size == job.size && Utils::Crypto::ConstantTimeEquals(Utils::Crypto::Sha256FileHex(job.path), job.sha256)) {
                return true;
            }
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Cached '{}' does not match the server's copy; downloading it again", job.fileName);
            return false;
        }

        // Sleeps in short steps so a cancelled transfer does not linger.
        bool WaitUnlessCancelled(const std::atomic<bool> &cancel, int milliseconds) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
            while (std::chrono::steady_clock::now() < until) {
                if (cancel.load()) {
                    return false;
                }
                std::this_thread::sleep_for(std::min(std::chrono::milliseconds(100), std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()) + std::chrono::milliseconds(1)));
            }
            return !cancel.load();
        }
    } // namespace

    AssetDownloader::~AssetDownloader() {
        Cancel();
    }

    void AssetDownloader::Start(const std::string &origin, const std::string &token, std::vector<Job> jobs) {
        Cancel();
        _reported             = false;
        _transfer             = std::make_shared<Transfer>();
        _transfer->filesTotal = static_cast<uint32_t>(jobs.size());
        _transfer->retry      = _retryPolicy;
        for (const auto &job : jobs) {
            _transfer->total += job.size;
        }
        std::thread(&AssetDownloader::Run, _transfer, origin, token, std::move(jobs)).detach();
    }

    void AssetDownloader::Cancel() {
        if (!_transfer) {
            return;
        }
        // httplib's stop() does not wake a read blocked in select() on Windows, so the worker is not
        // joined; holding the mutex orders this against its last write into the cache.
        {
            std::lock_guard lock(_transfer->mutex);
            _transfer->cancel.store(true);
            if (_transfer->client) {
                _transfer->client->stop();
            }
        }
        _transfer.reset();
        _reported = false;
    }

    bool AssetDownloader::PollFinished(bool &success) {
        if (!IsRunning() || !_transfer->done.load()) {
            return false;
        }
        success   = _transfer->success.load();
        _reported = true;
        return true;
    }

    uint64_t AssetDownloader::GetBytesDownloaded() const {
        return _transfer ? _transfer->bytes.load(std::memory_order_relaxed) : 0;
    }

    uint64_t AssetDownloader::GetBytesTotal() const {
        return _transfer ? _transfer->total : 0;
    }

    uint32_t AssetDownloader::GetFilesDone() const {
        return _transfer ? _transfer->filesDone.load(std::memory_order_relaxed) : 0;
    }

    uint32_t AssetDownloader::GetFilesTotal() const {
        return _transfer ? _transfer->filesTotal : 0;
    }

    std::string AssetDownloader::GetCurrentFile() const {
        if (!_transfer) {
            return {};
        }
        std::lock_guard lock(_transfer->mutex);
        return _transfer->current;
    }

    void AssetDownloader::Run(std::shared_ptr<Transfer> transfer, std::string origin, std::string token, std::vector<Job> jobs) {
        bool success = true;
        for (const auto &job : jobs) {
            if (transfer->cancel.load()) {
                return;
            }
            {
                std::lock_guard lock(transfer->mutex);
                transfer->current = job.fileName;
            }
            if (IsCached(job)) {
                transfer->bytes.fetch_add(job.size, std::memory_order_relaxed);
            }
            else if (!FetchWithRetries(*transfer, origin, token, job)) {
                success = false;
                break;
            }
            transfer->filesDone.fetch_add(1, std::memory_order_relaxed);
        }
        transfer->success.store(success);
        transfer->done.store(true);
    }

    bool AssetDownloader::FetchWithRetries(Transfer &transfer, const std::string &origin, const std::string &token, const Job &job) {
        const auto started = std::chrono::steady_clock::now();
        for (int attempt = 1;; ++attempt) {
            const uint64_t before = transfer.bytes.load(std::memory_order_relaxed);
            const Attempt result  = Fetch(transfer, origin, token, job);
            if (result.done) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Downloaded '{}' ({} bytes) in {} ms", job.fileName, job.size, elapsed);
                return true;
            }
            // The failed attempt's bytes come off the progress, so it never runs past the total.
            transfer.bytes.fetch_sub(transfer.bytes.load(std::memory_order_relaxed) - before, std::memory_order_relaxed);
            const RetryPolicy &policy = transfer.retry;
            if (!result.retry || attempt >= policy.attempts || transfer.cancel.load()) {
                return false;
            }
            // A wait the server names is its own, bounded only so a hostile server cannot park the client.
            const int delay = result.retryAfterMs > 0 ? std::min(result.retryAfterMs, kMaxServerDelayMs) : std::min(policy.firstDelayMs << std::min(attempt - 1, 16), policy.maxDelayMs);
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Retrying '{}' in {} ms (attempt {} of {})", job.fileName, delay, attempt + 1, policy.attempts);
            if (!WaitUnlessCancelled(transfer.cancel, delay)) {
                return false;
            }
        }
    }

    AssetDownloader::Attempt AssetDownloader::Fetch(Transfer &transfer, const std::string &origin, const std::string &token, const Job &job) {
        const std::filesystem::path partial = job.path + "." + std::to_string(gNextTransferId.fetch_add(1)) + ".part";
        std::error_code code;
        uint64_t received = 0;
        {
            std::ofstream output(partial, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not open '{}' for writing", partial.string());
                return {};
            }

            httplib::Client client(origin);
            client.set_connection_timeout(kConnectTimeoutSeconds, 0);
            client.set_read_timeout(kReadTimeoutSeconds, 0);
            client.set_default_headers({{kTokenHeader, token}});
            {
                std::lock_guard lock(transfer.mutex);
                if (transfer.cancel.load()) {
                    output.close();
                    std::filesystem::remove(partial, code);
                    return {};
                }
                transfer.client = &client;
            }
            // The status is judged before any body is read: an error page must not reach the partial file or
            // the progress, nor end the request early and lose the status with it.
            int status = 0;
            std::string retryAfter;
            const auto res = client.Get(
                "/files/" + job.fileName,
                [&](const httplib::Response &response) {
                    status     = response.status;
                    retryAfter = response.get_header_value("Retry-After");
                    return status == 200;
                },
                [&](const char *data, size_t length) {
                    received += length;
                    if (transfer.cancel.load() || received > job.size) {
                        return false;
                    }
                    output.write(data, static_cast<std::streamsize>(length));
                    transfer.bytes.fetch_add(length, std::memory_order_relaxed);
                    return output.good();
                });
            {
                std::lock_guard lock(transfer.mutex);
                transfer.client = nullptr;
            }
            if (!res || status != 200) {
                if (!transfer.cancel.load()) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Downloading '{}' from {} failed: {}", job.fileName, origin, status != 0 && status != 200 ? "HTTP " + std::to_string(status) : httplib::to_string(res.error()));
                }
                output.close();
                std::filesystem::remove(partial, code);
                // A refusal (403, 404) will not change by asking again; the network or a busy server may.
                Attempt failed;
                failed.retry = status == 0 || status >= 500;
                if (status == 503) {
                    failed.retryAfterMs = std::atoi(retryAfter.c_str()) * 1000;
                }
                return failed;
            }
        }

        if (received != job.size || !Utils::Crypto::ConstantTimeEquals(Utils::Crypto::Sha256FileHex(partial.string()), job.sha256)) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("'{}' does not match the hash the server announced", job.fileName);
            std::filesystem::remove(partial, code);
            Attempt corrupted;
            corrupted.retry = true;
            return corrupted;
        }

        std::lock_guard lock(transfer.mutex);
        if (transfer.cancel.load()) {
            std::filesystem::remove(partial, code);
            return {};
        }
        std::filesystem::rename(partial, job.path, code);
        if (code) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not move '{}' into the cache: {}", job.fileName, code.message());
            std::filesystem::remove(partial, code);
            return {};
        }
        Attempt done;
        done.done = true;
        return done;
    }
} // namespace Framework::Integrations::Client
