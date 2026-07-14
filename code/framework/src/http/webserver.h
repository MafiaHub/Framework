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
#include <cstdint>
#include <function2.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

// Keep httplib out of this header; consumers implementing request callbacks
// include <httplib.h> themselves for the complete Request/Response types.
namespace httplib {
    struct Request;
    struct Response;
    class ContentReader;
    class Server;
} // namespace httplib

namespace Framework::HTTP {
    using RequestCallback = fu2::function<void(const httplib::Request &, httplib::Response &) const>;
    using PostCallback = fu2::function<void(const httplib::Request &, httplib::Response &, const httplib::ContentReader &) const>;

    class Webserver final : public Framework::Lifecycle {
      public:
        Webserver();
        [[nodiscard]] WebserverError Init(const std::string &host, int32_t port, const std::string &);

        void Shutdown() override;

        void RegisterRequest(const std::string &path, const RequestCallback &callback) const;
        void RegisterPostRequest(const std::string &path, const PostCallback &callback) const;

        const std::string &GetServeDirectory() const noexcept {
            return _serveDir;
        }

      protected:
        void ServeDirectory(const std::string &dir);

      private:
        std::shared_ptr<httplib::Server> _server;
        std::atomic_bool _running;
        std::jthread _webThread;
        std::string _serveDir;
    };
} // namespace Framework::HTTP
