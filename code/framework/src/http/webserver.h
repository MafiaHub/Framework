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
#include <function2.hpp>
#include <httplib.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace Framework::HTTP {
    using RequestCallback = fu2::function<void(const httplib::Request &, httplib::Response &) const>;
    using PostCallback = fu2::function<void(const httplib::Request &, httplib::Response &, const httplib::ContentReader &) const>;

    class Webserver {
      public:
        Webserver();
        bool Init(const std::string &host, int32_t port, const std::string &);

        bool Shutdown();

        void RegisterRequest(const char *, const RequestCallback &) const;
        void RegisterPostRequest(const char *, const PostCallback &) const;

        const std::string &GetServeDirectory() {
            return _serveDir;
        }

      protected:
        void ServeDirectory(const std::string &dir);

      private:
        std::shared_ptr<httplib::Server> _server;
        std::atomic_bool _running;
        std::thread _webThread;
        std::string _serveDir;
    };
} // namespace Framework::HTTP
