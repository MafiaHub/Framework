/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "gui/remote/channel.h"
#include "gui/resources/resource_provider.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Framework::GUI::Remote {
    // Every fw:// root lives in the client: its providers read the client's virtual filesystem and
    // in-memory pages, which this process cannot see. A request is forwarded over the channel and
    // its CEF file thread blocks here until the client answers. The body arrives in chunks the
    // client sends no further than Protocol::kResourceWindowBytes ahead of what this side has
    // acknowledged, so a request holds at most that much here however large the body is.
    class ResourceBroker final: public std::enable_shared_from_this<ResourceBroker> {
      public:
        struct Chunk {
            uint64_t offset = 0;
            std::string bytes;
        };

        struct Pending {
            bool responded = false;
            bool found     = false;
            Resources::ResourceStat stat;
            std::deque<Chunk> chunks;
            // End of the furthest chunk received, and what the queue holds unacknowledged.
            uint64_t receivedEnd = 0;
            uint64_t buffered    = 0;
            bool ended           = false;
            bool failed          = false;
        };

      private:
        Channel &_channel;
        std::mutex _mutex;
        std::condition_variable _changed;
        std::unordered_map<uint32_t, std::shared_ptr<Pending>> _pending;
        uint32_t _nextId = 1;
        bool _closed     = false;

        // A client that stops answering fails the request rather than a CEF file thread forever.
        static constexpr int kTimeoutSeconds = 30;

        std::shared_ptr<Pending> Find(uint32_t requestId);

      public:
        explicit ResourceBroker(Channel &channel): _channel(channel) {}

        std::unique_ptr<Resources::ResourceStream> Open(const std::string &host, const std::string &path, Resources::ResourceStat &stat);

        // Channel thread.
        void OnResponse(const Protocol::ResourceResponse &response);
        void OnData(Protocol::ResourceData &data);
        void OnEnd(const Protocol::ResourceEnd &end);

        // Waits for the next chunk of |requestId| and acknowledges it; false at the end of the body
        // or on failure.
        bool NextChunk(uint32_t requestId, const std::shared_ptr<Pending> &pending, Chunk &chunk, bool &failed);

        // The body is wanted from |offset| on; what the client already sent below it is dropped.
        void Seek(uint32_t requestId, uint64_t offset);

        // The response is no longer wanted; tells the client to stop sending it.
        void Cancel(uint32_t requestId);

        // The channel is gone: wakes every waiter with a failure.
        void FailAll();
    };

    class RemoteResourceProvider final: public Resources::ResourceProvider {
      private:
        std::string _host;
        std::shared_ptr<ResourceBroker> _broker;

      public:
        RemoteResourceProvider(std::string host, std::shared_ptr<ResourceBroker> broker): _host(std::move(host)), _broker(std::move(broker)) {}

        std::unique_ptr<Resources::ResourceStream> Open(const std::string &path, Resources::ResourceStat &stat) const override {
            return _broker->Open(_host, path, stat);
        }

        std::string Describe() const override {
            return "client root '" + _host + "'";
        }
    };
} // namespace Framework::GUI::Remote
