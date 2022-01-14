/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mongoose.h>
#include <string>
#include <thread>
#include <unordered_map>

namespace Framework::HTTP {
    using ResponseCallback = std::function<void(int32_t code, const std::string &message)>;
    using RequestCallback  = std::function<void(struct mg_connection *c, void *ev_data, ResponseCallback)>;
    using NotFoundCallback = std::function<void(std::string)>;
    using CallbacksMap     = std::unordered_map<const char *, RequestCallback>;

    class Webserver {
      public:
        bool Init(const std::string &host, int32_t port, const std::string &);

        bool Shutdown();

        void RegisterRequest(const char *, RequestCallback);

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
