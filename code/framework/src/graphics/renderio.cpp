/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "renderio.h"

namespace Framework::Graphics {

    void RenderIO::AddRenderTask(Proc proc) {
        _renderQueue.push(proc);
    }

    void RenderIO::RespondTask(Proc proc) {
        _mainQueue.push(proc);
    }

    void RenderIO::UpdateMainThread() {
        while (!_mainQueue.empty()) {
            const auto proc = _mainQueue.front();
            proc();
            _mainQueue.pop();
        }
    }

    void RenderIO::UpdateRenderThread() {
        while (!_renderQueue.empty()) {
            const auto proc = _renderQueue.front();
            proc();
            _renderQueue.pop();
        }
    }
} // namespace Framework::Graphics
