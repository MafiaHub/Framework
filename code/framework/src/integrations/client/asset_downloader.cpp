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
#include <utils/streamed_assets/transfer.h>

#include <httplib.h>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <thread>
#include <unordered_map>

namespace Framework::Integrations::Client {
    namespace {
        using Utils::StreamedAssets::Chunk;

        constexpr time_t kConnectTimeoutSeconds = 10;
        constexpr time_t kReadTimeoutSeconds    = 30;
        constexpr const char *kTokenHeader      = "X-Download-Token";
        constexpr int kMaxServerDelayMs         = 30000;
        constexpr int kBusyDelayMs              = 2000; // a 503 without Retry-After
        constexpr const char *kQueueHeader      = "X-Queue-Position";
        // The longest line of a chunk list, with room: bounds what is read of one before it is parsed.
        constexpr uint64_t kChunkListLineBytes = 80;

        double MiB(uint64_t bytes) {
            return static_cast<double>(bytes) / (1024.0 * 1024.0);
        }

        double MiBPerSecond(uint64_t bytes, long long milliseconds) {
            return milliseconds > 0 ? MiB(bytes) * 1000.0 / static_cast<double>(milliseconds) : 0.0;
        }

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

        void ConfigureClient(httplib::Client &client) {
            client.set_connection_timeout(kConnectTimeoutSeconds, 0);
            client.set_read_timeout(kReadTimeoutSeconds, 0);
        }

        httplib::Headers HeadersFor(const AssetDownloader::Source &source) {
            httplib::Headers headers;
            if (!source.token.empty()) {
                headers.emplace(kTokenHeader, source.token);
            }
            return headers;
        }

        // A file already in the cache and matching is not fetched: its verification note spares the
        // hash when the file has not been touched since it was last checked.
        bool IsCached(const AssetDownloader::Job &job) {
            std::error_code code;
            if (!std::filesystem::exists(job.path, code)) {
                return false;
            }
            if (Utils::StreamedAssets::IsVerified(job.path, job.sha256)) {
                return true;
            }
            const auto size = std::filesystem::file_size(job.path, code);
            if (!code && size == job.size && Utils::Crypto::ConstantTimeEquals(Utils::Crypto::Sha256FileHex(job.path), job.sha256)) {
                Utils::StreamedAssets::WriteVerifiedNote(job.path, job.sha256);
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

        // What the assembly note records of a chunk in place: where it is, how long, what it hashes
        // to, so that a chunk list of another shape (a mirror's stale one) cannot claim it.
        std::string NoteLine(const Chunk &chunk) {
            return std::to_string(chunk.offset) + ' ' + std::to_string(chunk.length) + ' ' + chunk.sha256;
        }

        std::set<std::string> ReadAssemblyNote(const std::string &path) {
            std::set<std::string> lines;
            std::ifstream note(path);
            std::string line;
            while (std::getline(note, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    lines.insert(line);
                }
            }
            return lines;
        }

        int RetryAfterMs(const std::string &header) {
            // Clamped while parsing: the header is the server's, and a large value must not overflow.
            const long long seconds = std::strtoll(header.c_str(), nullptr, 10);
            return static_cast<int>(std::clamp<long long>(seconds, 0, kMaxServerDelayMs / 1000) * 1000);
        }

        bool ChunkMatches(const char *data, const Chunk &chunk) {
            return Utils::Crypto::ConstantTimeEquals(Utils::Crypto::Sha256Hex(data, static_cast<size_t>(chunk.length)), chunk.sha256);
        }
    } // namespace

    void AssetDownloader::Transfer::Register(httplib::Client *client) {
        std::lock_guard lock(mutex);
        clients.push_back(client);
    }

    void AssetDownloader::Transfer::Unregister(httplib::Client *client) {
        std::lock_guard lock(mutex);
        clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end());
    }

    void AssetDownloader::Transfer::NoteAnswer(int status, const std::string &queue, bool accepted) {
        Touch();
        if (status == 503) {
            const long long position = queue.empty() ? -1 : std::clamp<long long>(std::strtoll(queue.c_str(), nullptr, 10), 0, std::numeric_limits<int32_t>::max());
            queuePosition.store(static_cast<int32_t>(position), std::memory_order_relaxed);
            waitingForSlot.store(true, std::memory_order_relaxed);
        }
        else if (accepted) {
            waitingForSlot.store(false, std::memory_order_relaxed);
            queuePosition.store(-1, std::memory_order_relaxed);
        }
    }

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
            for (httplib::Client *client : _transfer->clients) {
                client->stop();
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

    bool AssetDownloader::IsWaitingForSlot() const {
        return _transfer && _transfer->waitingForSlot.load(std::memory_order_relaxed);
    }

    int32_t AssetDownloader::GetQueuePosition() const {
        return _transfer ? _transfer->queuePosition.load(std::memory_order_relaxed) : -1;
    }

    uint32_t AssetDownloader::GetCuts() const {
        return _transfer ? _transfer->cuts.load(std::memory_order_relaxed) : 0;
    }

    uint64_t AssetDownloader::GetBytesReused() const {
        return _transfer ? _transfer->reused.load(std::memory_order_relaxed) : 0;
    }

    int64_t AssetDownloader::GetLastActivityMs() const {
        return _transfer ? _transfer->lastActivityMs.load(std::memory_order_relaxed) : 0;
    }

    int64_t AssetDownloader::NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void AssetDownloader::Run(std::shared_ptr<Transfer> transfer, std::vector<Source> sources, std::vector<Job> jobs) {
        const auto started   = std::chrono::steady_clock::now();
        uint64_t cachedBytes = 0;
        bool success         = true;
        transfer->Touch();
        for (const auto &job : jobs) {
            if (transfer->cancel.load()) {
                return;
            }
            {
                std::lock_guard lock(transfer->mutex);
                transfer->current = job.fileName;
            }
            // Hashing a cached pak is work, not silence; a pak hashes well inside the stall watch.
            transfer->Touch();
            if (IsCached(job)) {
                cachedBytes += job.size;
                transfer->bytes.fetch_add(job.size, std::memory_order_relaxed);
            }
            else {
                // The next source picks up the partial the last one left. Older files are searched for
                // the job's chunks once: what they could give is in place after that.
                bool reuseSearched = false;
                bool fetched       = false;
                for (size_t i = 0; i < sources.size() && !fetched && !transfer->cancel.load(); ++i) {
                    fetched = FetchWithRetries(*transfer, sources[i], job, reuseSearched);
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
        const auto elapsed     = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        const uint64_t counted = transfer->bytes.load(std::memory_order_relaxed);
        const uint64_t reused  = transfer->reused.load(std::memory_order_relaxed);
        const uint64_t fetched = counted - std::min(cachedBytes + reused, counted);
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)
            ->info("Asset transfer {}: {} of {} file(s), {:.1f} MiB fetched, {:.1f} MiB reused from older files and {:.1f} MiB already cached in {:.1f} s ({:.2f} MiB/s), {} cut(s)", success ? "complete" : "failed", transfer->filesDone.load(std::memory_order_relaxed),
                transfer->filesTotal, MiB(fetched), MiB(reused), MiB(cachedBytes), static_cast<double>(elapsed) / 1000.0, MiBPerSecond(fetched, elapsed), transfer->cuts.load(std::memory_order_relaxed));
        transfer->success.store(success);
        transfer->done.store(true);
    }

    std::string AssetDownloader::PartialPath(const Job &job) {
        return job.path + "." + job.sha256.substr(0, 16) + ".part";
    }

    std::string AssetDownloader::AssemblyNotePath(const Job &job) {
        return PartialPath(job) + ".done";
    }

    bool AssetDownloader::WaitBeforeRetry(Transfer &transfer, const Source &source, const Job &job, const Attempt &result, RetryState &state) {
        if (transfer.cancel.load()) {
            return false;
        }
        const RetryPolicy &policy = transfer.retry;
        if (result.busy) {
            // A full server is asked again after the pause it names; waiting is not failing. The
            // wait is bounded only by the policy, so a server that never frees a slot does not
            // park the client for good.
            const int64_t now = NowMs();
            if (state.queuedSinceMs == 0) {
                state.queuedSinceMs = now;
            }
            if (policy.queueWaitMs > 0 && now - state.queuedSinceMs >= policy.queueWaitMs) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("'{}' waited {} s for a download slot at {}; giving up", job.fileName, policy.queueWaitMs / 1000, source.url);
                return false;
            }
            const int delay        = result.retryAfterMs > 0 ? std::min(result.retryAfterMs, kMaxServerDelayMs) : kBusyDelayMs;
            const int32_t position = transfer.queuePosition.load(std::memory_order_relaxed);
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("{} is busy; asking for '{}' again in {} ms{}", source.url, job.fileName, delay, position >= 0 ? fmt::format(" ({} ahead in its queue)", position) : "");
            transfer.Touch();
            return WaitUnlessCancelled(transfer.cancel, delay);
        }
        state.queuedSinceMs = 0;
        if (!result.retry) {
            return false;
        }
        if (result.progressed) {
            // A transfer that was moving and got cut is a link event, not a failed request: it
            // resumes with a fresh budget.
            state.failuresWithoutProgress = 0;
            transfer.cuts.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            ++state.failuresWithoutProgress;
        }
        if (state.failuresWithoutProgress >= policy.attempts) {
            return false;
        }
        // A wait the server names is its own, bounded only so a hostile server cannot park the client.
        const int delay = result.retryAfterMs > 0 ? std::min(result.retryAfterMs, kMaxServerDelayMs) : std::min(policy.firstDelayMs << std::min(state.failuresWithoutProgress, 16), policy.maxDelayMs);
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Retrying '{}' in {} ms ({})", job.fileName, delay, result.progressed ? "resuming a cut transfer" : fmt::format("attempt {} of {}", state.failuresWithoutProgress + 1, policy.attempts));
        transfer.Touch();
        return WaitUnlessCancelled(transfer.cancel, delay);
    }

    bool AssetDownloader::FetchWithRetries(Transfer &transfer, const Source &source, const Job &job, bool &reuseSearched) {
        const auto started          = std::chrono::steady_clock::now();
        const uint32_t cutsBefore   = transfer.cuts.load(std::memory_order_relaxed);
        const uint64_t reusedBefore = transfer.reused.load(std::memory_order_relaxed);
        RetryState retries;
        std::vector<Chunk> chunks = FetchChunkList(transfer, source, job);
        for (;;) {
            const uint64_t before = transfer.bytes.load(std::memory_order_relaxed);
            transfer.Touch();
            Attempt result = chunks.empty() ? Fetch(transfer, source, job) : FetchChunked(transfer, source, job, chunks, reuseSearched);
            if (result.done) {
                const auto elapsed    = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                const uint32_t cuts   = transfer.cuts.load(std::memory_order_relaxed) - cutsBefore;
                const uint64_t reused = transfer.reused.load(std::memory_order_relaxed) - reusedBefore;
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)
                    ->info("Downloaded '{}' ({:.1f} MiB) from {} in {:.1f} s ({:.2f} MiB/s{}{})", job.fileName, MiB(job.size), source.url, static_cast<double>(elapsed) / 1000.0, MiBPerSecond(job.size - reused, elapsed),
                        reused > 0 ? fmt::format(", {:.1f} MiB reused from older files", MiB(reused)) : "", cuts > 0 ? fmt::format(", resumed after {} cut(s)", cuts) : "");
                return true;
            }
            // The attempt's bytes come off the progress; the next one counts what it resumes from again.
            transfer.bytes.fetch_sub(transfer.bytes.load(std::memory_order_relaxed) - before, std::memory_order_relaxed);
            if (result.fallback && !transfer.cancel.load()) {
                // The source serves the file but not its ranges, or its list is not the file's: it is
                // read whole from here on, at once.
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("{} does not serve '{}' by chunks; fetching it whole", source.url, job.fileName);
                chunks.clear();
                continue;
            }
            if (!WaitBeforeRetry(transfer, source, job, result, retries)) {
                return false;
            }
        }
    }

    std::vector<Chunk> AssetDownloader::FetchChunkList(Transfer &transfer, const Source &source, const Job &job) {
        std::vector<Chunk> chunks;
        std::string origin;
        std::string base;
        if (job.size <= Utils::StreamedAssets::kChunkSize || !SplitUrl(source.url, origin, base)) {
            // A file no longer than a chunk is one request either way, and the plain path resumes it.
            return chunks;
        }
        const std::string path   = base + "/" + Utils::StreamedAssets::ChunkListFileName(job.fileName);
        const uint64_t mostBytes = Utils::StreamedAssets::MaxChunkCount(job.size) * kChunkListLineBytes;
        RetryState retries;
        // A list a busy source or a flaky link withholds is asked for again like the file would be:
        // read whole, the file would lose what its chunks could have taken from older copies.
        for (;;) {
            if (transfer.cancel.load()) {
                return {};
            }
            httplib::Client client(origin);
            ConfigureClient(client);
            transfer.Register(&client);
            transfer.Touch();
            Answer answer;
            std::string body;
            const auto res = client.Get(
                path, HeadersFor(source),
                [&](const httplib::Response &response) {
                    answer.status        = response.status;
                    answer.retryAfter    = response.get_header_value("Retry-After");
                    answer.queuePosition = response.get_header_value(kQueueHeader);
                    transfer.NoteAnswer(answer.status, answer.queuePosition, answer.status == 200);
                    return true;
                },
                [&](const char *data, size_t length) {
                    if (transfer.cancel.load() || body.size() + length > mostBytes) {
                        return false;
                    }
                    body.append(data, length);
                    transfer.Touch();
                    return true;
                });
            transfer.Unregister(&client);
            if (transfer.cancel.load()) {
                return {};
            }
            if (res && answer.status == 200) {
                if (!Utils::StreamedAssets::ParseChunkList(body, job.size, chunks)) {
                    Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("{} describes '{}' with a chunk list that does not fit the file; fetching it whole", source.url, job.fileName);
                    chunks.clear();
                }
                return chunks;
            }
            Attempt failed;
            failed.busy  = answer.status == 503;
            failed.retry = failed.busy || answer.status == 0 || answer.status >= 500;
            if (failed.busy) {
                failed.retryAfterMs = RetryAfterMs(answer.retryAfter);
            }
            // A source without the list (404) serves the file whole; one that cannot be reached or is
            // busy is asked again, and the file is read whole once that is given up on.
            if (!failed.retry || !WaitBeforeRetry(transfer, source, job, failed, retries)) {
                return {};
            }
        }
    }

    AssetDownloader::Attempt AssetDownloader::Fetch(Transfer &transfer, const Source &source, const Job &job) {
        const std::filesystem::path partial = PartialPath(job);
        std::error_code code;
        // A partial of a chunked assembly is not a prefix of the file; it is not resumed as one.
        std::filesystem::remove(AssemblyNotePath(job), code);

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
        Answer answer;
        {
            std::ofstream output;
            httplib::Client client(origin);
            ConfigureClient(client);
            httplib::Headers headers = HeadersFor(source);
            if (offset > 0) {
                headers.emplace("Range", "bytes=" + std::to_string(offset) + "-");
            }
            {
                std::lock_guard lock(transfer.mutex);
                if (transfer.cancel.load()) {
                    return {};
                }
                transfer.clients.push_back(&client);
            }
            // The status is judged before any body is read: an error page must not reach the partial file or
            // the progress, nor end the request early and lose the status with it. A 206 continues the
            // partial from where it ends; a 200 is the whole file again.
            const auto res = client.Get(
                base + "/" + job.fileName, headers,
                [&](const httplib::Response &response) {
                    answer.status        = response.status;
                    answer.retryAfter    = response.get_header_value("Retry-After");
                    answer.queuePosition = response.get_header_value(kQueueHeader);
                    if (answer.status == 206 && offset > 0 && response.get_header_value("Content-Range").rfind("bytes " + std::to_string(offset) + "-", 0) == 0) {
                        resumed  = true;
                        received = offset;
                        transfer.bytes.fetch_add(offset, std::memory_order_relaxed);
                        output.open(partial, std::ios::binary | std::ios::app);
                    }
                    else if (answer.status == 200) {
                        output.open(partial, std::ios::binary | std::ios::trunc);
                    }
                    transfer.NoteAnswer(answer.status, answer.queuePosition, output.is_open());
                    if (answer.status != 200 && !resumed) {
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
                    transfer.Touch();
                    return output.good();
                });
            transfer.Unregister(&client);
            if (transfer.cancel.load()) {
                // Whatever arrived stays for the next transfer to resume from.
                return {};
            }
            const bool accepted = answer.status == 200 || resumed;
            if (!res || !accepted) {
                Attempt failed;
                // Bytes of the file arrived: past the partial on a resume, any at all on a fresh answer.
                failed.progressed = received > (resumed ? offset : 0);
                if (answer.status == 503) {
                    failed.retryAfterMs = RetryAfterMs(answer.retryAfter);
                    failed.busy         = true;
                    failed.retry        = true;
                    return failed;
                }
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)
                    ->error("Downloading '{}' from {} failed{}: {}", job.fileName, source.url, failed.progressed ? fmt::format(" after {:.1f} MiB", MiB(received - (resumed ? offset : 0))) : "",
                        answer.status != 0 && !accepted ? "HTTP " + std::to_string(answer.status) : httplib::to_string(res.error()));
                if ((answer.status == 206 && !resumed) || answer.status == 416 || received > job.size) {
                    // A range other than the one asked for, or more bytes than the file has: start it over.
                    // Nothing of it is kept, so nothing of it counts as progress.
                    output.close();
                    std::filesystem::remove(partial, code);
                    failed.progressed = false;
                    failed.retry      = true;
                    return failed;
                }
                // A refusal (403, 404) will not change by asking again; the network or a busy server may.
                // Bytes already written stay for the retry to resume from.
                failed.retry = answer.status == 0 || accepted || answer.status >= 500;
                return failed;
            }
        }

        if (received != job.size) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("'{}' ended {} bytes short; fetching it whole", job.fileName, job.size - received);
            std::filesystem::remove(partial, code);
            Attempt corrupted;
            corrupted.retry = true;
            return corrupted;
        }
        if (!VerifyFile(transfer, job, partial.string())) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("'{}' does not match the hash the server announced{}", job.fileName, resumed ? " (resumed); fetching it whole" : "");
            std::filesystem::remove(partial, code);
            Attempt corrupted;
            corrupted.retry = true;
            return corrupted;
        }
        if (resumed) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Resumed '{}'", job.fileName);
        }
        return Install(transfer, job, partial.string());
    }

    bool AssetDownloader::VerifyFile(Transfer &transfer, const Job &job, const std::string &partial) {
        transfer.Touch();
        return Utils::Crypto::ConstantTimeEquals(Utils::Crypto::Sha256FileHex(partial), job.sha256);
    }

    AssetDownloader::Attempt AssetDownloader::Install(Transfer &transfer, const Job &job, const std::string &partial) {
        std::error_code code;
        std::lock_guard lock(transfer.mutex);
        if (transfer.cancel.load()) {
            return {};
        }
        Utils::StreamedAssets::RemoveVerifiedNote(job.path);
        std::filesystem::remove(job.path, code);
        std::filesystem::rename(partial, job.path, code);
        if (code) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not move '{}' into the cache: {}", job.fileName, code.message());
            std::filesystem::remove(partial, code);
            return {};
        }
        std::filesystem::remove(AssemblyNotePath(job), code);
        Utils::StreamedAssets::WriteVerifiedNote(job.path, job.sha256);
        Attempt done;
        done.done = true;
        return done;
    }

    AssetDownloader::Attempt AssetDownloader::FetchRun(Transfer &transfer, httplib::Client &client, const std::string &path, const Source &source, const Job &job, const std::vector<Chunk> &chunks, size_t first, size_t last, std::vector<char> &data) {
        const uint64_t offset = chunks[first].offset;
        const uint64_t length = chunks[last].offset + chunks[last].length - offset;
        data.clear();
        data.reserve(static_cast<size_t>(length));

        httplib::Headers headers = HeadersFor(source);
        headers.emplace("Range", "bytes=" + std::to_string(offset) + "-" + std::to_string(offset + length - 1));
        Answer answer;
        bool accepted  = false;
        bool overrun   = false;
        const auto res = client.Get(
            path, headers,
            [&](const httplib::Response &response) {
                answer.status        = response.status;
                answer.retryAfter    = response.get_header_value("Retry-After");
                answer.queuePosition = response.get_header_value(kQueueHeader);
                accepted             = answer.status == 206 && response.get_header_value("Content-Range").rfind("bytes " + std::to_string(offset) + "-", 0) == 0;
                transfer.NoteAnswer(answer.status, answer.queuePosition, accepted);
                return accepted;
            },
            [&](const char *bytes, size_t count) {
                if (transfer.cancel.load()) {
                    return false;
                }
                if (data.size() + count > length) {
                    overrun = true;
                    return false;
                }
                data.insert(data.end(), bytes, bytes + count);
                transfer.bytes.fetch_add(count, std::memory_order_relaxed);
                transfer.Touch();
                return true;
            });
        if (transfer.cancel.load()) {
            return {};
        }
        if (res && accepted && data.size() == length) {
            Attempt done;
            done.done = true;
            return done;
        }
        // Whether what arrived counts as progress is the caller's: it keeps the whole chunks.
        Attempt failed;
        if (answer.status == 503) {
            failed.retryAfterMs = RetryAfterMs(answer.retryAfter);
            failed.busy         = true;
            failed.retry        = true;
            return failed;
        }
        if (answer.status == 200 || answer.status == 416) {
            // The source serves the file but not its ranges: the caller falls back to reading it whole.
            failed.fallback = true;
            return failed;
        }
        std::string reason;
        if (answer.status != 0 && !accepted) {
            reason = "HTTP " + std::to_string(answer.status);
        }
        else if (overrun) {
            reason = "more bytes came than were asked for";
        }
        else if (res) {
            reason = "ended " + std::to_string(length - data.size()) + " bytes short";
        }
        else {
            reason = httplib::to_string(res.error());
        }
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Chunks {}-{} of '{}' from {} failed: {}", first, last, job.fileName, source.url, reason);
        failed.retry = answer.status == 0 || (accepted && !overrun) || answer.status >= 500;
        return failed;
    }

    AssetDownloader::Attempt AssetDownloader::FetchChunked(Transfer &transfer, const Source &source, const Job &job, const std::vector<Chunk> &chunks, bool &reuseSearched) {
        const std::string partial = PartialPath(job);
        const std::string note    = AssemblyNotePath(job);
        std::error_code code;

        // The assembly lives in a partial of the file's full size, with the note naming the chunks in
        // place. A partial of another shape is an unfinished whole-file read, and starts over here.
        std::vector<bool> done(chunks.size(), false);
        bool resumed = false;
        if (std::filesystem::exists(partial, code) && !code && std::filesystem::file_size(partial, code) == job.size && std::filesystem::exists(note, code)) {
            const std::set<std::string> noted = ReadAssemblyNote(note);
            for (size_t i = 0; i < chunks.size(); ++i) {
                if (noted.count(NoteLine(chunks[i])) != 0) {
                    done[i] = true;
                    resumed = true;
                    transfer.bytes.fetch_add(chunks[i].length, std::memory_order_relaxed);
                }
            }
        }
        else {
            std::filesystem::remove(partial, code);
            std::filesystem::remove(note, code);
            std::ofstream(partial, std::ios::binary | std::ios::trunc).close();
            std::filesystem::resize_file(partial, job.size, code);
            if (code) {
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not reserve '{}' for '{}': {}", partial, job.fileName, code.message());
                return {};
            }
        }
        std::fstream output(partial, std::ios::binary | std::ios::in | std::ios::out);
        std::ofstream noteOut(note, std::ios::app);
        if (!output.is_open() || !noteOut.is_open()) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not open '{}' for writing", partial);
            return {};
        }
        std::vector<size_t> pending;
        for (size_t i = 0; i < chunks.size(); ++i) {
            if (!done[i]) {
                pending.push_back(i);
            }
        }

        // Chunks an older file already holds are copied, never fetched. The older files are hashed
        // once for the job: what they could give is in place after this, whatever source follows.
        if (!pending.empty() && !reuseSearched) {
            reuseSearched = true;
            std::unordered_map<std::string, std::pair<std::string, uint64_t>> known;
            for (const std::string &older : job.reuse) {
                // The stale copy at job.path is read here and replaced only once the assembly verifies.
                if (older == partial || !std::filesystem::exists(older, code)) {
                    continue;
                }
                transfer.Touch();
                for (const Chunk &theirs : Utils::StreamedAssets::HashFileChunks(older)) {
                    known.emplace(theirs.sha256, std::make_pair(older, theirs.offset));
                }
            }
            std::vector<size_t> still;
            std::vector<char> buffer;
            std::ifstream older;
            std::string olderPath;
            for (const size_t index : pending) {
                if (transfer.cancel.load()) {
                    return {};
                }
                const Chunk &chunk = chunks[index];
                const auto have    = known.find(chunk.sha256);
                if (have == known.end()) {
                    still.push_back(index);
                    continue;
                }
                if (olderPath != have->second.first) {
                    olderPath = have->second.first;
                    older.close();
                    older.clear();
                    older.open(olderPath, std::ios::binary);
                }
                buffer.resize(static_cast<size_t>(chunk.length));
                older.clear();
                older.seekg(static_cast<std::streamoff>(have->second.second));
                older.read(buffer.data(), static_cast<std::streamsize>(chunk.length));
                if (static_cast<uint64_t>(older.gcount()) != chunk.length || !ChunkMatches(buffer.data(), chunk)) {
                    // The older file changed under the map; this chunk comes from the network instead.
                    still.push_back(index);
                    continue;
                }
                output.seekp(static_cast<std::streamoff>(chunk.offset));
                output.write(buffer.data(), static_cast<std::streamsize>(chunk.length));
                noteOut << NoteLine(chunk) << '\n';
                transfer.bytes.fetch_add(chunk.length, std::memory_order_relaxed);
                transfer.reused.fetch_add(chunk.length, std::memory_order_relaxed);
                transfer.Touch();
            }
            output.flush();
            noteOut.flush();
            pending.swap(still);
        }

        // The rest over several connections, a run of neighbouring chunks per request, up to
        // kChunkSize at a time; each run keeps its own retry budget.
        struct Run {
            size_t first = 0;
            size_t last  = 0;
        };
        std::vector<Run> runs;
        for (const size_t index : pending) {
            const Chunk &chunk = chunks[index];
            if (!runs.empty() && runs.back().last + 1 == index && chunk.offset + chunk.length - chunks[runs.back().first].offset <= Utils::StreamedAssets::kChunkSize) {
                runs.back().last = index;
            }
            else {
                runs.push_back({index, index});
            }
        }
        std::string origin;
        std::string base;
        if (!runs.empty() && !SplitUrl(source.url, origin, base)) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Cannot download from '{}': not an http(s) URL", source.url);
            return {};
        }
        const std::string path = base + "/" + job.fileName;
        std::mutex shared;
        size_t next = 0;
        bool failed = false;
        Attempt failure;
        const int workers = static_cast<int>(std::min<size_t>(static_cast<size_t>(std::max(transfer.retry.connections, 1)), runs.size()));
        const auto worker = [&] {
            // One connection for every run the worker fetches: a handshake per run, TLS on a
            // mirror, would cost a round trip or two every few MiB.
            httplib::Client client(origin);
            ConfigureClient(client);
            client.set_keep_alive(true);
            {
                std::lock_guard lock(transfer.mutex);
                if (transfer.cancel.load()) {
                    return;
                }
                transfer.clients.push_back(&client);
            }
            // Every path out of the loop: a request blocked on the network is stopped through the
            // registration, so it must outlive the loop and no more.
            struct Unregister {
                Transfer &transfer;
                httplib::Client &client;
                ~Unregister() {
                    transfer.Unregister(&client);
                }
            } const unregister {transfer, client};
            std::vector<char> data;
            for (;;) {
                Run run;
                {
                    std::lock_guard lock(shared);
                    if (failed || next >= runs.size()) {
                        return;
                    }
                    run = runs[next++];
                }
                RetryState retries;
                for (;;) {
                    if (transfer.cancel.load()) {
                        return;
                    }
                    Attempt result = FetchRun(transfer, client, path, source, job, chunks, run.first, run.last, data);
                    // The whole chunks that arrived and verify are kept whether the rest came or not: a
                    // cut costs the chunk it fell in, and the run goes on from there.
                    size_t kept        = 0;
                    uint64_t keptBytes = 0;
                    {
                        std::lock_guard lock(shared);
                        for (size_t i = run.first; i <= run.last; ++i) {
                            const Chunk &chunk = chunks[i];
                            const size_t at    = static_cast<size_t>(chunk.offset - chunks[run.first].offset);
                            if (at + chunk.length > data.size() || !ChunkMatches(data.data() + at, chunk)) {
                                break;
                            }
                            output.seekp(static_cast<std::streamoff>(chunk.offset));
                            output.write(data.data() + at, static_cast<std::streamsize>(chunk.length));
                            noteOut << NoteLine(chunk) << '\n';
                            ++kept;
                            keptBytes += chunk.length;
                        }
                        if (kept > 0) {
                            output.flush();
                            noteOut.flush();
                        }
                    }
                    // Only this attempt's bytes, and only the ones not kept: other workers move the counter too.
                    transfer.bytes.fetch_sub(data.size() - keptBytes, std::memory_order_relaxed);
                    if (kept == run.last - run.first + 1) {
                        break;
                    }
                    run.first += kept;
                    if (result.done) {
                        // Every byte came and one chunk is not what the list says: the source's bytes are
                        // not the file's, which asking again may or may not mend.
                        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Chunk {} of '{}' from {} does not match its announced hash", run.first, job.fileName, source.url);
                        result       = {};
                        result.retry = true;
                    }
                    result.progressed = kept > 0;
                    if (result.fallback || !WaitBeforeRetry(transfer, source, job, result, retries)) {
                        std::lock_guard lock(shared);
                        if (!failed) {
                            failed  = true;
                            failure = result;
                        }
                        return;
                    }
                }
            }
        };
        std::vector<std::thread> threads;
        for (int i = 0; i < workers; ++i) {
            threads.emplace_back(worker);
        }
        for (std::thread &thread : threads) {
            thread.join();
        }
        output.close();
        noteOut.close();
        if (transfer.cancel.load()) {
            return {};
        }
        if (failed) {
            // The chunks in place stay for the next transfer. A worker that gave up has already waited
            // out its budget or its queue, so this source is done with; one that found no ranges hands
            // the file to the whole-file path.
            Attempt result;
            result.fallback = failure.fallback;
            result.retry    = false;
            return result;
        }
        if (!VerifyFile(transfer, job, partial)) {
            return RepairAssembly(transfer, job, chunks, partial, note);
        }
        if (resumed) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Resumed '{}'", job.fileName);
        }
        return Install(transfer, job, partial);
    }

    AssetDownloader::Attempt AssetDownloader::RepairAssembly(Transfer &transfer, const Job &job, const std::vector<Chunk> &chunks, const std::string &partial, const std::string &note) {
        // Every chunk verified as it was put in place, so one is not as written any more: a crash
        // between the data and the note, a disk fault. Those come again from the network. If every
        // chunk still is as the list says, the list is not the file's, and the file is read whole.
        std::error_code code;
        std::ifstream in(partial, std::ios::binary);
        std::vector<char> buffer;
        std::string good;
        size_t bad = 0;
        for (const Chunk &chunk : chunks) {
            buffer.resize(static_cast<size_t>(chunk.length));
            in.clear();
            in.seekg(static_cast<std::streamoff>(chunk.offset));
            in.read(buffer.data(), static_cast<std::streamsize>(chunk.length));
            if (static_cast<uint64_t>(in.gcount()) != chunk.length || !ChunkMatches(buffer.data(), chunk)) {
                ++bad;
                continue;
            }
            good += NoteLine(chunk);
            good += '\n';
        }
        transfer.Touch();
        if (bad == 0) {
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("'{}' assembled from its chunk list does not match the hash the server announced; fetching it whole", job.fileName);
            std::filesystem::remove(partial, code);
            std::filesystem::remove(note, code);
            Attempt mislisted;
            mislisted.fallback = true;
            return mislisted;
        }
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("'{}' does not match the hash the server announced; {} chunk(s) in place are not as written and come again", job.fileName, bad);
        std::ofstream(note, std::ios::trunc) << good;
        Attempt corrupted;
        corrupted.retry = true;
        return corrupted;
    }
} // namespace Framework::Integrations::Client
