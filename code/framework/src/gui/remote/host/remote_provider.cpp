/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "remote_provider.h"

#include <logging/logger.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace Framework::GUI::Remote {
    namespace {
        // Reads the body in order, and skips by telling the client where to continue instead of
        // fetching what lies in between.
        class RemoteResourceStream final: public Resources::ResourceStream {
          private:
            std::shared_ptr<ResourceBroker> _broker;
            std::shared_ptr<ResourceBroker::Pending> _pending;
            uint32_t _requestId;
            uint64_t _size;
            ResourceBroker::Chunk _chunk;
            std::size_t _offset = 0;
            // Next byte of the body handed out: _chunk.offset + _offset while inside _chunk.
            uint64_t _position = 0;
            bool _done         = false;

            // 1 with bytes available, 0 at the end of the body, -1 on failure.
            int Fill() {
                while (_offset >= _chunk.bytes.size()) {
                    if (_done) {
                        return 0;
                    }
                    bool failed = false;
                    ResourceBroker::Chunk next;
                    if (!_broker->NextChunk(_requestId, _pending, next, failed)) {
                        _done = true;
                        return failed ? -1 : 0;
                    }
                    // Sent before the client saw a seek past it.
                    if (next.offset + next.bytes.size() <= _position) {
                        continue;
                    }
                    // The client skipped further than it was asked to.
                    if (next.offset > _position) {
                        _done = true;
                        return -1;
                    }
                    _offset = static_cast<std::size_t>(_position - next.offset);
                    _chunk  = std::move(next);
                }
                return 1;
            }

          public:
            RemoteResourceStream(std::shared_ptr<ResourceBroker> broker, std::shared_ptr<ResourceBroker::Pending> pending, uint32_t requestId, uint64_t size): _broker(std::move(broker)), _pending(std::move(pending)), _requestId(requestId), _size(size) {}

            ~RemoteResourceStream() override {
                _broker->Cancel(_requestId);
            }

            std::int64_t Read(void *out, std::size_t bytesToRead) override {
                const int state = Fill();
                if (state <= 0) {
                    return state;
                }
                const std::size_t count = (std::min)(bytesToRead, _chunk.bytes.size() - _offset);
                std::memcpy(out, _chunk.bytes.data() + _offset, count);
                _offset += count;
                _position += count;
                return static_cast<std::int64_t>(count);
            }

            std::int64_t Skip(std::int64_t bytesToSkip) override {
                if (bytesToSkip < 0) {
                    return -1;
                }
                const uint64_t target   = (std::min)(_position + static_cast<uint64_t>(bytesToSkip), (std::max)(_size, _position));
                const uint64_t chunkEnd = _chunk.offset + _chunk.bytes.size();
                if (_offset < _chunk.bytes.size() && target <= chunkEnd) {
                    _offset += static_cast<std::size_t>(target - _position);
                }
                else {
                    _chunk  = {};
                    _offset = 0;
                    if (!_done) {
                        _broker->Seek(_requestId, target);
                    }
                }
                const uint64_t skipped = target - _position;
                _position              = target;
                return static_cast<std::int64_t>(skipped);
            }
        };
    } // namespace

    std::shared_ptr<ResourceBroker::Pending> ResourceBroker::Find(uint32_t requestId) {
        const auto it = _pending.find(requestId);
        return it == _pending.end() ? nullptr : it->second;
    }

    std::unique_ptr<Resources::ResourceStream> ResourceBroker::Open(const std::string &host, const std::string &path, Resources::ResourceStat &stat) {
        auto pending       = std::make_shared<Pending>();
        uint32_t requestId = 0;
        {
            std::scoped_lock lock(_mutex);
            if (_closed) {
                return nullptr;
            }
            requestId = _nextId++;
            if (requestId == 0) {
                requestId = _nextId++;
            }
            _pending[requestId] = pending;
        }

        Protocol::ResourceRequest request;
        request.requestId = requestId;
        request.host      = host;
        request.path      = path;
        if (!_channel.Send(request)) {
            std::scoped_lock lock(_mutex);
            _pending.erase(requestId);
            return nullptr;
        }

        std::unique_lock lock(_mutex);
        const bool answered = _changed.wait_for(lock, std::chrono::seconds(kTimeoutSeconds), [&pending] {
            return pending->responded || pending->failed;
        });
        if (!answered || pending->failed || !pending->found) {
            if (!answered) {
                Framework::Logging::GetLogger("Web")->warn("fw://{}/{} got no answer from the client within {} s", host, path, kTimeoutSeconds);
            }
            _pending.erase(requestId);
            lock.unlock();
            if (!answered) {
                Protocol::ResourceCancel cancel;
                cancel.requestId = requestId;
                _channel.Send(cancel);
            }
            return nullptr;
        }
        stat = pending->stat;
        lock.unlock();
        return std::make_unique<RemoteResourceStream>(shared_from_this(), pending, requestId, stat.size);
    }

    void ResourceBroker::OnResponse(const Protocol::ResourceResponse &response) {
        std::scoped_lock lock(_mutex);
        const auto pending = Find(response.requestId);
        if (!pending || pending->responded) {
            return;
        }
        pending->responded      = true;
        pending->found          = response.found;
        pending->stat.size      = response.size;
        pending->stat.immutable = response.immutable;
        pending->stat.mimeType  = response.mimeType;
        _changed.notify_all();
    }

    void ResourceBroker::OnData(Protocol::ResourceData &data) {
        std::scoped_lock lock(_mutex);
        const auto pending = Find(data.requestId);
        if (!pending || !pending->responded || pending->ended || pending->failed) {
            return;
        }
        // In order, and within the window: anything else is a client not keeping to the protocol.
        const uint64_t size = data.bytes.size();
        if (size == 0 || data.offset < pending->receivedEnd || pending->buffered + size > Protocol::kResourceWindowBytes) {
            Framework::Logging::GetLogger("Web")->error("Resource request {}: chunk at {} ({} bytes) out of order or over the window; failing it", data.requestId, data.offset, size);
            pending->failed = true;
            pending->chunks.clear();
            pending->buffered = 0;
            _changed.notify_all();
            return;
        }
        pending->receivedEnd = data.offset + size;
        pending->buffered += size;
        pending->chunks.push_back({data.offset, std::move(data.bytes)});
        _changed.notify_all();
    }

    void ResourceBroker::OnEnd(const Protocol::ResourceEnd &end) {
        std::scoped_lock lock(_mutex);
        const auto pending = Find(end.requestId);
        if (!pending || !pending->responded) {
            return;
        }
        pending->ended  = true;
        pending->failed = !end.ok;
        _changed.notify_all();
    }

    bool ResourceBroker::NextChunk(uint32_t requestId, const std::shared_ptr<Pending> &pending, Chunk &chunk, bool &failed) {
        std::unique_lock lock(_mutex);
        const bool ready = _changed.wait_for(lock, std::chrono::seconds(kTimeoutSeconds), [&pending] {
            return !pending->chunks.empty() || pending->ended || pending->failed;
        });
        if (!pending->failed && !pending->chunks.empty()) {
            chunk = std::move(pending->chunks.front());
            pending->chunks.pop_front();
            pending->buffered -= chunk.bytes.size();
            const bool ended = pending->ended;
            lock.unlock();
            // Taken off the queue, so its room in the window is the client's again.
            if (!ended) {
                Protocol::ResourceAck ack;
                ack.requestId = requestId;
                ack.bytes     = static_cast<uint32_t>(chunk.bytes.size());
                _channel.Send(ack);
            }
            return true;
        }
        failed = !ready || pending->failed;
        if (!ready) {
            Framework::Logging::GetLogger("Web")->warn("Resource request {} stalled for {} s; failing it", requestId, kTimeoutSeconds);
        }
        return false;
    }

    void ResourceBroker::Seek(uint32_t requestId, uint64_t offset) {
        Protocol::ResourceSeek seek;
        seek.requestId = requestId;
        seek.offset    = offset;
        _channel.Send(seek);
    }

    void ResourceBroker::Cancel(uint32_t requestId) {
        bool wasPending = false;
        {
            std::scoped_lock lock(_mutex);
            const auto pending = Find(requestId);
            wasPending         = pending && !pending->ended && !pending->failed;
            _pending.erase(requestId);
        }
        if (wasPending) {
            Protocol::ResourceCancel cancel;
            cancel.requestId = requestId;
            _channel.Send(cancel);
        }
    }

    void ResourceBroker::FailAll() {
        std::scoped_lock lock(_mutex);
        _closed = true;
        for (auto &[id, pending] : _pending) {
            pending->failed = true;
        }
        _changed.notify_all();
    }
} // namespace Framework::GUI::Remote
