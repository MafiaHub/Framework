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

        // origin ("https://host:port") and path ("/base", no trailing slash) of an http(s) URL.
        bool SplitUrl(const std::string &url, std::string &origin, std::string &path) {
            const auto scheme = url.find("://");
            if (scheme == std::string::npos || (url.compare(0, scheme, "http") != 0 && url.compare(0, scheme, "https") != 0)) {
                return false;
            }
            const auto slash = url.find('/', scheme + 3);
            origin           = url.substr(0, slash);
            path             = slash == std::string::npos ? "" : url.substr(slash);
            while (!path.empty() && path.back() == '/') {
                path.pop_back();
            }
            return origin.size() > scheme + 3;
        }

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

    void AssetDownloader::Start(std::vector<Source> sources, std::vector<Job> jobs) {
        Cancel();
        _reported             = false;
        _transfer             = std::make_shared<Transfer>();
        _transfer->filesTotal = static_cast<uint32_t>(jobs.size());
        _transfer->retry      = _retryPolicy;
        for (const auto &job : jobs) {
            _transfer->total += job.size;
        }
        std::thread(&AssetDownloader::Run, _transfer, std::move(sources), std::move(jobs)).detach();
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

    void AssetDownloader::Run(std::shared_ptr<Transfer> transfer, std::vector<Source> sources, std::vector<Job> jobs) {
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
            else {
                // The next source picks up the partial the last one left.
                bool fetched = false;
                for (size_t i = 0; i < sources.size() && !fetched && !transfer->cancel.load(); ++i) {
                    fetched = FetchWithRetries(*transfer, sources[i], job);
                    if (!fetched && i + 1 < sources.size() && !transfer->cancel.load()) {
                        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("'{}' could not be fetched from {}; trying {}", job.fileName, sources[i].url, sources[i + 1].url);
                    }
                }
                if (!fetched) {
                    success = false;
                    break;
                }
            }
            transfer->filesDone.fetch_add(1, std::memory_order_relaxed);
        }
        transfer->success.store(success);
        transfer->done.store(true);
    }

    std::string AssetDownloader::PartialPath(const Job &job) {
        return job.path + "." + job.sha256.substr(0, 16) + ".part";
    }

    bool AssetDownloader::FetchWithRetries(Transfer &transfer, const Source &source, const Job &job) {
        const auto started = std::chrono::steady_clock::now();
        for (int attempt = 1;; ++attempt) {
            const uint64_t before = transfer.bytes.load(std::memory_order_relaxed);
            const Attempt result  = Fetch(transfer, source, job);
            if (result.done) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Downloaded '{}' ({} bytes) from {} in {} ms", job.fileName, job.size, source.url, elapsed);
                return true;
            }
            // The attempt's bytes come off the progress; the next one counts what it resumes from again.
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

    AssetDownloader::Attempt AssetDownloader::Fetch(Transfer &transfer, const Source &source, const Job &job) {
        const std::filesystem::path partial = PartialPath(job);
        std::error_code code;

        // What an earlier attempt left is asked for no more; a partial as long as the file or longer is
        // not one this downloader wrote, and starts over.
        uint64_t offset = 0;
        if (std::filesystem::exists(partial, code)) {
            offset = std::filesystem::file_size(partial, code);
            if (code || offset >= job.size) {
                std::filesystem::remove(partial, code);
                offset = 0;
            }
        }

        std::string origin;
        std::string base;
        if (!SplitUrl(source.url, origin, base)) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Cannot download from '{}': not an http(s) URL", source.url);
            return {};
        }

        uint64_t received = 0;
        bool resumed      = false;
        int status        = 0;
        std::string retryAfter;
        {
            std::ofstream output;
            httplib::Client client(origin);
            client.set_connection_timeout(kConnectTimeoutSeconds, 0);
            client.set_read_timeout(kReadTimeoutSeconds, 0);
            httplib::Headers headers;
            if (!source.token.empty()) {
                headers.emplace(kTokenHeader, source.token);
            }
            if (offset > 0) {
                headers.emplace("Range", "bytes=" + std::to_string(offset) + "-");
            }
            {
                std::lock_guard lock(transfer.mutex);
                if (transfer.cancel.load()) {
                    return {};
                }
                transfer.client = &client;
            }
            // The status is judged before any body is read: an error page must not reach the partial file or
            // the progress, nor end the request early and lose the status with it. A 206 continues the
            // partial from where it ends; a 200 is the whole file again.
            const auto res = client.Get(
                base + "/" + job.fileName, headers,
                [&](const httplib::Response &response) {
                    status     = response.status;
                    retryAfter = response.get_header_value("Retry-After");
                    if (status == 206 && offset > 0 && response.get_header_value("Content-Range").rfind("bytes " + std::to_string(offset) + "-", 0) == 0) {
                        resumed  = true;
                        received = offset;
                        transfer.bytes.fetch_add(offset, std::memory_order_relaxed);
                        output.open(partial, std::ios::binary | std::ios::app);
                    }
                    else if (status == 200) {
                        output.open(partial, std::ios::binary | std::ios::trunc);
                    }
                    else {
                        return false;
                    }
                    if (!output.is_open()) {
                        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not open '{}' for writing", partial.string());
                    }
                    return output.is_open();
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
            if (transfer.cancel.load()) {
                // Whatever arrived stays for the next transfer to resume from.
                return {};
            }
            const bool accepted = status == 200 || resumed;
            if (!res || !accepted) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Downloading '{}' from {} failed: {}", job.fileName, source.url, status != 0 && !accepted ? "HTTP " + std::to_string(status) : httplib::to_string(res.error()));
                Attempt failed;
                if (status == 206 || status == 416 || received > job.size) {
                    // A range other than the one asked for, or more bytes than the file has: start it over.
                    output.close();
                    std::filesystem::remove(partial, code);
                    failed.retry = true;
                    return failed;
                }
                // A refusal (403, 404) will not change by asking again; the network or a busy server may.
                // Bytes already written stay for the retry to resume from.
                failed.retry = status == 0 || accepted || status >= 500;
                if (status == 503) {
                    // Clamped while parsing: the header is the server's, and a large value must not overflow.
                    const long long seconds = std::strtoll(retryAfter.c_str(), nullptr, 10);
                    failed.retryAfterMs     = static_cast<int>(std::clamp<long long>(seconds, 0, kMaxServerDelayMs / 1000) * 1000);
                }
                return failed;
            }
        }

        if (received != job.size || !Utils::Crypto::ConstantTimeEquals(Utils::Crypto::Sha256FileHex(partial.string()), job.sha256)) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("'{}' does not match the hash the server announced{}", job.fileName, resumed ? " (resumed); fetching it whole" : "");
            std::filesystem::remove(partial, code);
            Attempt corrupted;
            corrupted.retry = true;
            return corrupted;
        }
        if (resumed) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Resumed '{}' from byte {}", job.fileName, offset);
        }

        std::lock_guard lock(transfer.mutex);
        if (transfer.cancel.load()) {
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
