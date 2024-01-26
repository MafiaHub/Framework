#pragma once

#include <string>

#include <utils/time.h>

#include <httplib.h>

namespace Framework::Services {
    enum class MasterlistType: uint8_t {
        Client,
        Server
    };

    class Masterlist {
        private:
            httplib::Client *_client;
            httplib::Headers _defaultHeaders;

            MasterlistType _type;
            std::string _pushKey;
            bool _isInitialized = false;

            Utils::Time::TimePoint _lastPingAt = {};

        public:
            Masterlist(MasterlistType);
            ~Masterlist() = delete;

            bool Init(std::string &);
            bool Shutdown();

            void Fetch();

            bool Ping();

            bool IsInitialized() const {
                return _isInitialized;
            }
    };
};