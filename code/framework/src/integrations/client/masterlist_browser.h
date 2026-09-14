/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
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
    // One entry of the masterlist's server list.
    struct MasterlistServer {
        std::string name;
        std::string host; // dotted-quad or hostname
        uint16_t port = 0;
        std::string gameMode;
        std::string version;
        std::string localization;
        int32_t currentPlayers = 0;
        int32_t maxPlayers     = 0;

        [[nodiscard]] std::string Address() const {
            return host + ":" + std::to_string(port);
        }
    };

    // Reads the masterlist's server list, for a client showing a server browser. The other half of
    // Services::MasterlistConnector, which only pushes: a server announces itself there, and this
    // reads the result back. One masterlist serves every MafiaHub game, so `gameMode` filters it.
    //
    // The fetch runs on a worker thread and Poll() hands a finished one to the thread that owns the
    // UI, returning true once per refresh so a browser rebuilds on that edge rather than re-reading
    // a list that has not changed.
    class MasterlistBrowser final {
      public:
        enum class Status {
            Idle,
            Fetching,
            Ready,
            Failed
        };

        MasterlistBrowser() = default;
        ~MasterlistBrowser();

        MasterlistBrowser(const MasterlistBrowser &)            = delete;
        MasterlistBrowser &operator=(const MasterlistBrowser &) = delete;

        // `masterlistUrl` is the scheme + host, e.g. "https://masterlist.mafiahub.dev".
        // `gameMode` matches the servers' reported gamemode; empty lists every game.
        bool Init(const std::string &masterlistUrl, const std::string &gameMode);

        // Start a fetch unless one is already in flight. Callable from any thread, but not
        // concurrently with Shutdown().
        void Refresh();

        // Pick up a completed fetch. True (and fills `out`) once per completed refresh.
        bool Poll(std::vector<MasterlistServer> &out);

        [[nodiscard]] Status State() const;

        // Why the last fetch failed; empty unless State() == Failed.
        [[nodiscard]] std::string LastError() const;

        // Join the worker, blocking until an in-flight fetch finishes or times out. Idempotent;
        // call it from the owner, not concurrently with Refresh().
        void Shutdown();

      private:
        void Fetch();

        std::string _masterlistUrl;
        std::string _gameMode;

        std::thread _worker;
        mutable std::mutex _mutex; // guards _results and _lastError
        std::vector<MasterlistServer> _results;
        std::string _lastError;

        std::atomic<bool> _running {false}; // a fetch is in flight
        std::atomic<bool> _pending {false}; // a finished fetch nobody has picked up yet
        std::atomic<Status> _status {Status::Idle};
    };
} // namespace Framework::Integrations::Client
