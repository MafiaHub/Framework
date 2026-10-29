/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "gui/resources/resource_provider.h"

#include "channel.h"
#include "protocol.h"

#include <function2/function2.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Framework::GUI::Remote {
    // The client's answer to the host's fw:// requests, from the client's own providers and off the
    // game thread. A few workers take turns over every open request one chunk at a time, so a large
    // or slow body does not hold up the page's other requests. A request sends at most
    // Protocol::kResourceWindowBytes beyond what the host has acknowledged, so neither process ever
    // holds a whole body. Owned by the RemoteSession; the handlers run on its channel thread.
    class ResourceServer final {
      public:
        using RootLookup = fu2::function<std::shared_ptr<Resources::ResourceProvider>(const std::string &host) const>;

      private:
        struct Served {
            Protocol::ResourceRequest request;
            std::unique_ptr<Resources::ResourceStream> stream;
            // Next byte the stream reads; only the worker serving it touches stream and position.
            uint64_t position = 0;
            // Under _mutex.
            uint64_t seekTo = 0;
            uint32_t credit = Protocol::kResourceWindowBytes;
            bool scheduled  = true;
            bool cancelled  = false;
        };

        Channel &_channel;
        RootLookup _findRoot;

        std::mutex _mutex;
        std::condition_variable _wake;
        std::condition_variable _idle;
        std::unordered_map<uint32_t, std::shared_ptr<Served>> _served;
        // Requests with something to do, served round-robin.
        std::deque<std::shared_ptr<Served>> _queue;
        std::vector<std::thread> _workers;
        int _busy  = 0;
        bool _stop = false;

        static constexpr std::size_t kWorkers     = 3;
        static constexpr std::size_t kMaxRequests = 1024;

        void WorkerLoop();
        // One step of a request: opens it, or sends its next chunk. False once it is finished.
        bool Step(Served &served, std::string &buffer);
        void Schedule(const std::shared_ptr<Served> &served);
        std::shared_ptr<Served> Find(uint32_t requestId);

      public:
        ResourceServer(Channel &channel, RootLookup findRoot);
        ~ResourceServer();

        ResourceServer(const ResourceServer &)            = delete;
        ResourceServer &operator=(const ResourceServer &) = delete;

        void Start();
        void Stop();

        // Drops every request of a host that is gone, once no worker is still serving one of them.
        // The channel must already be closed, so nothing more arrives for them.
        void Reset();

        void OnRequest(Protocol::ResourceRequest request);
        void OnAck(const Protocol::ResourceAck &ack);
        void OnSeek(const Protocol::ResourceSeek &seek);
        void OnCancel(const Protocol::ResourceCancel &cancel);
    };
} // namespace Framework::GUI::Remote
