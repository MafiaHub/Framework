/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "renderio.h"

namespace Framework::Graphics {

    void RenderIO::AddRenderTask(Utils::Channel<>::Proc proc) {
        _mainChannel.PushTask(proc);
    }

    void RenderIO::RespondTask(Utils::Channel<>::Proc proc) {
        _renderChannel.PushTask(proc);
    }

    void RenderIO::UpdateMainThread() {
        _mainChannel.Update();
    }

    void RenderIO::UpdateRenderThread() {
        _renderChannel.Update();
    }
} // namespace Framework::Graphics
