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

namespace Framework::Utils::StreamedAssets {
    struct Chunk;
} // namespace Framework::Utils::StreamedAssets

namespace Framework::Integrations::Client {
    // Fetches a server's resource packages and asset paks over HTTP, off the main thread. A file is
    // only written into the cache once its SHA-256 matches the one the server announced; one already
    // there and matching is not fetched again. Each file is asked of the sources in order.
    //
    // A file the server describes chunk by chunk (<name>.chunks, see utils/streamed_assets/transfer.h)
    // is assembled: chunks an older copy of the file already holds are copied from it, the rest are
    // fetched by Range, neighbouring chunks in one request, over several connections at once, and the
    // assembly survives a cut or a restart with every whole chunk it had. A server or mirror without
    // chunk lists is read whole, resumable from the bytes already received.
    //
    // A server that answers 503 is busy, not broken: the request is asked again after the pause the
    // server names, for as long as the policy allows, without counting against the attempts. Only a
    // request that moved no bytes counts as a failed attempt; a transfer cut mid-way resumes with a
    // fresh budget, so a link that drops every few minutes still finishes a large file.
    class AssetDownloader final {
      public:
        struct Job {
            std::string fileName;
            std::string path;
            std::string sha256;
            uint64_t size = 0;
            // Older files whose chunks may be reused: a previous version of the same lane pak, the
            // stale copy this one replaces, the same file in another server's cache.
            std::vector<std::string> reuse;
        };

        // Where files are fetched: <url>/<fileName>. A non-empty token is sent with every request.
        struct Source {
            std::string url;
            std::string token;
        };

        // How often a file is asked for without receiving a byte before the transfer fails, and the
        // first pause between tries; each later pause doubles up to maxDelayMs. A pause the server
        // names (Retry-After) is used instead. queueWaitMs bounds how long one file waits on a busy
        // server (503) before the transfer fails; 0 waits for as long as the server keeps answering.
        // connections is how many chunks of one file are fetched at once.
        struct RetryPolicy {
            int attempts     = 5;
            int firstDelayMs = 1000;
            int maxDelayMs   = 30000;
            int queueWaitMs  = 0;
            int connections  = 4;
        };

        ~AssetDownloader();

        void Start(std::vector<Source> sources, std::vector<Job> jobs);

        // The partial a job's bytes collect in until they are verified; named by the expected hash so a
        // newer version of the file never resumes from an older one's bytes.
        static std::string PartialPath(const Job &job);
        // Beside the partial of a chunked assembly: one "<offset> <length> <sha256>" per chunk in place.
        static std::string AssemblyNotePath(const Job &job);

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

        // True while the server has told the transfer to wait for a slot (503) and it has not been
        // answered with bytes since.
        bool IsWaitingForSlot() const;
        // The place the server reported in its download queue (X-Queue-Position), -1 when none.
        int32_t GetQueuePosition() const;
        // How many transfers were interrupted after moving bytes and resumed.
        uint32_t GetCuts() const;
        // Bytes taken from older files on disk rather than from the network.
        uint64_t GetBytesReused() const;
        // When the transfer last did anything observable (asked, heard back, received bytes, began a
        // pause), in milliseconds of a steady clock; 0 before the first request.
        int64_t GetLastActivityMs() const;

        static int64_t NowMs();

      private:
        struct Transfer {
            std::atomic<bool> cancel {};
            std::atomic<bool> done {};
            std::atomic<bool> success {};
            std::atomic<uint64_t> bytes {};
            std::atomic<uint64_t> reused {};
            std::atomic<uint32_t> filesDone {};
            std::atomic<uint32_t> cuts {};
            std::atomic<bool> waitingForSlot {};
            std::atomic<int32_t> queuePosition {-1};
            std::atomic<int64_t> lastActivityMs {};
            uint64_t total {};
            uint32_t filesTotal {};
            RetryPolicy retry;
            std::mutex mutex;
            std::string current;
            // Every request in flight, so a cancel can stop each one.
            std::vector<httplib::Client *> clients;

            void Touch() {
                lastActivityMs.store(NowMs(), std::memory_order_relaxed);
            }
            void Register(httplib::Client *client);
            void Unregister(httplib::Client *client);
            // Reads an answer's status and queue header into the progress: a 503 is a wait for a slot,
            // an accepted one the end of it.
            void NoteAnswer(int status, const std::string &queuePosition, bool accepted);
        };

        // Whether a request succeeded, and if not, whether asking again could help and after how long.
        struct Attempt {
            bool done {};
            bool retry {};
            bool busy {};       // the server refused for load (503); not a failure of the file or the link
            bool progressed {}; // bytes of the file arrived before it ended
            bool fallback {};   // the source cannot serve the file by chunks; read it whole instead
            int retryAfterMs {};
        };

        // One request's answer: the status and what the pacing headers said.
        struct Answer {
            int status = 0;
            std::string retryAfter;
            std::string queuePosition;
        };

        static void Run(std::shared_ptr<Transfer> transfer, std::vector<Source> sources, std::vector<Job> jobs);
        // reuseSearched is the job's: whether its older files were already searched for chunks.
        static bool FetchWithRetries(Transfer &transfer, const Source &source, const Job &job, bool &reuseSearched);
        // What one request's retries have cost so far: attempts in a row without a byte, and since
        // when it has been waiting for a slot.
        struct RetryState {
            int failuresWithoutProgress = 0;
            int64_t queuedSinceMs       = 0;
        };

        // Waits out a refusal or a failure before the next try; false once the policy or a cancel ends it.
        static bool WaitBeforeRetry(Transfer &transfer, const Source &source, const Job &job, const Attempt &result, RetryState &state);
        static Attempt Fetch(Transfer &transfer, const Source &source, const Job &job);
        // The source's chunk list for the file, asked again while it is busy or unreachable; empty
        // when it has none or the file is no longer than one chunk.
        static std::vector<Utils::StreamedAssets::Chunk> FetchChunkList(Transfer &transfer, const Source &source, const Job &job);
        static Attempt FetchChunked(Transfer &transfer, const Source &source, const Job &job, const std::vector<Utils::StreamedAssets::Chunk> &chunks, bool &reuseSearched);
        // The chunks |first| to |last|, neighbours in the file, in one request over |client|, a
        // connection the worker keeps across the runs it fetches. Done when every byte came; |data|
        // holds whatever did, and the caller keeps the whole chunks of it either way.
        static Attempt FetchRun(Transfer &transfer, httplib::Client &client, const std::string &path, const Source &source, const Job &job, const std::vector<Utils::StreamedAssets::Chunk> &chunks, size_t first, size_t last, std::vector<char> &data);
        // An assembly that is complete but not the file announced: the chunks not as written come
        // again; if every one is, the list was not the file's and it is read whole.
        static Attempt RepairAssembly(Transfer &transfer, const Job &job, const std::vector<Utils::StreamedAssets::Chunk> &chunks, const std::string &partial, const std::string &note);
        static bool VerifyFile(Transfer &transfer, const Job &job, const std::string &partial);
        // Moves the verified partial into the cache and writes its note.
        static Attempt Install(Transfer &transfer, const Job &job, const std::string &partial);

        std::shared_ptr<Transfer> _transfer;
        RetryPolicy _retryPolicy;
        bool _reported {};
    };
} // namespace Framework::Integrations::Client
