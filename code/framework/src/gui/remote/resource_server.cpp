/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_server.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace Framework::GUI::Remote {
    namespace {
        constexpr std::size_t kMaxProviderPath = 4096;

        // The host already normalized the path; it is checked again because the host is another
        // process. Providers keep their own containment checks on top.
        bool IsProviderPath(const std::string &path) {
            if (path.empty() || path.size() > kMaxProviderPath || path.front() == '/' || path.find_first_of(std::string_view("\\:\0", 3)) != std::string::npos) {
                return false;
            }
            std::size_t start = 0;
            while (start <= path.size()) {
                const std::size_t end          = (std::min)(path.find('/', start), path.size());
                const std::string_view segment = std::string_view(path).substr(start, end - start);
                if (segment == "." || segment == "..") {
                    return false;
                }
                start = end + 1;
            }
            return true;
        }
    } // namespace

    ResourceServer::ResourceServer(Channel &channel, RootLookup findRoot): _channel(channel), _findRoot(std::move(findRoot)) {}

    ResourceServer::~ResourceServer() {
        Stop();
    }

    void ResourceServer::Start() {
        if (!_workers.empty()) {
            return;
        }
        _stop = false;
        for (std::size_t i = 0; i < kWorkers; ++i) {
            _workers.emplace_back([this] {
                WorkerLoop();
            });
        }
    }

    void ResourceServer::Stop() {
        {
            std::scoped_lock lock(_mutex);
            _stop = true;
        }
        _wake.notify_all();
        for (auto &worker : _workers) {
            worker.join();
        }
        _workers.clear();

        std::scoped_lock lock(_mutex);
        _served.clear();
        _queue.clear();
    }

    void ResourceServer::Reset() {
        std::unique_lock lock(_mutex);
        _served.clear();
        _queue.clear();
        // A worker still on one of them would send it into the next host's channel.
        _idle.wait(lock, [this] {
            return _busy == 0;
        });
    }

    std::shared_ptr<ResourceServer::Served> ResourceServer::Find(uint32_t requestId) {
        const auto it = _served.find(requestId);
        return it == _served.end() ? nullptr : it->second;
    }

    void ResourceServer::Schedule(const std::shared_ptr<Served> &served) {
        served->scheduled = true;
        _queue.push_back(served);
        _wake.notify_one();
    }

    void ResourceServer::OnRequest(Protocol::ResourceRequest request) {
        const uint32_t requestId = request.requestId;
        {
            std::scoped_lock lock(_mutex);
            if (_served.size() < kMaxRequests && !_served.contains(requestId)) {
                auto served     = std::make_shared<Served>();
                served->request = std::move(request);
                _served.emplace(requestId, served);
                Schedule(served);
                return;
            }
        }
        Protocol::ResourceResponse missing;
        missing.requestId = requestId;
        _channel.Send(missing);
    }

    void ResourceServer::OnAck(const Protocol::ResourceAck &ack) {
        std::scoped_lock lock(_mutex);
        const auto served = Find(ack.requestId);
        if (!served) {
            return;
        }
        // Never more than a window, whatever the host claims to have consumed.
        served->credit = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(served->credit) + ack.bytes, static_cast<uint64_t>(Protocol::kResourceWindowBytes)));
        if (!served->scheduled) {
            Schedule(served);
        }
    }

    void ResourceServer::OnSeek(const Protocol::ResourceSeek &seek) {
        std::scoped_lock lock(_mutex);
        const auto served = Find(seek.requestId);
        if (!served) {
            return;
        }
        served->seekTo = (std::max)(served->seekTo, seek.offset);
        if (!served->scheduled) {
            Schedule(served);
        }
    }

    void ResourceServer::OnCancel(const Protocol::ResourceCancel &cancel) {
        std::scoped_lock lock(_mutex);
        const auto it = _served.find(cancel.requestId);
        if (it == _served.end()) {
            return;
        }
        if (it->second->scheduled) {
            it->second->cancelled = true;
        }
        else {
            _served.erase(it);
        }
    }

    void ResourceServer::WorkerLoop() {
        std::string buffer(Protocol::kResourceChunkBytes, '\0');
        for (;;) {
            std::shared_ptr<Served> served;
            bool cancelled = false;
            {
                std::unique_lock lock(_mutex);
                _wake.wait(lock, [this] {
                    return _stop || !_queue.empty();
                });
                if (_stop) {
                    return;
                }
                served = std::move(_queue.front());
                _queue.pop_front();
                cancelled = served->cancelled;
                ++_busy;
            }

            const bool more = !cancelled && Step(*served, buffer);

            std::scoped_lock lock(_mutex);
            --_busy;
            const auto it      = _served.find(served->request.requestId);
            const bool current = it != _served.end() && it->second == served;
            if (current && (!more || served->cancelled)) {
                _served.erase(it);
            }
            else if (current && (served->credit > 0 || served->seekTo > served->position)) {
                // To the back: every other open request gets its turn first.
                Schedule(served);
            }
            else {
                served->scheduled = false;
            }
            if (_busy == 0) {
                _idle.notify_all();
            }
        }
    }

    bool ResourceServer::Step(Served &served, std::string &buffer) {
        const uint32_t requestId = served.request.requestId;
        if (!served.stream) {
            Protocol::ResourceResponse response;
            response.requestId = requestId;

            const auto provider = IsProviderPath(served.request.path) ? _findRoot(served.request.host) : nullptr;
            Resources::ResourceStat stat;
            auto stream = provider ? provider->Open(served.request.path, stat) : nullptr;
            if (!stream) {
                _channel.Send(response);
                return false;
            }
            response.found     = true;
            response.size      = stat.size;
            response.immutable = stat.immutable;
            response.mimeType  = stat.mimeType;
            if (!_channel.Send(response)) {
                return false;
            }
            served.stream = std::move(stream);
            return true;
        }

        uint32_t credit = 0;
        uint64_t seekTo = 0;
        {
            std::scoped_lock lock(_mutex);
            credit = served.credit;
            seekTo = served.seekTo;
        }

        Protocol::ResourceEnd end;
        end.requestId = requestId;
        if (seekTo > served.position) {
            const uint64_t distance    = (std::min)(seekTo - served.position, static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()));
            const std::int64_t skipped = served.stream->Skip(static_cast<std::int64_t>(distance));
            if (skipped < 0) {
                _channel.Send(end);
                return false;
            }
            served.position += static_cast<uint64_t>(skipped);
        }
        if (credit == 0) {
            return true;
        }

        const std::int64_t read = served.stream->Read(buffer.data(), (std::min)(buffer.size(), static_cast<std::size_t>(credit)));
        if (read <= 0) {
            end.ok = read == 0;
            _channel.Send(end);
            return false;
        }
        Protocol::ResourceData data;
        data.requestId = requestId;
        data.offset    = served.position;
        data.bytes.assign(buffer.data(), static_cast<std::size_t>(read));
        if (!_channel.Send(data)) {
            return false;
        }
        served.position += static_cast<uint64_t>(read);

        std::scoped_lock lock(_mutex);
        served.credit -= static_cast<uint32_t>(read);
        return true;
    }
} // namespace Framework::GUI::Remote
