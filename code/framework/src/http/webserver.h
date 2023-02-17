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
#include <mongoose.h>
#include <string>
#include <thread>
#include <unordered_map>

namespace Framework::HTTP {
    using ResponseCallback = fu2::function<void(int32_t code, const std::string &message) const>;
    using RequestCallback  = fu2::function<void(struct mg_connection *c, void *ev_data, ResponseCallback) const>;
    using NotFoundCallback = fu2::function<void(std::string) const>;
    using CallbacksMap     = std::unordered_map<const char *, RequestCallback>;

    class Webserver {
      public:
        bool Init(const std::string &host, int32_t port, const std::string &);

        bool Shutdown();

        void RegisterRequest(const char *, const RequestCallback &);

        void SetNotFoundCallback(NotFoundCallback cb) {
            _notFoundCallback = cb;
        }

        CallbacksMap GetRegisteredRequestCallbacks() const {
            return _registeredRequestCallback;
        }

        NotFoundCallback GetNotFoundCallback() const {
            return _notFoundCallback;
        }

        const std::string &GetServeDirectory() {
            return _serveDir;
        }

      protected:
        void ServeDirectory(const std::string &dir);

      private:
        mg_mgr _manager;
        CallbacksMap _registeredRequestCallback;
        NotFoundCallback _notFoundCallback;
        std::atomic_bool _running;
        std::thread _webThread;
        std::string _serveDir;
    };
} // namespace Framework::HTTP
