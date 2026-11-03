/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Framework::Integrations::Server {
    // Declares GET /files/<name> on the framework's webserver and serves the client resource packages
    // and asset paks there, so downloads stay off the game connection. Only published names are
    // served: the staging directory beside them holds the pak builder's indexes too. A request must carry a
    // token the server handed to a connected peer (kTokenHeader).
    //
    // At most maxTransfers transfers are open at once in all, over at most that many peers and at most
    // kMaxTransfersPerClient connections each, so the webserver's other routes keep workers. A peer past the cap is told to retry (503,
    // Retry-After) and where it stands (kQueueHeader): slots are handed out in the order peers first
    // asked, so a join burst drains fairly instead of racing. A peer that stops asking leaves the queue.
    class AssetFileServer final {
      public:
        static constexpr const char *kTokenHeader = "X-Download-Token";
        // How many peers stood ahead of the refused one when it asked; 0 means it is next.
        static constexpr const char *kQueueHeader      = "X-Queue-Position";
        static constexpr size_t kMaxTransfersPerClient = 4;
        // Seconds a refused peer is told to wait; its place in the queue is kept for a few of them.
        static constexpr int kRetryAfterSeconds = 2;

        struct Admission {
            bool granted         = false;
            size_t queuePosition = 0;
        };

        // maxTransfers is the worker budget: how many transfers may be open at once in all, and the most
        // peers that may hold a slot.
        bool Register(size_t maxTransfers);

        // Serves these names, and beside each the chunk list the client assembles it from
        // (<name>.chunks, written next to the file when missing or older than it). A mirror copies
        // both. Chunk lists are small and answered outside the transfer slots.
        void Publish(std::unordered_map<std::string, std::filesystem::path> files);

        // A fresh token for this peer, replacing any it held; empty if no randomness was available.
        std::string Grant(uint64_t peer);
        void Revoke(uint64_t peer);

        // Asks for a transfer slot on behalf of |token|; a granted one is held until Release.
        Admission Acquire(const std::string &token);
        void Release(const std::string &token);

        // Peers with a transfer open now, and peers refused and still asking.
        size_t GetActiveClients();
        size_t GetQueuedClients();

      private:
        bool IsGranted(const std::string &token);
        void ExpireQueue(std::chrono::steady_clock::time_point now);

        std::mutex _filesMutex;
        std::unordered_map<std::string, std::filesystem::path> _files;

        std::mutex _tokensMutex;
        std::unordered_map<uint64_t, std::string> _tokens;

        struct Slot {
            size_t open = 0;                                 // connections transferring now
            std::chrono::steady_clock::time_point idleSince; // when open last reached zero
        };

        size_t _maxClients   = 1;
        size_t _maxTransfers = 1;
        std::mutex _slotsMutex;
        // The peers holding a client slot: transferring, or just done and kept briefly for their next file.
        std::unordered_map<std::string, Slot> _slots;
        // Tokens refused for load, oldest first, with when each last asked.
        std::deque<std::pair<std::string, std::chrono::steady_clock::time_point>> _queue;
    };
} // namespace Framework::Integrations::Server
