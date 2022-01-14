/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <functional>
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

        void SetCommandHandler(CommandCallback cb) {
            _cb = cb;
        }

        void Update();
    };
} // namespace Framework::Utils
