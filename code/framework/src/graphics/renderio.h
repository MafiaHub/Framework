/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/channel.h"

namespace Framework::Graphics {
    class RenderIO final {
      private:
        Utils::Channel<> _mainChannel;
        Utils::Channel<> _renderChannel;

      public:
        void AddRenderTask(Utils::Channel<>::Proc proc);
        void RespondTask(Utils::Channel<>::Proc proc);

        // Poll these in distinct threads
        void UpdateMainThread();
        void UpdateRenderThread();
    };
} // namespace Framework::Graphics
