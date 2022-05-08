/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <function2.hpp>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <memory>
#include <utility>

namespace Framework::Utils {
    using CommandCallback = fu2::function<void(const std::string &) const>;
    class CommandListener {
      private:
        std::shared_ptr<std::thread> _currentThread;
        std::queue<std::string> _queue;
        std::mutex _mutex;
        CommandCallback _cb;
        std::atomic_bool _running = false;

      public:
        CommandListener();

        void SetCommandHandler(CommandCallback cb) {
            _cb = std::move(cb);
        }

        void Update();

        void Shutdown();
    };
} // namespace Framework::Utils
