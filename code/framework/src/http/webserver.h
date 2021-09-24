#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mongoose.h>
#include <string>
#include <thread>
#include <unordered_map>

namespace Framework::HTTP {
    using RequestCallback = std::function<void(struct mg_connection *c, void *ev_data)>;
    using NotFoundCallback = std::function<void(std::string)>;
    using CallbacksMap    = std::unordered_map<const char *, RequestCallback>;

    class Webserver {
      public:
        virtual bool Init(const std::string &host, int32_t port, const std::string &);

        bool Shutdown();

        void RegisterRequest(const char *, RequestCallback);

        void SetNotFoundCallback(NotFoundCallback cb){
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
        static void Respond(mg_connection *c, int32_t code, const std::string &message);

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
