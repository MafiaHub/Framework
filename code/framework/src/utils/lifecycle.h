/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>

namespace Framework {
    class Lifecycle {
      public:
        virtual ~Lifecycle() = default;

        Lifecycle(const Lifecycle &)            = delete;
        Lifecycle &operator=(const Lifecycle &) = delete;

        virtual void Shutdown() {
            _initialized = false;
        }

        virtual void Update() {}

        bool IsInitialized() const noexcept {
            return _initialized;
        }

      protected:
        Lifecycle()        = default;
        std::atomic<bool> _initialized = false;
    };
} // namespace Framework
