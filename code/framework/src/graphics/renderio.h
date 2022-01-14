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

namespace Framework::Graphics {
    class RenderIO final {
      public:
        using Proc = std::function<void()>;

      private:
        std::queue<Proc> _mainQueue;
        std::queue<Proc> _renderQueue;

      public:
        void AddRenderTask(Proc proc);
        void RespondTask(Proc proc);

        // Poll these in distinct threads
        void UpdateMainThread();
        void UpdateRenderThread();
    };
} // namespace Framework::Graphics
