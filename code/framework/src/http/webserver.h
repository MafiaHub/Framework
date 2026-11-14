/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <utils/lifecycle.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <function2/function2.hpp>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Keep httplib out of this header; consumers implementing request callbacks
// include <httplib.h> themselves for the complete Request/Response types.
namespace httplib {
    struct Request;
    struct Response;
    class ContentReader;
    class Server;
} // namespace httplib

namespace Framework::Utils {
    class TaskScheduler;
} // namespace Framework::Utils

namespace Framework::HTTP {
    using RequestCallback = fu2::function<void(const httplib::Request &, httplib::Response &) const>;
    using PostCallback    = fu2::function<void(const httplib::Request &, httplib::Response &, const httplib::ContentReader &) const>;

    class Webserver final: public Framework::Lifecycle {
      public:
        Webserver();
        ~Webserver();
        [[nodiscard]] WebserverError Init(const std::string &host, int32_t port, const std::string &);

        void Shutdown() override;

        // Each in-flight request holds a worker until its response is written; call before Init.
        void SetWorkerCount(size_t count);
        // How long one write to a client may block before the response is abandoned. httplib's 5 s
        // default cuts a large transfer whenever the receiver stalls that long; a route streaming files
        // wants more. Applies to every route.
        void SetWriteTimeout(std::chrono::seconds timeout);

        // GET and HEAD. The callback runs on one of the web workers, several at once: it may read only
        // what it guards itself, never the replicated world or anything else the tick thread owns.
        void RegisterRequest(const std::string &path, const RequestCallback &callback) const;
        // The same, with the callback run on the tick thread while the web worker waits for it: the
        // route for a handler that reads or changes the world. Answers 503 with no scheduler, or when
        // the tick thread has not run it within the scheduler's timeout; a handler that was given up
        // on is never run afterwards. A handler that throws answers 500.
        void RegisterTickThreadRequest(const std::string &path, const RequestCallback &callback) const;
        // The scheduler whose owner runs tick-thread routes, and how long a request waits for it. It must
        // outlive this server's workers: stop the server (Shutdown) before destroying the scheduler. Taking
        // it back with null only keeps later requests off it, not one that already read it.
        void SetTickThreadScheduler(Utils::TaskScheduler *scheduler, std::chrono::milliseconds timeout = std::chrono::seconds(5));
        void RegisterPostRequest(const std::string &path, const PostCallback &callback) const;

        const std::string &GetServeDirectory() const noexcept {
            return _serveDir;
        }

      protected:
        void ServeDirectory(const std::string &dir);

      private:
        std::shared_ptr<httplib::Server> _server;
        std::atomic_bool _running;
        std::atomic<Utils::TaskScheduler *> _tickScheduler {nullptr};
        std::atomic<int64_t> _tickTimeoutMs {5000};
        std::jthread _webThread;
        std::string _serveDir;
        // Dispatched before the served directory is looked at, so a declared route is never shadowed by a file.
        mutable std::mutex _getRoutesMutex;
        mutable std::vector<std::pair<std::regex, RequestCallback>> _getRoutes;
    };
} // namespace Framework::HTTP
