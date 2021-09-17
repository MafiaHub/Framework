#pragma once

#include <string>

namespace Framework::Services {
    class Masterlist {
      public:
        void Update();

        void SetInterval(int32_t interval) {
            _interval = interval;
        }

      protected:
        virtual std::string GetGame() {
            return "Generic";
        }

        virtual std::string GetMap() {
            return "None";
        }

        virtual std::string GetSecretKey() {
            return "";
        }

        virtual std::string GetServerName() {
            return "Default server";
        }

        virtual std::string GetHost() {
            return "";
        }

        virtual int32_t GetPort() {
            return 27015;
        }

        virtual std::string GetVersion() {
            return "1.0.0";
        }

        virtual bool IsPassworded() {
            return false;
        }

        virtual int32_t GetMaxPlayers() {
            return 10;
        }

        virtual int32_t GetPlayers() {
            return 0;
        }

      private:
        int32_t _interval   = 60000;
        int64_t _nextUpdate = 0;
    };
} // namespace Framework::Services
