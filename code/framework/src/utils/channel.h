/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <functional>
#include <queue>
#include <mutex>

namespace Framework::Utils {
    template <typename ProcT = std::function<void()>> 
    class Channel {
      public:
        using Proc = ProcT;

      private:
        std::queue<Proc> _queue;
        std::mutex _mtx;

      public:
        inline void PushTask(Proc proc) {
            std::lock_guard lock(_mtx);
            _queue.push(proc);
        }

        void Update() {
            std::lock_guard lock(_mtx);
            while (!_queue.empty()) {
                const auto &proc = _queue.front();
                proc();
                _queue.pop();
            }
        }
    };
} // namespace Framework::Utils
