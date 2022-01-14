/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <chrono>
#include <functional>
#include <vector>

namespace Framework::Utils {
    /**
     * Delays a scope with set amount of milliseconds.
     */
    class DelayScope {
      public:
        DelayScope(uint32_t delay, std::function<void()> callback);

        /// Call this in a loop to process all delayed scopes.
        static void Update();

        static void Push(uint32_t delay, std::function<void()> callback);

      private:
        bool FireWhenReady();

        std::chrono::time_point<std::chrono::high_resolution_clock> _created;

        std::function<void()> _callback = nullptr;

        uint32_t _delay = 0;

        static std::vector<DelayScope *> activeHandlers;
    };
} // namespace Framework::Utils
