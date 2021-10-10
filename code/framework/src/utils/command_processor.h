#pragma once

#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace Framework::Utils {
    using CommandCallback = std::function<void(const std::string &)>;
    class CommandProcessor {
      private:
        std::shared_ptr<std::thread> _currentThread;
        std::queue<std::string> _queue;
        std::mutex _mutex;
        CommandCallback _cb;

      public:
        CommandProcessor();

        void SetCommandHandler(CommandCallback cb){
            _cb = cb;
        }

        void Update();
    };
} // namespace Framework::Utils